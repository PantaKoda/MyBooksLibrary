#include "storage/importservice.h"

#include "catalog/catalog.h"
#include "catalog/imports.h"
#include "catalog/library.h"
#include "storage/filecopy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

namespace mbl::storage {

using namespace mbl::domain;
using Outcome = ImportResult::Outcome;

namespace {

template <typename T>
T wait(QFuture<T> future)
{
    return future.result();
}

} // namespace

QString outcomeName(Outcome outcome)
{
    switch (outcome) {
    case Outcome::Imported: return QStringLiteral("imported");
    case Outcome::Duplicate: return QStringLiteral("duplicate");
    case Outcome::DuplicateInTrash: return QStringLiteral("duplicate_in_trash");
    case Outcome::Failed: return QStringLiteral("failed");
    case Outcome::Cancelled: return QStringLiteral("cancelled");
    case Outcome::Interrupted: return QStringLiteral("interrupted");
    }
    return QStringLiteral("failed");
}

ImportService::ImportService(catalog::Library& library) : m_library(library), m_layout(library.rootDir())
{
    QString error;
    m_layout.ensureDirectories(&error);  // Failures surface as copy/install errors.
}

void ImportService::removeStaging(const ImportId& id) const
{
    QDir(m_layout.absolute(LibraryLayout::stagingDirectory(id))).removeRecursively();
}

bool ImportService::removeIfUnreferenced(const QString& relativePath)
{
    auto referenced = wait(m_library.run(
        [relativePath](QSqlDatabase& db) { return catalog::managedPathReferenced(db, relativePath); }));
    if (!referenced || referenced.value())
        return false;
    const QString absolute = m_layout.absolute(relativePath);
    const bool removed = QFile::remove(absolute);
    QDir().rmdir(QFileInfo(absolute).absolutePath());  // Only succeeds if now empty.
    return removed;
}

ImportResult ImportService::importFile(const QString& sourcePath, const std::atomic_bool* cancel,
                                       const Progress& progress)
{
    ImportResult result;
    result.sourcePath = sourcePath;
    const QFileInfo sourceInfo(sourcePath);
    const FileStamp stamp = stampOf(sourcePath);
    if (!stamp.exists) {
        result.error = QStringLiteral("%1 does not exist or is not a file.").arg(sourcePath);
        return result;
    }
    if (!looksLikePdf(sourcePath)) {
        result.error = QStringLiteral("%1 is not a PDF file.").arg(sourcePath);
        return result;
    }

    // 1. Record the operation before touching the library folder.
    const QString absoluteSource = sourceInfo.absoluteFilePath();
    const QString name = sourceInfo.fileName();
    auto begun = wait(m_library.run([absoluteSource, name, stamp](QSqlDatabase& db) {
        return catalog::beginImport(db, absoluteSource, name, stamp.size, stamp.modified);
    }));
    if (!begun) {
        result.error = begun.error().message;
        return result;
    }
    const ImportId id = begun.value();
    result.operation = id;
    const auto close = [this, id](ImportPhase phase, const std::optional<BookId>& book, const QString& error) {
        wait(m_library.run([id, phase, book, error](QSqlDatabase& db) {
            return catalog::closeImport(db, id, phase, book, error);
        }));
    };
    if (crashAt(ImportStage::Begun)) {
        result.outcome = Outcome::Interrupted;
        return result;
    }

    // 2. Copy and hash into staging, then verify the staged bytes.
    const QString stagedRelative = LibraryLayout::stagedSourcePath(id);
    const QString staged = m_layout.absolute(stagedRelative);
    QDir().mkpath(QFileInfo(staged).absolutePath());
    CopyHooks hooks;
    hooks.progress = progress;
    hooks.afterFirstChunk = m_afterFirstChunk;
    const CopyOutcome copy = copyVerified(absoluteSource, staged, cancel, hooks);
    if (!copy.ok()) {
        removeStaging(id);
        const bool wasCancelled = copy.status == CopyOutcome::Status::Cancelled;
        close(wasCancelled ? ImportPhase::Cancelled : ImportPhase::Failed, std::nullopt, copy.error);
        result.outcome = wasCancelled ? Outcome::Cancelled : Outcome::Failed;
        result.error = copy.error;
        return result;
    }
    result.sha256 = copy.sha256;
    result.bytes = copy.bytes;
    if (crashAt(ImportStage::Staged)) {
        result.outcome = Outcome::Interrupted;
        return result;
    }

    // 3. Deduplicate by exact SHA-256; never by title.
    const QString sha = copy.sha256;
    auto existing = wait(m_library.run([sha](QSqlDatabase& db) -> Result<std::optional<BookDetails>> {
        auto found = catalog::findBookBySha256(db, sha);
        if (!found)
            return found.error();
        if (!found.value())
            return std::optional<BookDetails>();
        auto details = catalog::bookDetails(db, *found.value());
        if (!details)
            return details.error();
        return std::optional<BookDetails>(details.value());
    }));
    if (!existing) {
        removeStaging(id);
        close(ImportPhase::Failed, std::nullopt, existing.error().message);
        result.error = existing.error().message;
        return result;
    }
    if (existing.value()) {
        removeStaging(id);
        const BookSummary& summary = existing.value()->summary;
        close(ImportPhase::Duplicate, summary.id, {});
        result.book = summary.id;
        result.outcome = summary.lifecycle == Lifecycle::Trashed ? Outcome::DuplicateInTrash : Outcome::Duplicate;
        return result;
    }

    const AssetId asset = AssetId::create();
    auto verified = wait(m_library.run([id, sha, bytes = copy.bytes, asset](QSqlDatabase& db) {
        return catalog::markImportVerified(db, id, sha, bytes, asset);
    }));
    if (!verified) {
        removeStaging(id);
        close(ImportPhase::Failed, std::nullopt, verified.error().message);
        result.error = verified.error().message;
        return result;
    }
    if (crashAt(ImportStage::VerifiedRecorded)) {
        result.outcome = Outcome::Interrupted;
        return result;
    }

    // 4. Install at the stable managed path (same filesystem: an atomic rename).
    const QString managedRelative = LibraryLayout::managedSourcePath(asset);
    const QString managed = m_layout.absolute(managedRelative);
    if (!QDir().mkpath(QFileInfo(managed).absolutePath()) || QFile::exists(managed)
        || !QFile::rename(staged, managed)) {
        removeStaging(id);
        const QString error = QStringLiteral("Cannot install %1.").arg(managed);
        close(ImportPhase::Failed, std::nullopt, error);
        result.error = error;
        return result;
    }
    removeStaging(id);
    if (crashAt(ImportStage::Installed)) {
        result.outcome = Outcome::Interrupted;
        return result;
    }

    // 5. Register in one catalog transaction.
    NewBook book;
    book.asset.id = asset;
    book.asset.sha256 = sha;
    book.asset.byteSize = copy.bytes;
    book.asset.managedPath = managedRelative;  // Page count unknown at import: valid.
    book.originalFileName = name;
    book.originalPath = absoluteSource;
    auto registered = wait(m_library.run([id, book](QSqlDatabase& db) { return catalog::completeImport(db, id, book); }));
    if (!registered) {
        // Another import catalogued the same bytes first, or SQL failed: the
        // installed copy is unreferenced and can go.
        removeIfUnreferenced(managedRelative);
        if (registered.error().code == ErrorCode::Duplicate) {
            auto other = wait(m_library.run([sha](QSqlDatabase& db) { return catalog::findBookBySha256(db, sha); }));
            const std::optional<BookId> otherBook = other && other.value() ? other.value() : std::nullopt;
            close(ImportPhase::Duplicate, otherBook, {});
            result.book = otherBook;
            result.outcome = Outcome::Duplicate;
            return result;
        }
        close(ImportPhase::Failed, std::nullopt, registered.error().message);
        result.error = registered.error().message;
        return result;
    }
    result.book = registered.value();
    result.outcome = Outcome::Imported;
    return result;
}

RecoveryReport ImportService::recover()
{
    RecoveryReport report;
    auto open = wait(m_library.run([](QSqlDatabase& db) { return catalog::openImports(db); }));
    if (!open) {
        report.notes << QStringLiteral("Cannot list open imports: %1").arg(open.error().message);
        return report;
    }
    QSet<QString> claimedAssets;

    for (const ImportOperation& op : open.value()) {
        const ImportId id = op.id;
        const auto close = [this, id](ImportPhase phase, const std::optional<BookId>& book, const QString& error) {
            return wait(m_library.run([id, phase, book, error](QSqlDatabase& db) {
                return catalog::closeImport(db, id, phase, book, error);
            }));
        };

        if (op.phase == ImportPhase::Copying) {
            // Nothing was verified: discard the partial copy. The original is untouched.
            removeStaging(id);
            close(ImportPhase::Abandoned, std::nullopt,
                  QStringLiteral("Interrupted before the copy was verified; import %1 again.").arg(op.sourceName));
            ++report.abandoned;
            report.notes << QStringLiteral("abandoned %1 (%2)").arg(id.toString(), op.sourceName);
            continue;
        }

        // Verified: finish if the verified bytes are still available.
        const QString sha = *op.sha256;
        const QString managedRelative = LibraryLayout::managedSourcePath(*op.assetId);
        const QString managed = m_layout.absolute(managedRelative);
        const QString staged = m_layout.absolute(LibraryLayout::stagedSourcePath(id));
        claimedAssets.insert(op.assetId->toString());

        auto existing = wait(m_library.run([sha](QSqlDatabase& db) { return catalog::findBookBySha256(db, sha); }));
        if (existing && existing.value()) {
            if (QFile::exists(managed) && removeIfUnreferenced(managedRelative))
                ++report.removedUnreferencedFiles;
            removeStaging(id);
            close(ImportPhase::Duplicate, existing.value(), {});
            ++report.duplicates;
            report.notes << QStringLiteral("duplicate %1 (%2)").arg(id.toString(), op.sourceName);
            continue;
        }

        bool installed = QFile::exists(managed) && sha256OfFile(managed) == sha;
        if (!installed && QFile::exists(staged) && sha256OfFile(staged) == sha) {
            QDir().mkpath(QFileInfo(managed).absolutePath());
            installed = !QFile::exists(managed) && QFile::rename(staged, managed);
        }
        removeStaging(id);
        if (!installed) {
            if (QFile::exists(managed) && removeIfUnreferenced(managedRelative))
                ++report.removedUnreferencedFiles;
            close(ImportPhase::Failed, std::nullopt,
                  QStringLiteral("The verified copy of %1 was missing or damaged after an interruption; import it "
                                 "again.")
                      .arg(op.sourceName));
            ++report.failed;
            report.notes << QStringLiteral("failed %1 (%2)").arg(id.toString(), op.sourceName);
            continue;
        }

        NewBook book;
        book.asset.id = *op.assetId;
        book.asset.sha256 = sha;
        book.asset.byteSize = op.byteSize.value_or(0);
        book.asset.managedPath = managedRelative;
        book.originalFileName = op.sourceName;
        book.originalPath = op.sourcePath;
        auto registered = wait(m_library.run([id, book](QSqlDatabase& db) { return catalog::completeImport(db, id, book); }));
        if (registered) {
            ++report.registered;
            report.notes << QStringLiteral("registered %1 (%2)").arg(id.toString(), op.sourceName);
        } else {
            report.notes << QStringLiteral("could not register %1: %2").arg(id.toString(), registered.error().message);
        }
    }

    // Staging folders without an open operation are leftovers from closed or
    // unrecorded attempts; they never hold catalogued files.
    auto stillOpen = wait(m_library.run([](QSqlDatabase& db) { return catalog::openImports(db); }));
    QSet<QString> openIds;
    if (stillOpen) {
        for (const ImportOperation& op : stillOpen.value())
            openIds.insert(op.id.toString());
    }
    QDir staging(m_layout.absolute(QStringLiteral("staging")));
    for (const QString& entry : staging.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (openIds.contains(entry))
            continue;
        if (QDir(staging.filePath(entry)).removeRecursively())
            ++report.removedStagingDirectories;
    }

    // Managed files nothing references are reported only; deleting is never automatic.
    QDir files(m_layout.absolute(QStringLiteral("files")));
    for (const QString& entry : files.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString relative = QStringLiteral("files/%1/source.pdf").arg(entry);
        if (claimedAssets.contains(entry) || !QFile::exists(m_layout.absolute(relative)))
            continue;
        auto referenced = wait(m_library.run(
            [relative](QSqlDatabase& db) { return catalog::managedPathReferenced(db, relative); }));
        if (referenced && !referenced.value())
            report.orphanedManagedFiles << relative;
    }
    return report;
}

} // namespace mbl::storage
