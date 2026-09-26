// Presentation: the library session behind the main window. Opens the
// library (lock, migrations, import recovery) and imports files on a worker
// thread, and keeps the GUI-owned book list current. QML only calls its
// commands and reads its properties; no SQL, file or SDK work happens in QML
// or on the GUI thread.
#pragma once

#include "presentation/booklistmodel.h"

#include <QMutex>
#include <QQmlEngine>
#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QUrl>

#include <atomic>
#include <memory>

namespace mbl::catalog {
class Library;
}
namespace mbl::storage {
class ImportService;
}

namespace mbl::presentation {

class LibraryController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The application creates the library session.")
    Q_PROPERTY(bool opening READ opening NOTIFY stateChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    Q_PROPERTY(bool failed READ failed NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool importing READ importing NOTIFY importProgressChanged)
    Q_PROPERTY(int importTotal READ importTotal NOTIFY importProgressChanged)
    Q_PROPERTY(int importDone READ importDone NOTIFY importProgressChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY importProgressChanged)
    Q_PROPERTY(double fileProgress READ fileProgress NOTIFY importProgressChanged)  // 0..1
    Q_PROPERTY(QString libraryPath READ libraryPath NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QStringList problems READ problems NOTIFY problemsChanged)
    Q_PROPERTY(mbl::presentation::BookListModel* books READ books CONSTANT)

public:
    enum class State { Closed, Opening, Ready, Failed };

    struct BatchSummary {
        int imported = 0;
        int duplicates = 0;
        int duplicatesInTrash = 0;
        int failed = 0;
        int cancelled = 0;
    };

    explicit LibraryController(QObject* parent = nullptr);
    // Requests cancellation and waits for the worker; the window's closing
    // flow waits for !busy first, so this does not block in normal use.
    ~LibraryController() override;

    // Opens (and creates if needed) the library at `rootDir` on the worker
    // thread, then recovers interrupted imports and loads the book list.
    Q_INVOKABLE void open(const QString& rootDir);
    // Queues files for import. Files queued while the library is opening are
    // imported once it is ready, or reported as not imported if opening fails.
    // Ignored when the library is closed or failed.
    Q_INVOKABLE void importUrls(const QList<QUrl>& urls);
    void importFiles(const QStringList& localPaths);
    // Cancels the file being copied and drops queued files.
    Q_INVOKABLE void cancelImports();
    Q_INVOKABLE void refresh();

    State state() const { return m_state; }
    bool opening() const { return m_state == State::Opening; }
    bool ready() const { return m_state == State::Ready; }
    bool failed() const { return m_state == State::Failed; }
    bool busy() const { return opening() || importing() || m_refreshesPending > 0; }
    bool importing() const { return m_importTotal > 0; }
    int importTotal() const { return m_importTotal; }
    int importDone() const { return m_importDone; }
    QString currentFile() const { return m_currentFile; }
    double fileProgress() const { return m_fileProgress; }
    QString libraryPath() const { return m_libraryPath; }
    QString statusText() const { return m_statusText; }
    QStringList problems() const { return m_problems; }
    BookListModel* books() { return &m_books; }
    BatchSummary lastBatch() const { return m_lastBatch; }

signals:
    void stateChanged();
    void busyChanged();
    void importProgressChanged();
    void statusTextChanged();
    void problemsChanged();
    void importsFinished();  // A batch ended; lastBatch() holds its counts.
    void booksRefreshed();

private:
    struct FileResult;
    void startBatch();
    void onOpened(std::shared_ptr<catalog::Library> library, std::shared_ptr<storage::ImportService> importer,
                  QString error, QString recoveryText);
    void onFileStarted(const QString& path);
    void onFileProgress(qint64 done, qint64 total);
    void onFileFinished(const FileResult& result);
    void onBatchFinished();
    void finishBatch();  // Publishes the batch summary and clears the import counters.
    void setState(State state);
    void setStatus(const QString& text);
    void setBusyFlags(const std::function<void()>& change);

    BookListModel m_books;
    QThreadPool m_pool;  // One worker: opening, recovery and file imports.
    std::shared_ptr<catalog::Library> m_library;
    std::shared_ptr<storage::ImportService> m_importer;
    std::shared_ptr<std::atomic_bool> m_cancel = std::make_shared<std::atomic_bool>(false);

    QMutex m_queueMutex;  // Guards m_queue (GUI thread appends, worker takes).
    QStringList m_queue;
    bool m_batchRunning = false;

    State m_state = State::Closed;
    QString m_libraryPath;
    QString m_statusText;
    QStringList m_problems;
    int m_importTotal = 0;
    int m_importDone = 0;
    QString m_currentFile;
    double m_fileProgress = 0;
    int m_refreshesPending = 0;
    BatchSummary m_batch;
    BatchSummary m_lastBatch;
};

} // namespace mbl::presentation
