#include "presentation/librarycontroller.h"

#include "catalog/catalog.h"
#include "catalog/jobs.h"
#include "catalog/library.h"
#include "processing/processingcoordinator.h"
#include "storage/importservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFuture>
#include <QMetaObject>
#include <QMutexLocker>

namespace mbl::presentation {

using storage::ImportResult;
using Outcome = ImportResult::Outcome;

struct LibraryController::FileResult {
    QString path;
    Outcome outcome = Outcome::Failed;
    QString error;
};

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("LibraryController", text);
}

// Plural-aware: `text` contains %n.
QString trn(const char* text, int n)
{
    return QCoreApplication::translate("LibraryController", text, nullptr, n);
}

} // namespace

LibraryController::LibraryController(QObject* parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
    m_pool.setObjectName(QStringLiteral("mbl-library-worker"));
    m_jobs.setTitleLookup([this](const domain::BookId& id) { return m_books.titleOf(id); });
    m_search.results()->setStateLookup([this](const domain::BookId& id) { return m_books.processingStateOf(id); });
}

LibraryController::~LibraryController()
{
    m_cancel->store(true);
    {
        QMutexLocker lock(&m_queueMutex);
        m_queue.clear();
    }
    m_coordinator.reset();  // Stops and waits; the window already waited for !busy.
    m_pool.waitForDone();
    m_importer.reset();
    m_library.reset();  // Closes the catalog and releases the lock.
}

void LibraryController::setState(State state)
{
    if (m_state == state)
        return;
    const bool wasBusy = busy();
    m_state = state;
    emit stateChanged();
    if (busy() != wasBusy)
        emit busyChanged();
}

void LibraryController::setStatus(const QString& text)
{
    if (m_statusText == text)
        return;
    m_statusText = text;
    emit statusTextChanged();
}

void LibraryController::setBusyFlags(const std::function<void()>& change)
{
    const bool wasBusy = busy();
    change();
    if (busy() != wasBusy)
        emit busyChanged();
}

void LibraryController::setProcessors(std::shared_ptr<processing::MetadataExtractor> extractor,
                                      std::shared_ptr<processing::ContentsAnalyzer> analyzer, bool ocrAvailable)
{
    Q_ASSERT(m_state == State::Closed);
    m_extractor = std::move(extractor);
    m_analyzer = std::move(analyzer);
    m_ocrAvailable = ocrAvailable;
}

void LibraryController::open(const QString& rootDir)
{
    if (m_state == State::Opening || m_state == State::Ready)
        return;
    m_libraryPath = QDir::toNativeSeparators(QDir(rootDir).absolutePath());
    setState(State::Opening);
    setStatus(tr("Opening library…"));

    m_pool.start([this, rootDir] {
        auto opened = catalog::Library::open(rootDir);
        if (!opened) {
            const QString error = opened.error().message;
            QMetaObject::invokeMethod(this, [this, error] { onOpened(nullptr, nullptr, error, {}); },
                                      Qt::QueuedConnection);
            return;
        }
        std::shared_ptr<catalog::Library> library(std::move(opened.value()));
        auto importer = std::make_shared<storage::ImportService>(*library);
        const storage::RecoveryReport report = importer->recover();
        QStringList parts;
        if (report.registered)
            parts << trn("%n interrupted import(s) completed", report.registered);
        if (report.abandoned + report.failed)
            parts << trn("%n interrupted import(s) could not be completed; import those files again",
                         report.abandoned + report.failed);
        if (report.deferred)
            parts << trn("%n interrupted import(s) will be retried at the next start", report.deferred);
        const QString recoveryText = parts.join(QStringLiteral("; "));
        QMetaObject::invokeMethod(
            this, [this, library, importer, recoveryText] { onOpened(library, importer, {}, recoveryText); },
            Qt::QueuedConnection);
    });
}

void LibraryController::onOpened(std::shared_ptr<catalog::Library> library,
                                 std::shared_ptr<storage::ImportService> importer, QString error,
                                 QString recoveryText)
{
    if (!library) {
        // Files queued while opening can never be imported by this session:
        // report them and end the batch so busy/importing clear.
        int dropped = 0;
        {
            QMutexLocker lock(&m_queueMutex);
            dropped = int(m_queue.size());
            m_queue.clear();
        }
        setState(State::Failed);
        setStatus(tr("The library could not be opened: %1").arg(error));
        if (dropped > 0) {
            m_problems << trn("%n file(s) were not imported because the library could not be opened.", dropped);
            emit problemsChanged();
            m_batch.failed += dropped;
            finishBatch();
        }
        return;
    }
    m_library = std::move(library);
    m_importer = std::move(importer);
    m_inspector.setLibrary(m_library);
    m_search.setLibrary(m_library);
    m_reader.setLibrary(m_library);
    setState(State::Ready);
    setStatus(recoveryText.isEmpty() ? tr("Library ready.") : recoveryText);
    refresh();
    startBatch();  // Files queued while opening.
    startProcessing();
}

void LibraryController::startProcessing()
{
    if (!m_extractor || m_coordinator)
        return;
    m_coordinator = std::make_unique<processing::ProcessingCoordinator>(*m_library, m_extractor, m_analyzer);
    auto* coordinator = m_coordinator.get();
    connect(coordinator, &processing::ProcessingCoordinator::jobChanged, this, [this](const domain::JobRecord& job) {
        m_jobs.upsert(job);
        m_books.updateJob(job);
        m_search.results()->statesChanged();
    });
    connect(coordinator, &processing::ProcessingCoordinator::metadataPublished, this, [this] { refresh(); });
    connect(coordinator, &processing::ProcessingCoordinator::contentsPublished, this, [this] { refresh(); });
    connect(coordinator, &processing::ProcessingCoordinator::jobProgress, this,
            [this](const domain::JobId& job, const QString& stage, int pages) { m_jobs.setProgress(job, stage, pages); });
    connect(coordinator, &processing::ProcessingCoordinator::enqueueFailed, this,
            [this](const domain::BookId&, const QString& error) {
                setStatus(tr("Metadata extraction could not be requested: %1").arg(error));
            });
    connect(coordinator, &processing::ProcessingCoordinator::busyChanged, this, [this, coordinator] {
        setBusyFlags([this, coordinator] { m_processingBusy = coordinator->busy(); });
    });

    // Recovery runs before the worker starts (ProcessingCoordinator::recover).
    setBusyFlags([this] { m_recoveringJobs = true; });
    coordinator->recover().then(this, [this](const domain::Result<processing::Recovery>& recovery) {
        if (!recovery) {
            setStatus(tr("Interrupted metadata jobs could not be recovered: %1").arg(recovery.error().message));
        } else if (recovery.value().jobs.requeued > 0) {
            setStatus(trn("%n interrupted job(s) queued again.", recovery.value().jobs.requeued));
        }
        m_jobsRecovered = true;
        setBusyFlags([this] { m_recoveringJobs = false; });
        refresh();
        startJobs();
    });
}

void LibraryController::startJobs()
{
    if (m_coordinator && m_jobsRecovered && !m_closing)
        m_coordinator->start();
}

void LibraryController::cancelJob(const QString& jobId)
{
    if (m_coordinator)
        m_coordinator->cancelJob(domain::JobId::fromString(jobId));
}

void LibraryController::retryJob(const QString& jobId)
{
    if (!m_coordinator || m_closing)
        return;
    const auto job = m_jobs.job(domain::JobId::fromString(jobId));
    if (!job)
        return;
    if (job->kind == domain::JobKind::Metadata)
        m_coordinator->enqueueMetadata(job->book);
    else
        m_coordinator->enqueueContents(job->book);
}

void LibraryController::cancelAllJobs()
{
    if (m_coordinator)
        m_coordinator->cancelAll();
}

void LibraryController::prepareToClose()
{
    if (!m_closing) {
        m_closing = true;
        emit closingChanged();
    }
    m_reader.flushPosition();  // Where the open book was being read.
    cancelImports();
    if (m_coordinator)
        m_coordinator->stop();  // Not a cancel: queued jobs resume next time.
}

void LibraryController::refresh()
{
    if (!m_library)
        return;
    // Imports and publications each ask for a reload. Keep one in flight and
    // at most one more pending, so the database thread (shared with the
    // processing worker) and closing never wait behind a backlog of reloads.
    if (m_refreshInFlight) {
        m_refreshAgain = true;
        return;
    }
    setBusyFlags([this] { m_refreshInFlight = true; });
    runRefresh();
}

void LibraryController::runRefresh()
{
    struct Snapshot {
        domain::Result<QList<domain::BookSummary>> books;
        domain::Result<QList<domain::JobRecord>> latest;
        domain::Result<QList<domain::JobRecord>> recent;
        domain::Result<QList<domain::JobRecord>> open;
    };
    m_library
        ->run([](QSqlDatabase& db) {
            // Recent jobs for the activity list, plus every open job however
            // old, so waiting counts and per-job Cancel are complete.
            return Snapshot{catalog::listBooks(db, domain::Lifecycle::Active), catalog::latestJobs(db),
                            catalog::listJobs(db, false, 100), catalog::listJobs(db, true, -1)};
        })
        .then(this, [this](const Snapshot& snapshot) {
            if (snapshot.books) {
                m_books.setBooks(snapshot.books.value());
                m_jobs.titlesChanged();
            } else {
                setStatus(tr("The book list could not be loaded: %1").arg(snapshot.books.error().message));
            }
            // jobChanged may already have delivered a newer state than this
            // snapshot; both models merge and keep the newer one (updatedAt).
            if (snapshot.latest)
                m_books.setLatestJobs(snapshot.latest.value());
            for (const auto* jobs : {&snapshot.recent, &snapshot.open}) {
                if (*jobs) {
                    for (const domain::JobRecord& job : jobs->value())
                        m_jobs.upsert(job);
                }
            }
            m_inspector.reload();  // The shown book may have new metadata or contents.
            m_search.refresh();    // New titles or contents entries may match now.
            emit booksRefreshed();
            if (m_refreshAgain && m_library) {
                m_refreshAgain = false;
                runRefresh();  // Still in flight: busy stays set.
                return;
            }
            setBusyFlags([this] { m_refreshInFlight = false; });
        });
}

void LibraryController::importUrls(const QList<QUrl>& urls)
{
    QStringList paths;
    for (const QUrl& url : urls) {
        if (url.isLocalFile())
            paths << url.toLocalFile();  // Never toString(): keeps non-ASCII paths intact.
    }
    importFiles(paths);
}

void LibraryController::importFiles(const QStringList& localPaths)
{
    if (localPaths.isEmpty() || m_state == State::Failed || m_state == State::Closed)
        return;
    if (!importing() && !m_problems.isEmpty()) {  // A fresh batch starts.
        m_problems.clear();
        emit problemsChanged();
    }
    {
        QMutexLocker lock(&m_queueMutex);
        m_queue += localPaths;
    }
    setBusyFlags([this, &localPaths] { m_importTotal += int(localPaths.size()); });
    emit importProgressChanged();
    if (m_state == State::Ready)
        startBatch();
}

void LibraryController::cancelImports()
{
    int dropped = 0;
    {
        QMutexLocker lock(&m_queueMutex);
        dropped = int(m_queue.size());
        m_queue.clear();
    }
    m_batch.cancelled += dropped;
    m_importDone += dropped;
    m_cancel->store(true);
    emit importProgressChanged();
    if (m_batchRunning)
        setStatus(tr("Cancelling…"));
    else if (dropped > 0)
        onBatchFinished();
}

void LibraryController::startBatch()
{
    if (m_batchRunning || !m_importer)
        return;
    {
        QMutexLocker lock(&m_queueMutex);
        if (m_queue.isEmpty())
            return;
    }
    m_batchRunning = true;
    m_cancel->store(false);
    const auto importer = m_importer;
    const auto cancel = m_cancel;

    m_pool.start([this, importer, cancel] {
        for (;;) {
            QString path;
            {
                QMutexLocker lock(&m_queueMutex);
                if (m_queue.isEmpty() || cancel->load())
                    break;
                path = m_queue.takeFirst();
            }
            QMetaObject::invokeMethod(this, [this, path] { onFileStarted(path); }, Qt::QueuedConnection);
            QElapsedTimer throttle;
            throttle.start();
            const ImportResult r = importer->importFile(path, cancel.get(), [this, &throttle](qint64 done, qint64 total) {
                if (throttle.elapsed() < 100 && done < total)
                    return;  // At most ~10 progress updates a second.
                throttle.restart();
                QMetaObject::invokeMethod(this, [this, done, total] { onFileProgress(done, total); },
                                          Qt::QueuedConnection);
            });
            FileResult result{path, r.outcome, r.error};
            QMetaObject::invokeMethod(this, [this, result] { onFileFinished(result); }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(this, [this] { onBatchFinished(); }, Qt::QueuedConnection);
    });
}

void LibraryController::onFileStarted(const QString& path)
{
    m_currentFile = QFileInfo(path).fileName();
    m_fileProgress = 0;
    emit importProgressChanged();
    setStatus(tr("Importing %1…").arg(m_currentFile));
}

void LibraryController::onFileProgress(qint64 done, qint64 total)
{
    m_fileProgress = total > 0 ? double(done) / double(total) : 0;
    emit importProgressChanged();
}

void LibraryController::onFileFinished(const FileResult& result)
{
    const QString name = QFileInfo(result.path).fileName();
    switch (result.outcome) {
    case Outcome::Imported:
        ++m_batch.imported;
        refresh();
        startJobs();  // The import queued the book's metadata job.
        break;
    case Outcome::Duplicate:
        ++m_batch.duplicates;
        m_problems << tr("%1 is already in the library.").arg(name);
        break;
    case Outcome::DuplicateInTrash:
        ++m_batch.duplicatesInTrash;
        m_problems << tr("%1 is already in the library, in Trash.").arg(name);
        break;
    case Outcome::Cancelled:
        ++m_batch.cancelled;
        break;
    case Outcome::Failed:
    case Outcome::Interrupted:
        ++m_batch.failed;
        m_problems << tr("%1 was not imported: %2").arg(name, result.error);
        break;
    }
    ++m_importDone;
    m_fileProgress = 1;
    emit importProgressChanged();
    if (result.outcome != Outcome::Imported && result.outcome != Outcome::Cancelled)
        emit problemsChanged();
}

void LibraryController::onBatchFinished()
{
    m_batchRunning = false;
    // Files queued after the worker's last check start a new batch. That
    // includes files added after a cancel: cancelImports() already cleared
    // the queue, so anything in it now was added afterwards. Decide under
    // the lock, but call startBatch() only after releasing it, because
    // startBatch() locks m_queueMutex again (QMutex is not recursive).
    bool more = false;
    {
        QMutexLocker lock(&m_queueMutex);
        more = !m_queue.isEmpty();
    }
    if (more && m_importer) {
        startBatch();  // Resets m_cancel.
        return;
    }
    finishBatch();
}

void LibraryController::finishBatch()
{
    m_lastBatch = m_batch;
    m_batch = {};
    QStringList parts;
    if (m_lastBatch.imported)
        parts << tr("%1 imported").arg(m_lastBatch.imported);
    if (m_lastBatch.duplicates + m_lastBatch.duplicatesInTrash)
        parts << tr("%1 already in the library").arg(m_lastBatch.duplicates + m_lastBatch.duplicatesInTrash);
    if (m_lastBatch.failed)
        parts << tr("%1 failed").arg(m_lastBatch.failed);
    if (m_lastBatch.cancelled)
        parts << tr("%1 cancelled").arg(m_lastBatch.cancelled);
    if (m_state != State::Failed)  // Keep the reason the library could not be opened.
        setStatus(parts.isEmpty() ? tr("Library ready.") : parts.join(QStringLiteral(", ")) + u'.');
    setBusyFlags([this] {
        m_importTotal = 0;
        m_importDone = 0;
        m_currentFile.clear();
        m_fileProgress = 0;
    });
    emit importProgressChanged();
    emit importsFinished();
}

} // namespace mbl::presentation
