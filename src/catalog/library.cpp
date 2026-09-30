#include "catalog/library.h"

#include "catalog/migrations.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>

namespace mbl::catalog {

using domain::ErrorCode;
using domain::makeError;

domain::Result<std::unique_ptr<Library>> Library::open(const QString& rootDir)
{
    const QString root = QDir::cleanPath(QDir(rootDir).absolutePath());
    // A backup looks like a library, but opening it would change it: WAL mode
    // is stored in the catalog file, and recovery and jobs write into the
    // folder. Its catalog would then no longer match its manifest, and Restore
    // would refuse it. Nothing is created, locked or opened.
    if (QFileInfo::exists(QDir(root).filePath(QLatin1StringView(kBackupManifestFileName)))) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("%1 is a MyBooksLibrary backup, not a library. Opening it would change the "
                                        "backup, so it is not opened. To use it, choose Backup → Restore a backup…, "
                                        "which makes a new library from it.")
                             .arg(QDir::toNativeSeparators(root)));
    }
    if (!QDir().mkpath(root))
        return makeError(ErrorCode::Io, QStringLiteral("Cannot create the library folder %1.").arg(root));

    std::unique_ptr<Library> library(new Library);
    library->m_root = root;

    // Lock staleness is decided only by whether the owning process is alive.
    library->m_lock = std::make_unique<QLockFile>(QDir(root).filePath(QLatin1StringView(kLockFileName)));
    library->m_lock->setStaleLockTime(0);
    if (!library->m_lock->tryLock(0)) {
        qint64 pid = 0;
        QString host, app;
        library->m_lock->getLockInfo(&pid, &host, &app);
        const auto reason = library->m_lock->error();
        if (reason == QLockFile::LockFailedError) {
            return makeError(ErrorCode::LibraryLocked,
                             QStringLiteral("The library %1 is already open in another MyBooksLibrary process "
                                            "(process %2 on %3).")
                                 .arg(root)
                                 .arg(pid)
                                 .arg(host));
        }
        return makeError(ErrorCode::Io, QStringLiteral("Cannot create the library lock in %1.").arg(root));
    }

    QString error;
    library->m_executor =
        infrastructure::DatabaseExecutor::open(QDir(root).filePath(QLatin1StringView(kCatalogFileName)), &error);
    if (!library->m_executor)
        return makeError(ErrorCode::Database, error);

    struct Migrated {
        domain::Status status = domain::Done{};
        int version = 0;
    };
    const Migrated migrated = library->m_executor
                                  ->post([](QSqlDatabase& db) {
                                      Migrated m;
                                      // Refuse an unsupported catalog before
                                      // any persistent change (WAL is stored
                                      // in the file).
                                      m.status = checkCompatible(db);
                                      if (!m.status)
                                          return m;
                                      QSqlQuery wal(db);
                                      if (!wal.exec(QStringLiteral("PRAGMA journal_mode = WAL")) || !wal.next()
                                          || wal.value(0).toString().compare(QLatin1String("wal"), Qt::CaseInsensitive) != 0) {
                                          m.status = makeError(ErrorCode::Database,
                                                               QStringLiteral("Cannot enable WAL journaling: %1")
                                                                   .arg(wal.lastError().text()));
                                          return m;
                                      }
                                      wal.finish();
                                      m.status = migrate(db);
                                      m.version = mbl::catalog::schemaVersion(db);
                                      return m;
                                  })
                                  .result();
    if (!migrated.status)
        return migrated.status.error();
    library->m_schemaVersion = migrated.version;
    return library;
}

Library::~Library()
{
    m_executor.reset();  // Close the catalog before releasing the lock.
    if (m_lock)
        m_lock->unlock();
}

} // namespace mbl::catalog
