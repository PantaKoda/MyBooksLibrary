#include "infrastructure/sqlitecapabilities.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace mbl::infrastructure {

namespace {

const QString kDriver = QStringLiteral("QSQLITE");

// Runs `sql` and records the first error. Returns false on failure.
bool exec(QSqlQuery& query, const QString& sql, QString& error)
{
    if (query.exec(sql))
        return true;
    if (error.isEmpty())
        error = QStringLiteral("%1: %2").arg(sql, query.lastError().text());
    return false;
}

// Number of rows returned by a MATCH query, or -1 on error.
int matchCount(QSqlDatabase& db, const QString& ftsQuery, QString& error)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT rowid FROM temp.mbl_fts_probe WHERE mbl_fts_probe MATCH ?"));
    query.addBindValue(ftsQuery);
    if (!query.exec()) {
        if (error.isEmpty())
            error = QStringLiteral("MATCH %1: %2").arg(ftsQuery, query.lastError().text());
        return -1;
    }
    int rows = 0;
    while (query.next())
        ++rows;
    return rows;
}

void probe(QSqlDatabase& db, SqliteCapabilities& caps)
{
    QSqlQuery query(db);
    if (exec(query, QStringLiteral("SELECT sqlite_version()"), caps.error) && query.next())
        caps.sqliteVersion = query.value(0).toString();
    if (exec(query, QStringLiteral("PRAGMA compile_options"), caps.error)) {
        while (query.next())
            caps.compileOptions << query.value(0).toString();
    }

    if (!exec(query,
              QStringLiteral("CREATE VIRTUAL TABLE temp.mbl_fts_probe USING fts5("
                             "title, tokenize = 'unicode61 remove_diacritics 2')"),
              caps.error)) {
        // Retry without tokenizer options to tell "no FTS5" from "option unsupported".
        QString ignored;
        if (!exec(query, QStringLiteral("CREATE VIRTUAL TABLE temp.mbl_fts_probe USING fts5(title)"),
                  ignored))
            return;
    }

    const QStringList titles = {
        QStringLiteral("Programming with TCP/IP"),
        QStringLiteral("Über Straßen und Brücken"),
        QStringLiteral("C++ Primer"),
    };
    query.prepare(QStringLiteral("INSERT INTO temp.mbl_fts_probe(title) VALUES (?)"));
    for (const QString& title : titles) {
        query.addBindValue(title);
        if (!query.exec()) {
            if (caps.error.isEmpty())
                caps.error = QStringLiteral("INSERT: %1").arg(query.lastError().text());
            return;
        }
    }

    caps.fts5 = matchCount(db, QStringLiteral("tcp"), caps.error) == 1;
    if (!caps.fts5)
        return;

    QSqlQuery ranked(db);
    caps.fts5Bm25 = exec(ranked,
                         QStringLiteral("SELECT rowid, bm25(mbl_fts_probe) FROM temp.mbl_fts_probe "
                                        "WHERE mbl_fts_probe MATCH 'primer' "
                                        "ORDER BY bm25(mbl_fts_probe)"),
                         caps.error)
                    && ranked.next();
    caps.fts5RemoveDiacritics = matchCount(db, QStringLiteral("uber"), caps.error) == 1;
    caps.fts5Prefix = matchCount(db, QStringLiteral("prog*"), caps.error) == 1;
}

} // namespace

SqliteCapabilities probeSqliteCapabilities()
{
    SqliteCapabilities caps;
    caps.driverAvailable = QSqlDatabase::isDriverAvailable(kDriver);
    if (!caps.driverAvailable) {
        caps.error = QStringLiteral("Qt SQL driver QSQLITE is not available");
        return caps;
    }

    const QString connection =
        QStringLiteral("mbl-sqlite-probe-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(kDriver, connection);
        db.setDatabaseName(QStringLiteral(":memory:"));
        if (!db.open())
            caps.error = QStringLiteral("open: %1").arg(db.lastError().text());
        else
            probe(db, caps);
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return caps;
}

} // namespace mbl::infrastructure
