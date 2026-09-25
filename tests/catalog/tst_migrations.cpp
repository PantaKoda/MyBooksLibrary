// A2 migrations: versioned, transactional, never destructive.
#include "catalog/library.h"
#include "catalog/migrations.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace mbl::catalog;
using mbl::domain::ErrorCode;

namespace {

// Runs `body(db)` on a private connection in the test thread.
template <typename Body>
void withConnection(const QString& path, Body body)
{
    const QString name = QStringLiteral("raw-%1").arg(quintptr(&body));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        QVERIFY(db.open());
        body(db);
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
}

bool tableExists(QSqlDatabase& db, const QString& table)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT count(*) FROM sqlite_master WHERE name = ?"));
    q.addBindValue(table);
    return q.exec() && q.next() && q.value(0).toInt() == 1;
}

} // namespace

class TestMigrations : public QObject {
    Q_OBJECT

private slots:
    void freshLibraryReachesLatest();
    void reopeningIsIdempotent();
    void newerSchemaIsRefusedUnchanged();
    void failedMigrationRollsBack();
    void nonConsecutiveListIsRejected();
};

void TestMigrations::freshLibraryReachesLatest()
{
    QTemporaryDir dir;
    auto library = Library::open(dir.path());
    QVERIFY2(library, library ? "" : qPrintable(library.error().message));
    QCOMPARE(library.value()->schemaVersion(), latestSchemaVersion());
    QVERIFY(latestSchemaVersion() >= 1);
}

void TestMigrations::reopeningIsIdempotent()
{
    QTemporaryDir dir;
    { QVERIFY(Library::open(dir.path())); }
    auto again = Library::open(dir.path());
    QVERIFY2(again, again ? "" : qPrintable(again.error().message));
    QCOMPARE(again.value()->schemaVersion(), latestSchemaVersion());
}

void TestMigrations::newerSchemaIsRefusedUnchanged()
{
    QTemporaryDir dir;
    { QVERIFY(Library::open(dir.path())); }
    const QString path = dir.filePath(QLatin1StringView(Library::kCatalogFileName));
    withConnection(path, [](QSqlDatabase& db) {
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 999")));
    });

    auto refused = Library::open(dir.path());
    QVERIFY(!refused);
    QCOMPARE(refused.error().code, ErrorCode::SchemaTooNew);
    QVERIFY(refused.error().message.contains(QStringLiteral("999")));

    withConnection(path, [](QSqlDatabase& db) {
        QCOMPARE(schemaVersion(db), 999);
        QVERIFY(tableExists(db, QStringLiteral("books")));
    });
}

void TestMigrations::failedMigrationRollsBack()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("m.sqlite"));
    const QList<Migration> good = {
        {1, QStringLiteral("first"), {QStringLiteral("CREATE TABLE t1 (x INTEGER)"),
                                      QStringLiteral("INSERT INTO t1 VALUES (1)")}},
    };
    const QList<Migration> bad = good + QList<Migration>{
        {2, QStringLiteral("second"), {QStringLiteral("CREATE TABLE t2 (y INTEGER)"),
                                       QStringLiteral("DROP TABLE t1"),
                                       QStringLiteral("THIS IS NOT SQL")}},
    };
    withConnection(path, [&](QSqlDatabase& db) {
        QVERIFY(migrate(db, good));
        QCOMPARE(schemaVersion(db), 1);
        auto failed = migrate(db, bad);
        QVERIFY(!failed);
        QCOMPARE(failed.error().code, ErrorCode::Database);
        QVERIFY(failed.error().message.contains(QStringLiteral("schema version 1")));
        QCOMPARE(schemaVersion(db), 1);
        QVERIFY(tableExists(db, QStringLiteral("t1")));   // DROP was rolled back.
        QVERIFY(!tableExists(db, QStringLiteral("t2")));  // CREATE was rolled back.
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("SELECT x FROM t1")) && q.next());
        QCOMPARE(q.value(0).toInt(), 1);
    });
}

void TestMigrations::nonConsecutiveListIsRejected()
{
    QTemporaryDir dir;
    withConnection(dir.filePath(QStringLiteral("n.sqlite")), [](QSqlDatabase& db) {
        const QList<Migration> gap = {{2, QStringLiteral("gap"), {QStringLiteral("CREATE TABLE t (x)")}}};
        auto result = migrate(db, gap);
        QVERIFY(!result);
        QCOMPARE(schemaVersion(db), 0);
    });
}

QTEST_GUILESS_MAIN(TestMigrations)
#include "tst_migrations.moc"
