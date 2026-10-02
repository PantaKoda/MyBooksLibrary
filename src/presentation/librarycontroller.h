// Presentation: the library session behind the main window. Opens the
// library (lock, migrations, import and job recovery), imports files on a
// worker thread, runs metadata jobs through the ProcessingCoordinator, and
// keeps the GUI-owned book and job lists current. QML only calls its
// commands and reads its properties; no SQL, file or SDK work happens in QML
// or on the GUI thread.
//
// Organization (M08): the book list shows a view (the library, one
// collection, or Trash); collections are created, renamed and deleted here;
// books are added to and removed from collections, moved to Trash (its
// running SDK call is stopped early) and restored (resumed work starts).
#pragma once

#include "presentation/backupcontroller.h"
#include "presentation/bookinspector.h"
#include "presentation/booklistmodel.h"
#include "presentation/collectionlistmodel.h"
#include "presentation/exportcontroller.h"
#include "presentation/joblistmodel.h"
#include "presentation/libraryswitcher.h"
#include "presentation/searchcontroller.h"
#include "reader/readercontroller.h"

#include <QMutex>
#include <QQmlEngine>
#include <QSqlDatabase>
#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QUrl>

#include <atomic>
#include <functional>
#include <memory>

namespace mbl::catalog {
class Library;
}
namespace mbl::storage {
class ImportService;
}
namespace mbl::processing {
class BookExporter;
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
    // Failed: why the library could not be opened, in plain words.
    Q_PROPERTY(QString openError READ openError NOTIFY stateChanged)
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
    Q_PROPERTY(mbl::presentation::CollectionListModel* collections READ collections CONSTANT)
    // The Export dialog's session (M09).
    Q_PROPERTY(mbl::presentation::ExportController* exporter READ exporter CONSTANT)
    // Back up… and Restore… (M10).
    Q_PROPERTY(mbl::presentation::BackupController* backup READ backup CONSTANT)
    // Which library this window holds, and Open library… (issue #30).
    Q_PROPERTY(mbl::presentation::LibrarySwitcher* switcher READ switcher CONSTANT)
    // What the book list shows, and its heading.
    Q_PROPERTY(View view READ view NOTIFY viewChanged)
    Q_PROPERTY(QString viewCollectionId READ viewCollectionId NOTIFY viewChanged)
    Q_PROPERTY(QString viewTitle READ viewTitle NOTIFY viewChanged)
    Q_PROPERTY(int libraryCount READ libraryCount NOTIFY countsChanged)
    Q_PROPERTY(int trashCount READ trashCount NOTIFY countsChanged)
    // Books of the last import batch that were already in the library, in Trash.
    Q_PROPERTY(int trashedDuplicateCount READ trashedDuplicateCount NOTIFY trashedDuplicatesChanged)
    Q_PROPERTY(QString organizeError READ organizeError NOTIFY organizeErrorChanged)

public:
    enum class State { Closed, Opening, Ready, Failed };
    enum class View { Library, Collection, Trash };
    Q_ENUM(View)

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
    // Composition root, before open(): writes bookmarked copies (without it,
    // exports are refused as not available).
    void setExporter(std::shared_ptr<processing::BookExporter> exporter);
    void setMetadataExtractor(std::shared_ptr<processing::MetadataExtractor> extractor, bool ocrAvailable)
    {
        setProcessors(std::move(extractor), nullptr, ocrAvailable);
    }

    // Opens (and creates if needed) the library at `rootDir` on the worker
    // thread, then recovers interrupted imports and jobs, loads the book
    // list and starts processing.
    Q_INVOKABLE void open(const QString& rootDir);
    // As open(), but only an existing library (catalog::Library::OpenMode::
    // ExistingOnly): a missing folder, one without a catalog, or a backup
    // fails with the reason, and nothing is created. For a library the user
    // chose in the app (--existing-library).
    void openExisting(const QString& rootDir);
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

    // Views of the book list.
    Q_INVOKABLE void showLibrary();
    Q_INVOKABLE void showCollection(const QString& collectionId);
    Q_INVOKABLE void showTrash();
    // Collections. Refusals (an empty or duplicate name, a book in Trash)
    // are reported in organizeError.
    Q_INVOKABLE void createCollection(const QString& name);
    Q_INVOKABLE void renameCollection(const QString& collectionId, const QString& name);
    Q_INVOKABLE void deleteCollection(const QString& collectionId);  // The books stay.
    Q_INVOKABLE void addToCollection(const QString& collectionId, const QString& bookId);
    Q_INVOKABLE void removeFromCollection(const QString& collectionId, const QString& bookId);
    // Trash: reversible. Moving a book there stops its processing; restoring
    // it resumes the work the trash stopped.
    Q_INVOKABLE void moveToTrash(const QString& bookId);
    Q_INVOKABLE void restoreFromTrash(const QString& bookId);
    Q_INVOKABLE void restoreTrashedDuplicates();
    Q_INVOKABLE void dismissOrganizeError();

    State state() const { return m_state; }
    bool opening() const { return m_state == State::Opening; }
    bool ready() const { return m_state == State::Ready; }
    bool failed() const { return m_state == State::Failed; }
    bool busy() const
    {
        return opening() || importing() || m_refreshInFlight || m_recoveringJobs || m_processingBusy || m_backupBusy;
    }
    bool importing() const { return m_importTotal > 0; }
    int importTotal() const { return m_importTotal; }
    int importDone() const { return m_importDone; }
    QString currentFile() const { return m_currentFile; }
    double fileProgress() const { return m_fileProgress; }
    QString libraryPath() const { return m_libraryPath; }
    QString openError() const { return m_openError; }
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
    CollectionListModel* collections() { return &m_collections; }
    ExportController* exporter() { return &m_export; }
    BackupController* backup() { return &m_backup; }
    LibrarySwitcher* switcher() { return &m_switcher; }
    View view() const { return m_view; }
    QString viewCollectionId() const { return m_view == View::Collection ? m_viewCollection.toString() : QString(); }
    QString viewTitle() const;
    int libraryCount() const { return m_libraryCount; }
    int trashCount() const { return m_trashCount; }
    int trashedDuplicateCount() const { return int(m_trashedDuplicates.size()); }
    QString organizeError() const { return m_organizeError; }
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
    void viewChanged();
    void countsChanged();
    void trashedDuplicatesChanged();
    void organizeErrorChanged();
    void organized();  // An organization change was saved (the list then refreshes).

private:
    struct FileResult;
    void openLibrary(const QString& rootDir, bool existingOnly);
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
    void setView(View view, const domain::CollectionId& collection = {});
    void setOrganizeError(const QString& error);
    // Runs a catalog change on the database thread; then refreshes, or reports
    // the refusal (in the command's words when `describe` knows the error).
    void organize(std::function<domain::Status(QSqlDatabase&)> change,
                  std::function<QString(const domain::Error&)> describe, std::function<void()> after = {});
    void updateViewTitle();  // Emits viewChanged only when the heading changed.

    BookListModel m_books;
    CollectionListModel m_collections;
    View m_view = View::Library;
    QString m_shownViewTitle;
    domain::CollectionId m_viewCollection;
    int m_libraryCount = 0;
    int m_trashCount = 0;
    QList<domain::BookId> m_trashedDuplicates;
    QString m_organizeError;
    JobListModel m_jobs;
    BookInspector m_inspector;
    SearchController m_search;
    reader::ReaderController m_reader;
    std::shared_ptr<processing::MetadataExtractor> m_extractor;
    std::shared_ptr<processing::ContentsAnalyzer> m_analyzer;
    std::shared_ptr<processing::BookExporter> m_exporter;
    ExportController m_export;
    BackupController m_backup;
    bool m_backupBusy = false;  // A backup or restore runs; closing waits for it.
    LibrarySwitcher m_switcher;
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
    QString m_openError;
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
