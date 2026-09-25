// The executor owns its connection on one dedicated thread.
#include "infrastructure/databaseexecutor.h"

#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <stdexcept>

using mbl::infrastructure::DatabaseExecutor;

class TestDatabaseExecutor : public QObject {
    Q_OBJECT

private slots:
    void runsOnItsOwnThreadInOrder();
    void enablesForeignKeysAndWal();
    void deliversExceptions();
    void reportsOpenFailure();
    void closesConnectionOnDestruction();
};

void TestDatabaseExecutor::runsOnItsOwnThreadInOrder()
{
    QTemporaryDir dir;
    QString error;
    auto executor = DatabaseExecutor::open(dir.filePath(QStringLiteral("a.sqlite")), &error);
    QVERIFY2(executor, qPrintable(error));

    QList<QFuture<QThread*>> threads;
    QList<QFuture<int>> order;
    auto counter = std::make_shared<int>(0);
    for (int i = 0; i < 20; ++i) {
        threads << executor->post([](QSqlDatabase&) { return QThread::currentThread(); });
        order << executor->post([counter](QSqlDatabase&) { return (*counter)++; });
    }
    for (int i = 0; i < 20; ++i) {
        QCOMPARE(threads[i].result(), executor->thread());
        QVERIFY(threads[i].result() != QThread::currentThread());
        QCOMPARE(order[i].result(), i);
    }
}

void TestDatabaseExecutor::enablesForeignKeysAndWal()
{
    QTemporaryDir dir;
    QString error;
    auto executor = DatabaseExecutor::open(dir.filePath(QStringLiteral("b.sqlite")), &error);
    QVERIFY2(executor, qPrintable(error));
    const auto pragmas = executor->post([](QSqlDatabase& db) {
        QSqlQuery q(db);
        q.exec(QStringLiteral("PRAGMA foreign_keys"));
        q.next();
        const int fk = q.value(0).toInt();
        q.exec(QStringLiteral("PRAGMA journal_mode"));
        q.next();
        return qMakePair(fk, q.value(0).toString());
    }).result();
    QCOMPARE(pragmas.first, 1);
    QCOMPARE(pragmas.second, QStringLiteral("wal"));
}

void TestDatabaseExecutor::deliversExceptions()
{
    QTemporaryDir dir;
    QString error;
    auto executor = DatabaseExecutor::open(dir.filePath(QStringLiteral("c.sqlite")), &error);
    QVERIFY(executor);
    auto failing = executor->post([](QSqlDatabase&) -> int { throw std::runtime_error("boom"); });
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, failing.waitForFinished());
    // The thread keeps serving after a failed task.
    QCOMPARE(executor->post([](QSqlDatabase&) { return 7; }).result(), 7);
}

void TestDatabaseExecutor::reportsOpenFailure()
{
    QTemporaryDir dir;
    QString error;
    // A directory cannot be opened as a database file.
    auto executor = DatabaseExecutor::open(dir.path(), &error);
    QVERIFY(!executor);
    QVERIFY(!error.isEmpty());
}

void TestDatabaseExecutor::closesConnectionOnDestruction()
{
    QTemporaryDir dir;
    QString error;
    QString name;
    {
        auto executor = DatabaseExecutor::open(dir.filePath(QStringLiteral("d.sqlite")), &error);
        QVERIFY(executor);
        name = executor->connectionName();
        QVERIFY(executor->post([name](QSqlDatabase&) { return QSqlDatabase::contains(name); }).result());
    }
    QVERIFY(!QSqlDatabase::contains(name));
}

QTEST_GUILESS_MAIN(TestDatabaseExecutor)
#include "tst_databaseexecutor.moc"
