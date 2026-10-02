// An open library: the single-process writer lock, the database thread with
// its connection, and a migrated catalog. The composition root owns one.
#pragma once

#include "domain/result.h"
#include "infrastructure/databaseexecutor.h"

#include <QLockFile>
#include <QString>

#include <memory>

namespace mbl::catalog {

class Library {
public:
    static constexpr const char* kCatalogFileName = "library.sqlite";
    static constexpr const char* kLockFileName = "library.lock";
    // The manifest that marks a backup folder (storage/backup.h). A backup has
    // a library's layout, but it is never opened as one.
    static constexpr const char* kBackupManifestFileName = "backup.json";

    // What open() does with a folder that holds no library yet.
    enum class OpenMode {
        // Make one there: the default folder on first start, --library and
        // MYBOOKSLIBRARY_ROOT (which scripts rely on).
        CreateIfMissing,
        // Refuse it: a library the user chose in the app (Open library…, a
        // restored library). A missing or wrong folder must never silently
        // become a new, empty library.
        ExistingOnly,
    };

    // Creates `rootDir` if needed (CreateIfMissing), takes the writer lock,
    // opens the catalog on its own thread and applies pending migrations.
    // Blocks until done; call from a non-GUI thread or before the window is
    // shown. A backup folder is refused (InvalidArgument) before anything in
    // it is touched: opening it would change its catalog, and the backup
    // would no longer verify. With ExistingOnly, so is anything that is not an
    // existing library (see checkExisting()).
    static domain::Result<std::unique_ptr<Library>> open(const QString& rootDir,
                                                         OpenMode mode = OpenMode::CreateIfMissing);

    // Whether `rootDir` is an existing library, without creating or changing
    // anything: a folder holding the catalog, and not a backup. Fails with
    // NotFound (no such folder, or no catalog in it) or InvalidArgument (not
    // a folder, or a backup), with the reason in plain words.
    static domain::Status checkExisting(const QString& rootDir);

    // Closes the connection (after queued work) and releases the lock.
    ~Library();

    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    QString rootDir() const { return m_root; }
    int schemaVersion() const { return m_schemaVersion; }

    // Runs `task(QSqlDatabase&)` on the database thread.
    template <typename Task>
    auto run(Task task) { return m_executor->post(std::move(task)); }

    infrastructure::DatabaseExecutor& executor() { return *m_executor; }

private:
    Library() = default;

    QString m_root;
    std::unique_ptr<QLockFile> m_lock;
    std::unique_ptr<infrastructure::DatabaseExecutor> m_executor;
    int m_schemaVersion = 0;
};

} // namespace mbl::catalog
