// A2 migrations: versioned, transactional, never destructive.
#include "catalog/catalog.h"
#include "catalog/jobs.h"
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
    void version1CatalogUpgradesWithDataIntact();
    void version2CatalogGainsJobs();
    void version3ContentsRunsLoadAfterUpgrade();
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
// A catalog created by the M02 release (schema 1) with a book upgrades in place.
void TestMigrations::version1CatalogUpgradesWithDataIntact()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QLatin1StringView(Library::kCatalogFileName));
    withConnection(path, [](QSqlDatabase& db) {
        QVERIFY(migrate(db, {catalogMigrations().first()}));
        QCOMPARE(schemaVersion(db), 1);
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO assets(id, sha256, byte_size, managed_path, created_at) VALUES "
            "('a1', '0000000000000000000000000000000000000000000000000000000000000001', 10, 'files/a1/source.pdf', 'x')")));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO books(id, asset_id, original_file_name, original_path, created_at, updated_at) VALUES "
            "('b1', 'a1', 'old.pdf', 'C:/old.pdf', 'x', 'x')")));
    });

    auto library = Library::open(dir.path());
    QVERIFY2(library, library ? "" : qPrintable(library.error().message));
    QCOMPARE(library.value()->schemaVersion(), latestSchemaVersion());
    QVERIFY(latestSchemaVersion() >= 2);
    const auto counts = library.value()->run([](QSqlDatabase& db) {
        QSqlQuery q(db);
        q.exec(QStringLiteral("SELECT (SELECT count(*) FROM books), (SELECT count(*) FROM import_operations)"));
        q.next();
        return qMakePair(q.value(0).toInt(), q.value(1).toInt());
    }).result();
    QCOMPARE(counts.first, 1);
    QCOMPARE(counts.second, 0);
}

// A catalog left by the M03 release (schema 2) gains the job queue and the
// metadata evidence tables; its books can be queued at once.
void TestMigrations::version2CatalogGainsJobs()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QLatin1StringView(Library::kCatalogFileName));
    withConnection(path, [](QSqlDatabase& db) {
        QVERIFY(migrate(db, catalogMigrations().mid(0, 2)));
        QCOMPARE(schemaVersion(db), 2);
        QVERIFY(!tableExists(db, QStringLiteral("jobs")));
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO assets(id, sha256, byte_size, managed_path, created_at) VALUES "
            "('a1', '0000000000000000000000000000000000000000000000000000000000000001', 10, 'files/a1/source.pdf', 'x')")));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO books(id, asset_id, original_file_name, original_path, created_at, updated_at) VALUES "
            "('5b1c3c1e-0d6a-4c7e-9a52-3f1d2b7e8a01', 'a1', 'old.pdf', 'C:/old.pdf', 'x', 'x')")));
    });

    auto library = Library::open(dir.path());
    QVERIFY2(library, library ? "" : qPrintable(library.error().message));
    QCOMPARE(library.value()->schemaVersion(), latestSchemaVersion());
    QVERIFY(latestSchemaVersion() >= 3);
    const auto queued = library.value()->run([](QSqlDatabase& db) {
        const bool tables = tableExists(db, QStringLiteral("jobs")) && tableExists(db, QStringLiteral("metadata_field_details"));
        const auto book = mbl::domain::BookId::fromString(QStringLiteral("5b1c3c1e-0d6a-4c7e-9a52-3f1d2b7e8a01"));
        auto job = enqueueJob(db, book, mbl::domain::JobKind::Metadata);
        return qMakePair(tables, job ? job.value().generation : -1);
    }).result();
    QVERIFY(queued.first);
    QCOMPARE(queued.second, 1);  // The first metadata request of the upgraded book.
}

// A contents run stored by the M04 release (schema 3) still loads after the
// upgrade: its entries get empty evidence, its run unknown parse status.
void TestMigrations::version3ContentsRunsLoadAfterUpgrade()
{
    const QString book = QStringLiteral("7c9e6679-7425-40de-944b-e07fc1f90ae7");
    const QString run = QStringLiteral("9b2f4e1a-58d3-4b6f-a1c2-3d4e5f607182");
    QTemporaryDir dir;
    const QString path = dir.filePath(QLatin1StringView(Library::kCatalogFileName));
    withConnection(path, [&](QSqlDatabase& db) {
        QVERIFY(migrate(db, catalogMigrations().mid(0, 3)));
        QCOMPARE(schemaVersion(db), 3);
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO assets(id, sha256, byte_size, page_count, managed_path, created_at) VALUES "
            "('a1', '0000000000000000000000000000000000000000000000000000000000000001', 10, 5, 'files/a1/source.pdf', 'x')")));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO books(id, asset_id, original_file_name, original_path, created_at, "
                                      "updated_at) VALUES ('%1', 'a1', 'old.pdf', 'C:/old.pdf', 'x', 'x')").arg(book)));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO toc_runs(id, book_id, generation, source_sha256, sdk_version, model_identity, options_json, "
            "outcome, created_at, plan_ready) VALUES ('%1', '%2', 1, "
            "'0000000000000000000000000000000000000000000000000000000000000001', '0.2.0', '', '{}', 'plan_ready', 'x', 1)")
                           .arg(run, book)));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO toc_entries(run_id, sdk_entry_id, entry_order, title, hierarchy, destination_state, "
            "destination_page, source_toc_page, in_export_plan) VALUES ('%1', 'e0', 0, 'Old chapter', 'root', "
            "'resolved', 0, 1, 1)").arg(run)));
        QVERIFY(q.exec(QStringLiteral("UPDATE books SET active_toc_run_id = '%1' WHERE id = '%2'").arg(run, book)));
    });

    auto library = Library::open(dir.path());
    QVERIFY2(library, library ? "" : qPrintable(library.error().message));
    QVERIFY(latestSchemaVersion() >= 4);
    auto details = library.value()->run([book](QSqlDatabase& db) {
        return bookDetails(db, mbl::domain::BookId::fromString(book));
    }).result();
    QVERIFY2(details, details ? "" : qPrintable(details.error().message));
    QVERIFY(details.value().toc);
    const mbl::domain::TocAnalysis& toc = *details.value().toc;
    QCOMPARE(toc.entries.size(), 1);
    QCOMPARE(toc.entries.first().title, QStringLiteral("Old chapter"));
    QCOMPARE(toc.entries.first().destinationPage, std::optional<int>(0));
    QVERIFY(toc.entries.first().evidence.sourcePages.isEmpty());
    QVERIFY(!toc.parseComplete);
    QVERIFY(toc.planBlockers.isEmpty());
    QVERIFY(toc.planJson.isEmpty());
}

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
