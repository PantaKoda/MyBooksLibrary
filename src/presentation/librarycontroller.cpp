#include "presentation/librarycontroller.h"

#include "catalog/catalog.h"
#include "catalog/collections.h"
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
#include <QSet>

namespace mbl::presentation {

using storage::ImportResult;
using Outcome = ImportResult::Outcome;

struct LibraryController::FileResult {
    QString path;
    Outcome outcome = Outcome::Failed;
    QString error;
    std::optional<domain::BookId> book;  // The registered or existing book.
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
    // A correction changes the book's row and what search finds.
    connect(&m_inspector, &BookInspector::corrected, this, [this] { refresh(); });
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
    if (!job || job->kind == domain::JobKind::Export)  // Exports are asked for again with a destination.
        return;
    if (job->kind == domain::JobKind::Metadata)
        m_coordinator->enqueueMetadata(job->book);
    else
        m_coordinator->enqueueContents(job->book);
}

void LibraryController::rerunMetadata(const QString& bookId)
{
    const domain::BookId book = domain::BookId::fromString(bookId);
    if (m_coordinator && !m_closing && !book.isNull())
        m_coordinator->enqueueMetadata(book);
}

void LibraryController::rerunContents(const QString& bookId)
{
    const domain::BookId book = domain::BookId::fromString(bookId);
    if (m_coordinator && !m_closing && !book.isNull())
        m_coordinator->enqueueContents(book);
}

void LibraryController::showLibrary()
{
    setView(View::Library);
}

void LibraryController::showCollection(const QString& collectionId)
{
    const domain::CollectionId id = domain::CollectionId::fromString(collectionId);
    if (!id.isNull())
        setView(View::Collection, id);
}

void LibraryController::showTrash()
{
    setView(View::Trash);
}

void LibraryController::setView(View view, const domain::CollectionId& collection)
{
    const domain::CollectionId shown = view == View::Collection ? collection : domain::CollectionId();
    if (view == m_view && shown == m_viewCollection)
        return;
    m_view = view;
    m_viewCollection = shown;
    // Search follows a collection; elsewhere it covers the library.
    m_search.setCollection(view == View::Collection ? std::optional<domain::CollectionId>(shown) : std::nullopt);
    m_shownViewTitle = viewTitle();
    emit viewChanged();
    refresh();
}

void LibraryController::updateViewTitle()
{
    const QString title = viewTitle();
    if (title == m_shownViewTitle)
        return;
    m_shownViewTitle = title;
    emit viewChanged();
}

QString LibraryController::viewTitle() const
{
    switch (m_view) {
    case View::Library: return tr("Library");
    case View::Trash: return tr("Trash");
    case View::Collection: {
        const QString name = m_collections.nameOf(m_viewCollection);
        return name.isEmpty() ? tr("Collection") : name;
    }
    }
    return {};
}

void LibraryController::setOrganizeError(const QString& error)
{
    if (m_organizeError == error)
        return;
    m_organizeError = error;
    emit organizeErrorChanged();
}

void LibraryController::dismissOrganizeError()
{
    setOrganizeError({});
}

namespace {

// Messages for the refusals collection commands can meet; other errors are
// reported with their own text.
QString collectionRefusal(const domain::Error& error)
{
    switch (error.code) {
    case domain::ErrorCode::Duplicate:
        return QCoreApplication::translate("LibraryController", "A collection with that name already exists.");
    case domain::ErrorCode::InvalidArgument:
        return QCoreApplication::translate("LibraryController", "Enter a name for the collection.");
    case domain::ErrorCode::Trashed:
        return QCoreApplication::translate("LibraryController",
                                           "A book in Trash cannot be added to a collection. Restore it first.");
    case domain::ErrorCode::NotFound:
        return QCoreApplication::translate("LibraryController", "That book or collection is no longer in the library.");
    default:
        return {};
    }
}

QString bookRefusal(const domain::Error& error)
{
    if (error.code == domain::ErrorCode::NotFound)
        return QCoreApplication::translate("LibraryController", "That book is no longer in the library.");
    return {};
}

} // namespace

void LibraryController::organize(std::function<domain::Status(QSqlDatabase&)> change,
                                 std::function<QString(const domain::Error&)> describe, std::function<void()> after)
{
    if (!m_library || m_closing)
        return;
    setOrganizeError({});
    m_library->run(std::move(change)).then(this, [this, describe, after](const domain::Status& done) {
        if (!done) {
            const QString known = describe ? describe(done.error()) : QString();
            setOrganizeError(known.isEmpty() ? tr("The change was not saved: %1").arg(done.error().message) : known);
            refresh();
            return;
        }
        if (after)
            after();
        refresh();
        emit organized();
    });
}

void LibraryController::createCollection(const QString& name)
{
    organize(
        [name](QSqlDatabase& db) -> domain::Status {
            auto created = catalog::createCollection(db, name);
            if (!created)
                return created.error();
            return domain::Done{};
        },
        collectionRefusal);
}

void LibraryController::renameCollection(const QString& collectionId, const QString& name)
{
    const auto id = domain::CollectionId::fromString(collectionId);
    organize([id, name](QSqlDatabase& db) { return catalog::renameCollection(db, id, name); }, collectionRefusal);
}

void LibraryController::deleteCollection(const QString& collectionId)
{
    const auto id = domain::CollectionId::fromString(collectionId);
    organize([id](QSqlDatabase& db) { return catalog::deleteCollection(db, id); }, collectionRefusal,
             [this, id] {
                 if (m_view == View::Collection && m_viewCollection == id)
                     setView(View::Library);
             });
}

void LibraryController::addToCollection(const QString& collectionId, const QString& bookId)
{
    const auto id = domain::CollectionId::fromString(collectionId);
    const auto book = domain::BookId::fromString(bookId);
    organize([id, book](QSqlDatabase& db) { return catalog::addToCollection(db, id, {book}); }, collectionRefusal);
}

void LibraryController::removeFromCollection(const QString& collectionId, const QString& bookId)
{
    const auto id = domain::CollectionId::fromString(collectionId);
    const auto book = domain::BookId::fromString(bookId);
    organize([id, book](QSqlDatabase& db) { return catalog::removeFromCollection(db, id, {book}); },
             collectionRefusal);
}

void LibraryController::moveToTrash(const QString& bookId)
{
    const auto book = domain::BookId::fromString(bookId);
    // The catalog ends the book's jobs; a running SDK call is also told to
    // stop now, instead of finishing a result that would be refused.
    auto stopping = std::make_shared<QList<domain::JobId>>();
    organize(
        [book, stopping](QSqlDatabase& db) -> domain::Status {
            if (auto trashed = catalog::trashBook(db, book); !trashed)
                return trashed;
            auto open = catalog::listJobs(db, true, -1);
            if (!open)
                return open.error();
            for (const domain::JobRecord& job : open.value()) {
                if (job.book == book && job.state == domain::JobState::CancelRequested)
                    *stopping << job.id;
            }
            return domain::Done{};
        },
        bookRefusal,
        [this, stopping] {
            for (const domain::JobId& job : *stopping) {
                if (m_coordinator)
                    m_coordinator->cancelJob(job);
            }
        });
}

void LibraryController::restoreFromTrash(const QString& bookId)
{
    const auto book = domain::BookId::fromString(bookId);
    organize([book](QSqlDatabase& db) { return catalog::restoreBook(db, book); }, bookRefusal,
             [this, book] {
                 m_trashedDuplicates.removeAll(book);
                 emit trashedDuplicatesChanged();
                 startJobs();  // Resumed work runs now, not at the next launch.
             });
}

void LibraryController::restoreTrashedDuplicates()
{
    const QList<domain::BookId> books = m_trashedDuplicates;
    if (books.isEmpty())
        return;
    organize(
        [books](QSqlDatabase& db) -> domain::Status {
            for (const domain::BookId& book : books) {
                if (auto restored = catalog::restoreBook(db, book); !restored)
                    return restored;
            }
            return domain::Done{};
        },
        bookRefusal,
        [this] {
            m_trashedDuplicates.clear();
            emit trashedDuplicatesChanged();
            startJobs();
        });
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
        View view = View::Library;
        domain::CollectionId collection;
        domain::Result<QList<domain::BookSummary>> books;
        domain::Result<QList<domain::BookSummary>> trashed;
        std::optional<domain::Result<QList<domain::BookId>>> collectionMembers;
        domain::Result<QList<domain::CollectionSummary>> collections;
        domain::Result<QList<domain::JobRecord>> latest;
        domain::Result<QList<domain::JobRecord>> recent;
        domain::Result<QList<domain::JobRecord>> open;
    };
    m_library
        ->run([view = m_view, collection = m_viewCollection](QSqlDatabase& db) {
            // Recent jobs for the activity list, plus every open job however
            // old, so waiting counts and per-job Cancel are complete.
            Snapshot s{view,
                       collection,
                       catalog::listBooks(db, domain::Lifecycle::Active),
                       catalog::listBooks(db, domain::Lifecycle::Trashed),
                       std::nullopt,
                       catalog::listCollections(db),
                       catalog::latestJobs(db),
                       catalog::listJobs(db, false, 100),
                       catalog::listJobs(db, true, -1)};
            if (view == View::Collection)
                s.collectionMembers = catalog::collectionBookIds(db, collection);
            return s;
        })
        .then(this, [this](const Snapshot& snapshot) {
            if (snapshot.collections)
                m_collections.setCollections(snapshot.collections.value());
            if (snapshot.books && snapshot.trashed) {
                // Lookups (activity, search) know every book; the rows are the view.
                m_books.setKnownBooks(snapshot.books.value() + snapshot.trashed.value());
                m_jobs.titlesChanged();  // Titles may have changed, whatever the rows show.
                const int library = int(snapshot.books.value().size());
                const int trash = int(snapshot.trashed.value().size());
                if (library != m_libraryCount || trash != m_trashCount) {
                    m_libraryCount = library;
                    m_trashCount = trash;
                    emit countsChanged();
                }
            }
            const bool sameView = snapshot.view == m_view && snapshot.collection == m_viewCollection;
            if (!sameView) {
                m_refreshAgain = true;  // The view changed meanwhile: load it next.
            } else if (snapshot.view == View::Collection && snapshot.collectionMembers
                       && !*snapshot.collectionMembers
                       && snapshot.collectionMembers->error().code == domain::ErrorCode::NotFound) {
                setView(View::Library);  // The collection was deleted.
            } else {
                const domain::Result<QList<domain::BookSummary>>& source =
                    snapshot.view == View::Trash ? snapshot.trashed : snapshot.books;
                const bool membersLoaded = snapshot.view != View::Collection || (snapshot.collectionMembers
                                                                                 && *snapshot.collectionMembers);
                if (source && membersLoaded) {
                    QList<domain::BookSummary> list = source.value();
                    if (snapshot.view == View::Collection) {
                        // The library's books in the collection, in library order.
                        const QList<domain::BookId>& ids = snapshot.collectionMembers->value();
                        const QSet<domain::BookId> members(ids.cbegin(), ids.cend());
                        list.removeIf([&members](const domain::BookSummary& b) { return !members.contains(b.id); });
                    } else if (snapshot.view == View::Trash) {  // Most recently trashed first.
                        std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
                            return a.trashedAt > b.trashedAt;
                        });
                    }
                    m_books.setBooks(list);
                } else {
                    const QString error = !source ? source.error().message : snapshot.collectionMembers->error().message;
                    setStatus(tr("The book list could not be loaded: %1").arg(error));
                }
                updateViewTitle();  // A renamed collection.
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
    if (!importing() && !m_trashedDuplicates.isEmpty()) {  // A fresh batch: forget the last one's.
        m_trashedDuplicates.clear();
        emit trashedDuplicatesChanged();
    }
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
            FileResult result{path, r.outcome, r.error, r.book};
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
        if (result.book && !m_trashedDuplicates.contains(*result.book)) {
            m_trashedDuplicates << *result.book;
            emit trashedDuplicatesChanged();
        }
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
