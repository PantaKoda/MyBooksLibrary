// Presentation: the library session behind the main window. Opens the
// library (lock, migrations, import and job recovery), imports files on a
// worker thread, runs metadata jobs through the ProcessingCoordinator, and
// keeps the GUI-owned book and job lists current. QML only calls its
// commands and reads its properties; no SQL, file or SDK work happens in QML
// or on the GUI thread.
#pragma once

#include "presentation/bookinspector.h"
#include "presentation/booklistmodel.h"
#include "presentation/joblistmodel.h"
#include "presentation/searchcontroller.h"
#include "reader/readercontroller.h"

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
namespace mbl::processing {
class ContentsAnalyzer;
class MetadataExtractor;
class ProcessingCoordinator;
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
    Q_PROPERTY(mbl::presentation::JobListModel* jobs READ jobs CONSTANT)
    Q_PROPERTY(mbl::presentation::BookInspector* inspector READ inspector CONSTANT)
    Q_PROPERTY(mbl::presentation::SearchController* search READ search CONSTANT)
    Q_PROPERTY(mbl::reader::ReaderController* reader READ reader CONSTANT)
    Q_PROPERTY(bool processingAvailable READ processingAvailable CONSTANT)
    Q_PROPERTY(bool ocrAvailable READ ocrAvailable CONSTANT)
    Q_PROPERTY(bool closing READ closing NOTIFY closingChanged)

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

    // Composition root, before open(): the SDK steps for metadata and
    // contents jobs, and whether OCR models were found (shown as a capability
    // note). Without an extractor, jobs are queued but not run; without an
    // analyzer, contents jobs fail as unsupported.
    void setProcessors(std::shared_ptr<processing::MetadataExtractor> extractor,
                       std::shared_ptr<processing::ContentsAnalyzer> analyzer, bool ocrAvailable);
    void setMetadataExtractor(std::shared_ptr<processing::MetadataExtractor> extractor, bool ocrAvailable)
    {
        setProcessors(std::move(extractor), nullptr, ocrAvailable);
    }

    // Opens (and creates if needed) the library at `rootDir` on the worker
    // thread, then recovers interrupted imports and jobs, loads the book
    // list and starts processing.
    Q_INVOKABLE void open(const QString& rootDir);
    // Queues files for import. Files queued while the library is opening are
    // imported once it is ready, or reported as not imported if opening fails.
    // Ignored when the library is closed or failed.
    Q_INVOKABLE void importUrls(const QList<QUrl>& urls);
    void importFiles(const QStringList& localPaths);
    // Cancels the file being copied and drops queued files.
    Q_INVOKABLE void cancelImports();
    // Reloads books and jobs. Coalesced: at most one reload runs, and calls
    // made meanwhile schedule a single further one.
    Q_INVOKABLE void refresh();

    // Metadata jobs, by job ID (from the jobs model).
    Q_INVOKABLE void cancelJob(const QString& jobId);
    Q_INVOKABLE void retryJob(const QString& jobId);
    Q_INVOKABLE void cancelAllJobs();
    // Reruns: queue a new request (new generation) unless a job for that
    // component is already waiting or running, which is then returned. The
    // shown results and the user's corrections stay until a run publishes.
    Q_INVOKABLE void rerunMetadata(const QString& bookId);
    Q_INVOKABLE void rerunContents(const QString& bookId);
    // Closing the window: cancels imports and stops processing without
    // cancelling queued jobs (they resume next time). The window then waits
    // for !busy.
    Q_INVOKABLE void prepareToClose();

    State state() const { return m_state; }
    bool opening() const { return m_state == State::Opening; }
    bool ready() const { return m_state == State::Ready; }
    bool failed() const { return m_state == State::Failed; }
    bool busy() const
    {
        return opening() || importing() || m_refreshInFlight || m_recoveringJobs || m_processingBusy;
    }
    bool importing() const { return m_importTotal > 0; }
    int importTotal() const { return m_importTotal; }
    int importDone() const { return m_importDone; }
    QString currentFile() const { return m_currentFile; }
    double fileProgress() const { return m_fileProgress; }
    QString libraryPath() const { return m_libraryPath; }
    QString statusText() const { return m_statusText; }
    QStringList problems() const { return m_problems; }
    BookListModel* books() { return &m_books; }
    JobListModel* jobs() { return &m_jobs; }
    BookInspector* inspector() { return &m_inspector; }
    SearchController* search() { return &m_search; }
    reader::ReaderController* reader() { return &m_reader; }
    bool processingAvailable() const { return m_extractor != nullptr; }
    bool ocrAvailable() const { return m_ocrAvailable; }
    bool closing() const { return m_closing; }
    BatchSummary lastBatch() const { return m_lastBatch; }

signals:
    void stateChanged();
    void busyChanged();
    void importProgressChanged();
    void statusTextChanged();
    void problemsChanged();
    void importsFinished();  // A batch ended; lastBatch() holds its counts.
    void booksRefreshed();
    void closingChanged();

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
    void startProcessing();  // Creates the coordinator, recovers jobs, then starts it.
    void startJobs();        // Starts the worker once recovery has run.
    void runRefresh();

    BookListModel m_books;
    JobListModel m_jobs;
    BookInspector m_inspector;
    SearchController m_search;
    reader::ReaderController m_reader;
    std::shared_ptr<processing::MetadataExtractor> m_extractor;
    std::shared_ptr<processing::ContentsAnalyzer> m_analyzer;
    std::unique_ptr<processing::ProcessingCoordinator> m_coordinator;  // Destroyed before m_library.
    bool m_ocrAvailable = false;
    bool m_recoveringJobs = false;
    bool m_jobsRecovered = false;
    bool m_processingBusy = false;
    bool m_closing = false;
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
    bool m_refreshInFlight = false;
    bool m_refreshAgain = false;
    BatchSummary m_batch;
    BatchSummary m_lastBatch;
};

} // namespace mbl::presentation
