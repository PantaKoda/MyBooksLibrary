// M01 feasibility gate: FTS5 through the QSQLITE driver that ships with Qt.
#include "infrastructure/sqlitecapabilities.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>

class TestSqliteCapabilities : public QObject {
    Q_OBJECT

private slots:
    void probeReportsFts5();
    void punctuationNeedsEscaping();
};

void TestSqliteCapabilities::probeReportsFts5()
{
    const auto caps = mbl::infrastructure::probeSqliteCapabilities();
    qInfo().noquote() << "sqlite" << caps.sqliteVersion << "options:" << caps.compileOptions.join(u' ');
    QVERIFY2(caps.driverAvailable, qPrintable(caps.error));
    QVERIFY(!caps.sqliteVersion.isEmpty());
    QVERIFY2(caps.fts5, qPrintable(caps.error));
    QVERIFY2(caps.fts5Bm25, qPrintable(caps.error));
    QVERIFY2(caps.fts5RemoveDiacritics, qPrintable(caps.error));
    QVERIFY2(caps.fts5Prefix, qPrintable(caps.error));
    QVERIFY(caps.searchReady());
    QVERIFY2(caps.error.isEmpty(), qPrintable(caps.error));
    // The probe must not leak its private connection.
    for (const QString& name : QSqlDatabase::connectionNames())
        QVERIFY2(!name.startsWith(u"mbl-sqlite-probe-"), qPrintable(name));
}

// Records baseline behaviour that A3 must handle (AGENTS.md section 7):
// raw user text is FTS5 query syntax, so "C++" is a syntax error, while the
// same text as a quoted FTS5 string is a valid phrase query.
void TestSqliteCapabilities::punctuationNeedsEscaping()
{
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("punct"));
        db.setDatabaseName(QStringLiteral(":memory:"));
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("CREATE VIRTUAL TABLE temp.t USING fts5(title)")));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO temp.t(title) VALUES ('C++ Primer'), ('HTTP/2 in Action')")));

        QSqlQuery raw(db);
        raw.prepare(QStringLiteral("SELECT rowid FROM temp.t WHERE t MATCH ?"));
        raw.addBindValue(QStringLiteral("C++"));
        QVERIFY2(!raw.exec(), "raw C++ unexpectedly accepted as FTS5 syntax");

        QSqlQuery quoted(db);
        quoted.prepare(QStringLiteral("SELECT rowid FROM temp.t WHERE t MATCH ?"));
        quoted.addBindValue(QStringLiteral("\"HTTP/2\""));
        QVERIFY(quoted.exec());
        QVERIFY(quoted.next());
        QCOMPARE(quoted.value(0).toInt(), 2);
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("punct"));
}

QTEST_GUILESS_MAIN(TestSqliteCapabilities)
#include "tst_sqlitecapabilities.moc"
