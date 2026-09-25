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

    // Creates `rootDir` if needed, takes the writer lock, opens the catalog on
    // its own thread and applies pending migrations. Blocks until done; call
    // from a non-GUI thread or before the window is shown.
    static domain::Result<std::unique_ptr<Library>> open(const QString& rootDir);

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
