#include "infrastructure/databaseexecutor.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace mbl::infrastructure {

DatabaseExecutor::DatabaseExecutor()
    : m_thread(new QThread),
      m_context(new QObject),
      m_connectionName(QStringLiteral("mbl-catalog-") + QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    m_thread->setObjectName(QStringLiteral("mbl-database"));
    m_context->moveToThread(m_thread);
    m_thread->start();
}

std::unique_ptr<DatabaseExecutor> DatabaseExecutor::open(const QString& databasePath, QString* error)
{
    std::unique_ptr<DatabaseExecutor> executor(new DatabaseExecutor);
    const QString name = executor->m_connectionName;

    QFuture<QString> opened = executor->post([name, databasePath](QSqlDatabase&) -> QString {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(databasePath);
        if (!db.open())
            return QStringLiteral("Cannot open %1: %2").arg(databasePath, db.lastError().text());
        QSqlQuery query(db);
        // Connection-scoped settings only. Anything persisted in the file
        // (such as journal_mode) is the owner's decision after it has
        // checked that it supports the file (see catalog::Library::open).
        const char* pragmas[] = {
            "PRAGMA foreign_keys = ON",
            "PRAGMA synchronous = NORMAL",
            "PRAGMA busy_timeout = 5000",
        };
        for (const char* pragma : pragmas) {
            if (!query.exec(QString::fromLatin1(pragma)))
                return QStringLiteral("%1: %2").arg(QLatin1StringView(pragma), query.lastError().text());
        }
        if (!query.exec(QStringLiteral("PRAGMA foreign_keys")) || !query.next() || query.value(0).toInt() != 1)
            return QStringLiteral("SQLite foreign key enforcement could not be enabled");
        return {};
    });
    const QString failure = opened.result();
    if (!failure.isEmpty()) {
        if (error)
            *error = failure;
        return nullptr;  // The destructor closes and removes the connection.
    }
    return executor;
}

DatabaseExecutor::~DatabaseExecutor()
{
    const QString name = m_connectionName;
    QObject* context = m_context;
    QThread* thread = m_thread;
    QMetaObject::invokeMethod(
        context,
        [name, context, thread] {
            if (QSqlDatabase::contains(name)) {
                {
                    QSqlDatabase db = QSqlDatabase::database(name, false);
                    db.close();
                }
                QSqlDatabase::removeDatabase(name);
            }
            delete context;
            thread->quit();
        },
        Qt::QueuedConnection);
    m_thread->wait();
    delete m_thread;
}

} // namespace mbl::infrastructure
