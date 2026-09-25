// Infrastructure: what the SQLite behind Qt's QSQLITE driver can actually do.
// The SQLite version string alone does not prove FTS5 is compiled in, so the
// probe creates and queries a temporary FTS5 table through the real driver.
#pragma once

#include <QString>
#include <QStringList>

namespace mbl::infrastructure {

struct SqliteCapabilities {
    bool driverAvailable = false;      // QSqlDatabase::isDriverAvailable("QSQLITE").
    QString sqliteVersion;             // SELECT sqlite_version().
    QStringList compileOptions;        // PRAGMA compile_options (diagnostics).
    bool fts5 = false;                 // Temporary FTS5 table created, filled and matched.
    bool fts5Bm25 = false;             // bm25() ranking function usable in ORDER BY.
    bool fts5RemoveDiacritics = false; // unicode61 remove_diacritics 2: "uber" matches "Über".
    bool fts5Prefix = false;           // Prefix query "prog*" matches "Programming".
    QString error;                     // First failure, empty when every check passed.

    bool searchReady() const { return driverAvailable && fts5 && fts5Bm25; }
};

// Opens and closes its own private in-memory connection on the calling thread.
// Safe to call from any thread that owns no other connection with the same name.
SqliteCapabilities probeSqliteCapabilities();

} // namespace mbl::infrastructure
