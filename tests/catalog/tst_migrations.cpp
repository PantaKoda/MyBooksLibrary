// A2 migrations: versioned, transactional, never destructive.
#include "catalog/library.h"
#include "catalog/migrations.h"

#include <QSqlDatabase>
#include <QFile>
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
    const QString mode = library.value()->run([](QSqlDatabase& db) {
        QSqlQuery q(db);
        q.exec(QStringLiteral("PRAGMA journal_mode"));
        q.next();
        return q.value(0).toString();
    }).result();
    QCOMPARE(mode, QStringLiteral("wal"));  // Enabled for supported catalogs.
}

void TestMigrations::reopeningIsIdempotent()
{
    QTemporaryDir dir;
    { QVERIFY(Library::open(dir.path())); }
    auto again = Library::open(dir.path());
    QVERIFY2(again, again ? "" : qPrintable(again.error().message));
    QCOMPARE(again.value()->schemaVersion(), latestSchemaVersion());
}

// Starts from a closed catalog in DELETE journal mode, as a newer
// application might leave it, and checks that refusal writes nothing.
void TestMigrations::newerSchemaIsRefusedUnchanged()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QLatin1StringView(Library::kCatalogFileName));
    withConnection(path, [](QSqlDatabase& db) {
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("PRAGMA journal_mode = DELETE")));
        QVERIFY(q.exec(QStringLiteral("CREATE TABLE books (id TEXT PRIMARY KEY, future_column TEXT)")));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO books VALUES ('b1', 'from the future')")));
        QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 999")));
    });
    const auto fileBytes = [&] {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    };
    const QByteArray before = fileBytes();
    QVERIFY(!before.isEmpty());

    auto refused = Library::open(dir.path());
    QVERIFY(!refused);
    QCOMPARE(refused.error().code, ErrorCode::SchemaTooNew);
    QVERIFY(refused.error().message.contains(QStringLiteral("999")));

    QCOMPARE(fileBytes(), before);  // Byte-identical: no journal-mode or schema change.
    QVERIFY(!QFile::exists(path + QStringLiteral("-wal")));
    QVERIFY(!QFile::exists(path + QStringLiteral("-shm")));
    withConnection(path, [](QSqlDatabase& db) {
        QCOMPARE(schemaVersion(db), 999);
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("PRAGMA journal_mode")) && q.next());
        QCOMPARE(q.value(0).toString(), QStringLiteral("delete"));
        QVERIFY(q.exec(QStringLiteral("SELECT future_column FROM books")) && q.next());
        QCOMPARE(q.value(0).toString(), QStringLiteral("from the future"));
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
