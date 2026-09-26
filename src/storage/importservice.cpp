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

bool ImportService::install(const QString& staged, const QString& managed, QString* error)
{
    if (m_installHook && !m_installHook()) {
        if (error)
            *error = QStringLiteral("Installation failed (test hook).");
        return false;
    }
    if (!QDir().mkpath(QFileInfo(managed).absolutePath())) {
        if (error)
            *error = QStringLiteral("Cannot create the folder for %1.").arg(managed);
        return false;
    }
    return commitMove(staged, managed, error);
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

    // Closing an operation must itself succeed; otherwise the result says so.
    const auto fail = [this, id, &result](ImportPhase phase, Outcome outcome, const QString& error) {
        auto closed = wait(m_library.run([id, phase, error](QSqlDatabase& db) {
            return catalog::closeImport(db, id, phase, std::nullopt, error);
        }));
        result.outcome = outcome;
        result.error = error;
        if (!closed) {
            result.outcome = Outcome::Failed;
            result.error += QStringLiteral(" Recording the import's end also failed: %1").arg(closed.error().message);
        }
        return result;
    };
    const auto duplicate = [this, id, &result](const QString& sha, qint64 bytes, const BookId& book, Outcome outcome) {
        auto closed = wait(m_library.run([id, sha, bytes, book](QSqlDatabase& db) {
            return catalog::closeImportAsDuplicate(db, id, sha, bytes, book);
        }));
        if (!closed) {
            result.outcome = Outcome::Failed;
            result.error = QStringLiteral("The file is already in the library, but recording that failed: %1")
                               .arg(closed.error().message);
            return result;
        }
        result.book = book;
        result.outcome = outcome;
        return result;
    };
    if (crashAt(ImportStage::Begun)) {
        result.outcome = Outcome::Interrupted;
        return result;
    }

    // 2. Copy and hash into staging, then verify the staged bytes.
    const QString staged = m_layout.absolute(LibraryLayout::stagedSourcePath(id));
    QDir().mkpath(QFileInfo(staged).absolutePath());
    CopyHooks hooks;
    hooks.progress = progress;
    hooks.afterFirstChunk = m_afterFirstChunk;
    const CopyOutcome copy = copyVerified(absoluteSource, staged, cancel, hooks);
    if (!copy.ok()) {
        removeStaging(id);
        const bool wasCancelled = copy.status == CopyOutcome::Status::Cancelled;
        return fail(wasCancelled ? ImportPhase::Cancelled : ImportPhase::Failed,
                    wasCancelled ? Outcome::Cancelled : Outcome::Failed, copy.error);
    }
    result.sha256 = copy.sha256;
    result.bytes = copy.bytes;
    if (crashAt(ImportStage::Staged)) {
        result.outcome = Outcome::Interrupted;
        return result;
    }

    // 3. Deduplicate by exact SHA-256; never by title.
    const QString sha = copy.sha256;
    auto existing = wait(m_library.run([sha](QSqlDatabase& db) -> Result<std::optional<BookSummary>> {
        auto found = catalog::findBookBySha256(db, sha);
        if (!found)
            return found.error();
        if (!found.value())
            return std::optional<BookSummary>();
        auto details = catalog::bookDetails(db, *found.value());
        if (!details)
            return details.error();
        return std::optional<BookSummary>(details.value().summary);
    }));
    if (!existing) {
        removeStaging(id);
        return fail(ImportPhase::Failed, Outcome::Failed, existing.error().message);
    }
    if (existing.value()) {
        removeStaging(id);  // The same bytes are already managed.
        const BookSummary& summary = *existing.value();
        return duplicate(sha, copy.bytes, summary.id,
                         summary.lifecycle == Lifecycle::Trashed ? Outcome::DuplicateInTrash : Outcome::Duplicate);
    }

    const AssetId asset = AssetId::create();
    auto verified = wait(m_library.run([id, sha, bytes = copy.bytes, asset](QSqlDatabase& db) {
        return catalog::markImportVerified(db, id, sha, bytes, asset);
    }));
    if (!verified) {
        removeStaging(id);
        return fail(ImportPhase::Failed, Outcome::Failed, verified.error().message);
    }
    if (crashAt(ImportStage::VerifiedRecorded)) {
        result.outcome = Outcome::Interrupted;
        return result;
    }

    // 4. Install at the stable managed path with a strict same-volume move.
    //    The external original still exists, so a failure here closes the
    //    operation and the user can import again.
    const QString managedRelative = LibraryLayout::managedSourcePath(asset);
    const QString managed = m_layout.absolute(managedRelative);
    QString installError;
    if (!install(staged, managed, &installError)) {
        removeStaging(id);
        return fail(ImportPhase::Failed, Outcome::Failed, QStringLiteral("Cannot install the copy: %1").arg(installError));
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
            if (other && other.value())
                return duplicate(sha, copy.bytes, *other.value(), Outcome::Duplicate);
        }
        return fail(ImportPhase::Failed, Outcome::Failed, registered.error().message);
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
        const auto close = [this, id, &report](ImportPhase phase, const QString& error) {
            auto closed = wait(m_library.run([id, phase, error](QSqlDatabase& db) {
                return catalog::closeImport(db, id, phase, std::nullopt, error);
            }));
            if (!closed)
                report.notes << QStringLiteral("could not close %1: %2").arg(id.toString(), closed.error().message);
            return bool(closed);
        };

        if (op.phase == ImportPhase::Copying) {
            // Nothing was verified: discard the partial copy. The original was never touched.
            removeStaging(id);
            if (close(ImportPhase::Abandoned,
                      QStringLiteral("Interrupted before the copy was verified; import %1 again.").arg(op.sourceName))) {
                ++report.abandoned;
                report.notes << QStringLiteral("abandoned %1 (%2)").arg(id.toString(), op.sourceName);
            }
            continue;
        }

        // Verified. The staged copy is removed only once a good installed copy
        // exists or the bytes turned out to be catalogued already.
        const QString sha = *op.sha256;
        const qint64 bytes = op.byteSize.value_or(0);
        const QString managedRelative = LibraryLayout::managedSourcePath(*op.assetId);
        const QString managed = m_layout.absolute(managedRelative);
        const QString staged = m_layout.absolute(LibraryLayout::stagedSourcePath(id));
        claimedAssets.insert(op.assetId->toString());

        auto existing = wait(m_library.run([sha](QSqlDatabase& db) { return catalog::findBookBySha256(db, sha); }));
        if (!existing) {
            report.notes << QStringLiteral("kept %1 open: %2").arg(id.toString(), existing.error().message);
            ++report.deferred;
            continue;
        }
        if (existing.value()) {
            auto closed = wait(m_library.run([id, sha, bytes, book = *existing.value()](QSqlDatabase& db) {
                return catalog::closeImportAsDuplicate(db, id, sha, bytes, book);
            }));
            if (!closed) {
                report.notes << QStringLiteral("could not close %1 as duplicate: %2").arg(id.toString(), closed.error().message);
                ++report.deferred;
                continue;
            }
            if (QFile::exists(managed) && removeIfUnreferenced(managedRelative))
                ++report.removedUnreferencedFiles;
            removeStaging(id);
            ++report.duplicates;
            report.notes << QStringLiteral("duplicate %1 (%2)").arg(id.toString(), op.sourceName);
            continue;
        }

        const bool managedGood = QFile::exists(managed) && sha256OfFile(managed) == sha;
        const bool stagedGood = QFile::exists(staged) && sha256OfFile(staged) == sha;
        if (!managedGood && !stagedGood) {
            // Neither copy holds the verified bytes; they are unusable.
            if (QFile::exists(managed) && removeIfUnreferenced(managedRelative))
                ++report.removedUnreferencedFiles;
            removeStaging(id);
            if (close(ImportPhase::Failed,
                      QStringLiteral("The verified copy of %1 was missing or damaged after an interruption; import "
                                     "it again.")
                          .arg(op.sourceName))) {
                ++report.failed;
                report.notes << QStringLiteral("failed %1 (%2)").arg(id.toString(), op.sourceName);
            }
            continue;
        }
        if (!managedGood) {
            // A wrong-digest file at the destination is never catalogued (the
            // asset ID is reserved, not registered); replace it with the good stage.
            if (QFile::exists(managed)) {
                if (!removeIfUnreferenced(managedRelative)) {
                    report.notes << QStringLiteral("kept %1 open: cannot clear %2").arg(id.toString(), managedRelative);
                    ++report.deferred;
                    continue;
                }
                ++report.removedUnreferencedFiles;
            }
            QString error;
            if (!install(staged, managed, &error)) {
                report.notes << QStringLiteral("kept %1 open for retry: %2").arg(id.toString(), error);
                ++report.deferred;
                continue;  // The verified stage stays; the next recovery retries.
            }
        }

        NewBook book;
        book.asset.id = *op.assetId;
        book.asset.sha256 = sha;
        book.asset.byteSize = bytes;
        book.asset.managedPath = managedRelative;
        book.originalFileName = op.sourceName;
        book.originalPath = op.sourcePath;
        auto registered = wait(m_library.run([id, book](QSqlDatabase& db) { return catalog::completeImport(db, id, book); }));
        if (!registered) {
            report.notes << QStringLiteral("kept %1 open: registration failed: %2")
                                .arg(id.toString(), registered.error().message);
            ++report.deferred;
            continue;
        }
        removeStaging(id);
        ++report.registered;
        report.notes << QStringLiteral("registered %1 (%2)").arg(id.toString(), op.sourceName);
    }

    // Staging folders without an open operation are leftovers from closed or
    // unrecorded attempts; they never hold catalogued files.
    auto stillOpen = wait(m_library.run([](QSqlDatabase& db) { return catalog::openImports(db); }));
    if (stillOpen) {
        QSet<QString> openIds;
        for (const ImportOperation& op : stillOpen.value())
            openIds.insert(op.id.toString());
        QDir staging(m_layout.absolute(QStringLiteral("staging")));
        for (const QString& entry : staging.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (openIds.contains(entry))
                continue;
            if (QDir(staging.filePath(entry)).removeRecursively())
                ++report.removedStagingDirectories;
        }
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
