// Presentation (M10): backing up the open library and restoring a backup,
// for the window's Back up… and Restore… dialogs. The work runs on this
// controller's own worker thread (storage::createBackup / restoreBackup);
// progress and the result come back through queued calls, so nothing blocks
// the GUI thread. A backup is consistent while imports and jobs run
// (docs/STORAGE.md). A restore always makes a new library, never inside or
// around the open one; it is opened in a new window, not in place of this one.
#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QThreadPool>
#include <QUrl>

#include <atomic>
#include <memory>

namespace mbl::catalog {
class Library;
}

namespace mbl::presentation {

class BackupController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The library session owns the backup session.")
    Q_PROPERTY(Operation operation READ operation NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool succeeded READ succeeded NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(int done READ done NOTIFY progressChanged)    // Files copied (0 while starting).
    Q_PROPERTY(int total READ total NOTIFY progressChanged)  // 0 when not known yet.
    // The new backup, or the restored library, once it succeeded.
    Q_PROPERTY(QString resultFolder READ resultFolder NOTIFY stateChanged)
    Q_PROPERTY(QUrl resultFolderUrl READ resultFolderUrl NOTIFY stateChanged)
    Q_PROPERTY(QString suggestedBackupFolder READ suggestedBackupFolder CONSTANT)
    Q_PROPERTY(QString suggestedRestoreFolder READ suggestedRestoreFolder NOTIFY stateChanged)

public:
    enum class Operation { None, Backup, Restore };
    Q_ENUM(Operation)

    explicit BackupController(QObject* parent = nullptr);
    // Cancels and waits for the worker: teardown only (the window closes
    // after busy() is false).
    ~BackupController() override;

    void setLibrary(std::shared_ptr<catalog::Library> library);

    // A folder (path or file URL) to create the backup in: it must exist and
    // be outside the library folder.
    Q_INVOKABLE void backUp(const QString& folder);
    // Restores the backup folder as a new library at `targetFolder` (new or
    // empty; never inside or around the open library).
    Q_INVOKABLE void restore(const QString& backupFolder, const QString& targetFolder);
    Q_INVOKABLE void cancel();
    // Teardown: cancels and waits for the worker (blocking; the window waits
    // for !running first, so this returns at once in normal use).
    void stop();
    Q_INVOKABLE void reset();  // Clears a finished result (not while running).
    // Starts MyBooksLibrary on the restored library in a new window. False
    // when there is no restored library or it could not be started.
    Q_INVOKABLE bool openRestoredLibrary();
    // A folder dialog's URL as a path to show and type in (native separators).
    Q_INVOKABLE QString localPath(const QUrl& url) const;
    // "<parent>/MyBooksLibrary restored <date>": a new folder in the chosen one.
    Q_INVOKABLE QString restoreFolderIn(const QUrl& parent) const;

    Operation operation() const { return m_operation; }
    bool running() const { return m_running; }
    bool succeeded() const { return m_succeeded; }
    QString statusText() const { return m_statusText; }
    int done() const { return m_done; }
    int total() const { return m_total; }
    QString resultFolder() const { return m_resultFolder; }
    QUrl resultFolderUrl() const;
    QString suggestedBackupFolder() const;
    QString suggestedRestoreFolder() const;

signals:
    void stateChanged();
    void progressChanged();
    void finished();  // A backup or restore ended (see succeeded and statusText).

private:
    struct Outcome {
        bool ok = false;
        bool cancelled = false;
        QString text;
        QString folder;
    };
    void start(Operation operation, const QString& text);
    void setProgress(int done, int total);
    void finish(const Outcome& outcome);
    static QString pathOf(const QString& pathOrUrl);

    std::shared_ptr<catalog::Library> m_library;
    QThreadPool m_pool;  // One worker: one backup or restore at a time.
    std::shared_ptr<std::atomic_bool> m_cancel = std::make_shared<std::atomic_bool>(false);
    Operation m_operation = Operation::None;
    bool m_running = false;
    bool m_succeeded = false;
    QString m_statusText;
    int m_done = 0;
    int m_total = 0;
    QString m_resultFolder;
};

} // namespace mbl::presentation
