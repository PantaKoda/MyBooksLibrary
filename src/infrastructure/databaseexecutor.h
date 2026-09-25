// One thread that owns one SQLite connection. All queries and transactions
// for a library run here. Callers post tasks and get a QFuture of a copied
// value; QSqlQuery objects and the connection never leave the thread.
#pragma once

#include <QFuture>
#include <QMetaObject>
#include <QObject>
#include <QPromise>
#include <QSqlDatabase>
#include <QString>
#include <QThread>

#include <exception>
#include <memory>
#include <type_traits>

namespace mbl::infrastructure {

class DatabaseExecutor {
public:
    // Starts the thread and opens `databasePath` on it with foreign keys on,
    // WAL journaling and a busy timeout. Returns nullptr and sets `error`
    // when the connection cannot be opened.
    static std::unique_ptr<DatabaseExecutor> open(const QString& databasePath, QString* error);

    // Closes the connection on its thread, then stops the thread. Queued tasks
    // run first. Blocks briefly; call only during library shutdown.
    ~DatabaseExecutor();

    DatabaseExecutor(const DatabaseExecutor&) = delete;
    DatabaseExecutor& operator=(const DatabaseExecutor&) = delete;

    // Runs `task(QSqlDatabase&)` on the database thread, in submission order.
    // An exception thrown by the task is delivered through the future.
    template <typename Task>
    auto post(Task task) -> QFuture<std::invoke_result_t<Task, QSqlDatabase&>>;

    QThread* thread() const { return m_thread; }
    QString connectionName() const { return m_connectionName; }

private:
    DatabaseExecutor();

    QThread* m_thread = nullptr;
    QObject* m_context = nullptr;  // Lives on m_thread; tasks are queued to it.
    QString m_connectionName;
};

template <typename Task>
auto DatabaseExecutor::post(Task task) -> QFuture<std::invoke_result_t<Task, QSqlDatabase&>>
{
    using T = std::invoke_result_t<Task, QSqlDatabase&>;
    auto promise = std::make_shared<QPromise<T>>();
    QFuture<T> future = promise->future();
    promise->start();
    const QString name = m_connectionName;
    QMetaObject::invokeMethod(
        m_context,
        [promise, name, task = std::move(task)]() mutable {
            try {
                QSqlDatabase db = QSqlDatabase::database(name, false);
                if constexpr (std::is_void_v<T>) {
                    task(db);
                } else {
                    promise->addResult(task(db));
                }
            } catch (...) {
                promise->setException(std::current_exception());
            }
            promise->finish();
        },
        Qt::QueuedConnection);
    return future;
}

} // namespace mbl::infrastructure
