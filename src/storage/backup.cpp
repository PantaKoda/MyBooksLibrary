#include "storage/backup.h"

#include "catalog/backup.h"
#include "catalog/exports.h"
#include "catalog/library.h"
#include "storage/exportdestination.h"
#include "storage/filecopy.h"
#include "storage/librarylayout.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace mbl::storage {

using namespace mbl::domain;

namespace {

constexpr int kFormat = 1;
const QString kManifest = QStringLiteral("backup.json");
const QString kCatalog = QStringLiteral("library.sqlite");

struct ManifestFile {
    QString path;  // Relative, under files/ or reports/ (or the catalog).
    QString kind;  // "catalog", "source" or "report".
    qint64 size = 0;
    QString sha256;
};

struct Manifest {
    int format = kFormat;
    QDateTime createdAt;
    int schemaVersion = 0;
    int books = 0;
    ManifestFile catalog;
    QList<ManifestFile> files;
    QStringList missingReports;
};

Error failure(const QString& message)
{
    return Error{ErrorCode::InvalidArgument, message};
}

Error cancelled()
{
    return Error{ErrorCode::InvalidArgument, QStringLiteral("cancelled")};
}

bool isCancelled(const std::atomic_bool* cancel)
{
    return cancel && cancel->load();
}

QString shortId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}

// A manifest path must stay inside the backup: relative, clean, under files/
// or reports/. Anything else is refused before a byte is read or written.
bool safeRelativePath(const QString& path)
{
    if (path.isEmpty() || path.contains(u'\\') || path.contains(u':') || path.startsWith(u'/'))
        return false;
    if (QDir::cleanPath(path) != path || path.startsWith(QLatin1String("..")) || path.contains(QLatin1String("/..")))
        return false;
    return path.startsWith(QLatin1String("files/")) || path.startsWith(QLatin1String("reports/"));
}

QJsonObject toJson(const ManifestFile& f)
{
    return QJsonObject{{QStringLiteral("path"), f.path},
                       {QStringLiteral("kind"), f.kind},
                       {QStringLiteral("size"), double(f.size)},
                       {QStringLiteral("sha256"), f.sha256}};
}

ManifestFile fileFromJson(const QJsonObject& o)
{
    return ManifestFile{o.value(QStringLiteral("path")).toString(), o.value(QStringLiteral("kind")).toString(),
                        qint64(o.value(QStringLiteral("size")).toDouble()), o.value(QStringLiteral("sha256")).toString()};
}

Status writeManifest(const QString& folder, const Manifest& m)
{
    QJsonArray files;
    for (const ManifestFile& f : m.files)
        files.append(toJson(f));
    const QJsonObject root{{QStringLiteral("format"), m.format},
                           {QStringLiteral("application"), QStringLiteral("MyBooksLibrary")},
                           {QStringLiteral("createdAt"), m.createdAt.toString(Qt::ISODateWithMs)},
                           {QStringLiteral("schemaVersion"), m.schemaVersion},
                           {QStringLiteral("books"), m.books},
                           {QStringLiteral("catalog"), toJson(m.catalog)},
                           {QStringLiteral("files"), files},
                           {QStringLiteral("missingReports"), QJsonArray::fromStringList(m.missingReports)}};
    QSaveFile out(QDir(folder).filePath(kManifest));
    if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0
        || !out.commit())
        return Error{ErrorCode::Io, QStringLiteral("The backup's manifest could not be written: %1").arg(out.errorString())};
    return Done{};
}

Result<Manifest> readManifest(const QString& folder)
{
    QFile in(QDir(folder).filePath(kManifest));
    if (!in.open(QIODevice::ReadOnly))
        return failure(QStringLiteral("%1 is not a MyBooksLibrary backup (it has no %2).")
                           .arg(QDir::toNativeSeparators(folder), kManifest));
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(in.readAll(), &error);
    const QJsonObject root = doc.object();
    if (error.error != QJsonParseError::NoError || !doc.isObject()
        || root.value(QStringLiteral("application")).toString() != QLatin1String("MyBooksLibrary"))
        return failure(QStringLiteral("The backup's manifest cannot be read."));
    Manifest m;
    m.format = root.value(QStringLiteral("format")).toInt();
    if (m.format != kFormat)
        return failure(QStringLiteral("This backup has format %1; this version of MyBooksLibrary reads format %2.")
                           .arg(m.format)
                           .arg(kFormat));
    m.createdAt = QDateTime::fromString(root.value(QStringLiteral("createdAt")).toString(), Qt::ISODateWithMs);
    m.schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt();
    m.books = root.value(QStringLiteral("books")).toInt();
    m.catalog = fileFromJson(root.value(QStringLiteral("catalog")).toObject());
    if (m.catalog.path != kCatalog || m.catalog.sha256.size() != 64)
        return failure(QStringLiteral("The backup's manifest does not describe its catalog."));
    for (const QJsonValue& v : root.value(QStringLiteral("files")).toArray()) {
        const ManifestFile f = fileFromJson(v.toObject());
        if (!safeRelativePath(f.path) || f.sha256.size() != 64)
            return failure(QStringLiteral("The backup's manifest lists an invalid file (%1).").arg(f.path));
        m.files << f;
    }
    for (const QJsonValue& v : root.value(QStringLiteral("missingReports")).toArray())
        m.missingReports << v.toString();
    return m;
}

// The catalog file on its own connection: its integrity and schema version,
// and that the backup holds everything it needs: every source it references,
// with the digest it records, and every report (or the report is listed as
// missing). The manifest alone is not trusted to be complete.
Result<int> checkCatalogFile(const QString& file, const Manifest& manifest)
{
    const int expectedVersion = manifest.schemaVersion;
    QHash<QString, QString> listed;  // Path -> digest.
    for (const ManifestFile& f : manifest.files)
        listed.insert(f.path, f.sha256);
    const QString name = QStringLiteral("mbl-backup-check-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    Result<int> result = failure(QStringLiteral("The backup's catalog could not be opened."));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(file);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery q(db);
            if (!q.exec(QStringLiteral("PRAGMA integrity_check")) || !q.next()) {
                result = failure(QStringLiteral("The backup's catalog cannot be checked."));
            } else if (q.value(0).toString() != QLatin1String("ok")) {
                result = failure(QStringLiteral("The backup's catalog is damaged: %1").arg(q.value(0).toString()));
            } else if (!q.exec(QStringLiteral("PRAGMA user_version")) || !q.next()) {
                result = failure(QStringLiteral("The backup's catalog has no schema version."));
            } else if (q.value(0).toInt() != expectedVersion) {
                result = failure(QStringLiteral("The backup's catalog has schema version %1, but its manifest says %2.")
                                     .arg(q.value(0).toInt())
                                     .arg(expectedVersion));
            } else {
                const int version = q.value(0).toInt();
                QString problem;
                if (!q.exec(QStringLiteral("SELECT managed_path, sha256 FROM assets"))) {
                    problem = QStringLiteral("The backup's catalog cannot be read.");
                } else {
                    while (problem.isEmpty() && q.next()) {
                        const QString path = q.value(0).toString();
                        const auto it = listed.constFind(path);
                        if (it == listed.cend())
                            problem = QStringLiteral("The backup is missing %1, which its catalog needs.").arg(path);
                        else if (it->compare(q.value(1).toString(), Qt::CaseInsensitive) != 0)
                            problem = QStringLiteral("%1 in the backup is not the file its catalog records.").arg(path);
                    }
                }
                if (problem.isEmpty()
                    && !q.exec(QStringLiteral("SELECT report_path FROM metadata_runs WHERE report_path IS NOT NULL "
                                              "UNION SELECT report_path FROM toc_runs WHERE report_path IS NOT NULL"))) {
                    problem = QStringLiteral("The backup's catalog cannot be read.");
                }
                while (problem.isEmpty() && q.next()) {
                    const QString path = q.value(0).toString();
                    if (!listed.contains(path) && !manifest.missingReports.contains(path))
                        problem = QStringLiteral("The backup is missing %1, which its catalog needs.").arg(path);
                }
                result = problem.isEmpty() ? Result<int>(version) : Result<int>(failure(problem));
            }
            q.finish();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return result;
}

// Copies one file into the backup or the restored library, and checks the
// copied bytes against the digest they must have.
Status copyChecked(const QString& from, const QString& to, const QString& expectedSha, const QString& what,
                   const std::atomic_bool* cancel, QString* sha = nullptr, qint64* size = nullptr)
{
    if (!QDir().mkpath(QFileInfo(to).absolutePath()))
        return Error{ErrorCode::Io, QStringLiteral("The folder for %1 could not be created.").arg(what)};
    const CopyOutcome copied = copyVerified(from, to, cancel);
    if (copied.status == CopyOutcome::Status::Cancelled)
        return cancelled();
    if (!copied.ok())
        return Error{ErrorCode::Io, QStringLiteral("%1 could not be copied: %2").arg(what, copied.error)};
    if (!expectedSha.isEmpty() && copied.sha256.compare(expectedSha, Qt::CaseInsensitive) != 0)
        return failure(QStringLiteral("%1 does not match its recorded digest; nothing was changed.").arg(what));
    if (sha)
        *sha = copied.sha256;
    if (size)
        *size = copied.bytes;
    return Done{};
}

BackupInfo infoOf(const QString& folder, const Manifest& m)
{
    BackupInfo info;
    info.folder = QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
    info.createdAt = m.createdAt;
    info.schemaVersion = m.schemaVersion;
    info.books = m.books;
    info.bytes = m.catalog.size;
    for (const ManifestFile& f : m.files) {
        (f.kind == QLatin1String("report") ? info.reports : info.sources) += 1;
        info.bytes += f.size;
    }
    info.missingReports = m.missingReports;
    return info;
}

// The next free "<base>", "<base> (2)", ... in `parent`.
QString freeName(const QString& parent, const QString& base)
{
    QString candidate = QDir(parent).filePath(base);
    for (int n = 2; QFileInfo::exists(candidate); ++n)
        candidate = QDir(parent).filePath(QStringLiteral("%1 (%2)").arg(base).arg(n));
    return candidate;
}

} // namespace

Result<BackupInfo> createBackup(catalog::Library& library, const QString& parentFolder, const std::atomic_bool* cancel,
                                const BackupProgress& progress)
{
    const QFileInfo parentInfo(parentFolder);
    if (parentFolder.trimmed().isEmpty() || !parentInfo.isAbsolute() || !parentInfo.isDir())
        return failure(QStringLiteral("Choose an existing folder for the backup."));
    if (isInsideFolder(parentFolder, library.rootDir()))
        return failure(QStringLiteral("A backup cannot be saved inside the library folder."));

    Manifest manifest;
    manifest.createdAt = QDateTime::currentDateTimeUtc();
    const QString parent = QDir::cleanPath(parentInfo.absoluteFilePath());
    const QString base =
        QStringLiteral("MyBooksLibrary backup %1").arg(manifest.createdAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HHmmss")));
    const QString staging = QDir(parent).filePath(QStringLiteral(".%1-%2.partial").arg(base, shortId()));
    if (!QDir().mkpath(staging))
        return Error{ErrorCode::Io, QStringLiteral("The backup folder could not be created in %1.")
                                        .arg(QDir::toNativeSeparators(parent))};
    bool committed = false;
    const auto removeStaging = qScopeGuard([&] {
        if (!committed)
            QDir(staging).removeRecursively();  // Our own partial folder, never the user's.
    });

    // The catalog and what it references, in one database task.
    const QString catalogCopy = QDir(staging).filePath(kCatalog);
    auto snapshot = library.run([catalogCopy](QSqlDatabase& db) { return catalog::snapshotCatalog(db, catalogCopy); })
                        .result();
    if (!snapshot)
        return snapshot.error();
    manifest.schemaVersion = snapshot.value().schemaVersion;
    manifest.books = snapshot.value().books;
    const int total = 1 + int(snapshot.value().sources.size()) + int(snapshot.value().reports.size());
    int done = 1;
    if (progress)
        progress(done, total);

    const LibraryLayout layout(library.rootDir());
    for (const catalog::CatalogSnapshot::Source& s : snapshot.value().sources) {
        if (isCancelled(cancel))
            return cancelled();
        if (!safeRelativePath(s.managedPath))
            return failure(QStringLiteral("The catalog lists a file outside the library (%1).").arg(s.managedPath));
        ManifestFile f{s.managedPath, QStringLiteral("source"), 0, {}};
        // A source whose bytes changed is damage: the backup stops rather than copy it.
        if (auto st = copyChecked(layout.absolute(s.managedPath), QDir(staging).filePath(s.managedPath), s.sha256,
                                  QStringLiteral("The library's copy %1").arg(s.managedPath), cancel, &f.sha256, &f.size);
            !st)
            return st.error();
        manifest.files << f;
        if (progress)
            progress(++done, total);
    }
    for (const QString& report : snapshot.value().reports) {
        if (isCancelled(cancel))
            return cancelled();
        if (!safeRelativePath(report))
            return failure(QStringLiteral("The catalog lists a report outside the library (%1).").arg(report));
        const QString from = layout.absolute(report);
        if (!QFileInfo::exists(from)) {
            manifest.missingReports << report;  // Diagnostics only; said, not fatal.
        } else {
            ManifestFile f{report, QStringLiteral("report"), 0, {}};
            if (auto st = copyChecked(from, QDir(staging).filePath(report), {}, QStringLiteral("The report %1").arg(report),
                                      cancel, &f.sha256, &f.size);
                !st)
                return st.error();
            manifest.files << f;
        }
        if (progress)
            progress(++done, total);
    }

    // The copy is a plain rollback-journal database, so it can be read (and
    // verified) without write access or side files.
    {
        const QString name = QStringLiteral("mbl-backup-%1").arg(shortId());
        bool normalized = false;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
            db.setDatabaseName(catalogCopy);
            if (db.open()) {
                QSqlQuery q(db);
                normalized = q.exec(QStringLiteral("PRAGMA journal_mode = DELETE"));
                q.finish();
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(name);
        if (!normalized)
            return Error{ErrorCode::Io, QStringLiteral("The catalog copy could not be prepared.")};
    }
    const QFileInfo catalogInfo(catalogCopy);
    const auto catalogSha = sha256OfFile(catalogCopy, cancel);
    if (!catalogSha)
        return isCancelled(cancel) ? cancelled() : Error{ErrorCode::Io, QStringLiteral("The catalog copy cannot be read.")};
    manifest.catalog = ManifestFile{kCatalog, QStringLiteral("catalog"), catalogInfo.size(), *catalogSha};
    if (auto st = writeManifest(staging, manifest); !st)
        return st.error();

    // Everything is read back before the backup is given its name.
    auto verified = verifyBackup(staging, cancel);
    if (!verified)
        return verified.error();
    const QString finalFolder = freeName(parent, base);
    QString error;
    if (!commitMove(staging, finalFolder, &error))
        return Error{ErrorCode::Io, QStringLiteral("The backup could not be given its name: %1").arg(error)};
    committed = true;
    BackupInfo info = verified.value();
    info.folder = finalFolder;
    return info;
}

Result<BackupInfo> verifyBackup(const QString& backupFolder, const std::atomic_bool* cancel)
{
    auto manifest = readManifest(backupFolder);
    if (!manifest)
        return manifest.error();
    const Manifest& m = manifest.value();
    QList<ManifestFile> all = m.files;
    all.prepend(m.catalog);
    for (const ManifestFile& f : all) {
        if (isCancelled(cancel))
            return cancelled();
        const QString path = QDir(backupFolder).filePath(f.path);
        const QFileInfo info(path);
        if (!info.isFile())
            return failure(QStringLiteral("The backup is missing %1.").arg(f.path));
        if (info.size() != f.size)
            return failure(QStringLiteral("%1 in the backup has another size than when it was saved.").arg(f.path));
        const auto sha = sha256OfFile(path, cancel);
        if (!sha)
            return isCancelled(cancel) ? cancelled() : failure(QStringLiteral("%1 in the backup cannot be read.").arg(f.path));
        if (sha->compare(f.sha256, Qt::CaseInsensitive) != 0)
            return failure(QStringLiteral("%1 in the backup has changed since it was saved.").arg(f.path));
    }
    if (auto version = checkCatalogFile(QDir(backupFolder).filePath(kCatalog), m); !version)
        return version.error();
    return infoOf(backupFolder, m);
}

Result<RestoreInfo> restoreBackup(const QString& backupFolder, const QString& targetFolder,
                                  const QStringList& librariesInUse, const std::atomic_bool* cancel,
                                  const BackupProgress& progress)
{
    const QFileInfo target(targetFolder);
    if (targetFolder.trimmed().isEmpty() || !target.isAbsolute())
        return failure(QStringLiteral("Choose a new folder for the restored library."));
    const bool targetExists = target.exists();
    if (targetExists && (!target.isDir() || !QDir(targetFolder).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)))
        return failure(QStringLiteral("%1 is not empty; restore into a new or empty folder.")
                           .arg(QDir::toNativeSeparators(targetFolder)));
    const QString parent = target.absolutePath();
    if (!QFileInfo(parent).isDir())
        return failure(QStringLiteral("The folder %1 does not exist.").arg(QDir::toNativeSeparators(parent)));
    if (isInsideFolder(targetFolder, backupFolder))
        return failure(QStringLiteral("The library cannot be restored inside the backup."));
    for (const QString& root : librariesInUse) {
        if (isInsideFolder(targetFolder, root) || isInsideFolder(root, targetFolder))
            return failure(QStringLiteral("The library cannot be restored inside or around the library in use (%1).")
                               .arg(QDir::toNativeSeparators(root)));
    }

    auto verified = verifyBackup(backupFolder, cancel);  // Nothing is written if this fails.
    if (!verified)
        return verified.error();
    auto manifest = readManifest(backupFolder);
    if (!manifest)
        return manifest.error();
    const Manifest& m = manifest.value();

    const QString staging = QDir(parent).filePath(QStringLiteral(".%1-%2.restoring").arg(target.fileName(), shortId()));
    if (!QDir().mkpath(staging))
        return Error{ErrorCode::Io, QStringLiteral("The restored library could not be prepared in %1.")
                                        .arg(QDir::toNativeSeparators(parent))};
    bool committed = false;
    const auto removeStaging = qScopeGuard([&] {
        if (!committed)
            QDir(staging).removeRecursively();
    });

    QList<ManifestFile> all = m.files;
    all.prepend(m.catalog);
    int done = 0;
    for (const ManifestFile& f : all) {
        if (isCancelled(cancel))
            return cancelled();
        if (auto st = copyChecked(QDir(backupFolder).filePath(f.path), QDir(staging).filePath(f.path), f.sha256,
                                  QStringLiteral("%1 in the backup").arg(f.path), cancel);
            !st)
            return st.error();
        if (progress)
            progress(++done, int(all.size()));
    }
    if (isCancelled(cancel))
        return cancelled();

    // An empty target folder is replaced by the restored one.
    if (targetExists && !QDir().rmdir(targetFolder))
        return Error{ErrorCode::Io, QStringLiteral("%1 could not be replaced.").arg(QDir::toNativeSeparators(targetFolder))};
    QString error;
    if (!commitMove(staging, targetFolder, &error)) {
        if (targetExists)
            QDir().mkdir(targetFolder);  // As it was.
        return Error{ErrorCode::Io, QStringLiteral("The restored library could not be put in place: %1").arg(error)};
    }
    committed = true;

    // Open it as the application will: the lock, migrations for an older
    // catalog, and the integrity check.
    RestoreInfo info;
    info.libraryFolder = QDir::cleanPath(target.absoluteFilePath());
    info.backup = verified.value();
    auto opened = catalog::Library::open(info.libraryFolder);
    if (!opened)
        return Error{opened.error().code, QStringLiteral("The library was restored to %1 but could not be opened: %2")
                                              .arg(QDir::toNativeSeparators(info.libraryFolder), opened.error().message)};
    auto integrity = opened.value()
                         ->run([](QSqlDatabase& db) -> Result<QString> {
                             QSqlQuery q(db);
                             if (!q.exec(QStringLiteral("PRAGMA integrity_check")) || !q.next())
                                 return Error{ErrorCode::Database, QStringLiteral("integrity check failed to run")};
                             return q.value(0).toString();
                         })
                         .result();
    if (!integrity || integrity.value() != QLatin1String("ok"))
        return Error{ErrorCode::Database, QStringLiteral("The restored catalog did not pass its integrity check.")};
    // A copy is never written again without the user asking.
    auto closed = opened.value()->run([](QSqlDatabase& db) { return catalog::closeExportsAfterRestore(db); }).result();
    if (!closed)
        return closed.error();
    info.exportsClosed = closed.value();
    info.schemaVersion = opened.value()->schemaVersion();
    return info;
}

} // namespace mbl::storage
