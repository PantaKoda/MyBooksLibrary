// A4: runs durable processing jobs. One worker thread runs one SDK operation
// at a time; catalog work goes through the library's database thread; results
// are published only if the request is still current (generation, lifecycle,
// source digest) and the job was not cancelled. Signals are delivered on the
// thread that owns the coordinator (the GUI thread in the application).
//
// A book with both a metadata and a contents job queued is served by ONE SDK
// call (ContentsAnalyzer::analyzeBook), so its pages are read and OCR'd once;
// each job still publishes, cancels and fails on its own (docs/PROCESSING.md).
//
// An export writes a bookmarked copy (BookExporter): its destination is
// validated (A1) when it is requested and again right before the write, and
// a committed copy is recorded as written whatever happens after the commit.
#pragma once

#include "catalog/jobs.h"
#include "domain/export.h"
#include "domain/ids.h"
#include "domain/jobs.h"
#include "domain/result.h"
#include "processing/bookexporter.h"
#include "processing/contentsanalyzer.h"
#include "processing/metadataextractor.h"
#include "storage/librarylayout.h"

#include <QFuture>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QThreadPool>

#include <atomic>
#include <memory>

namespace mbl::catalog {
class Library;
}

namespace mbl::processing {

struct Recovery {
    catalog::JobRecovery jobs;
    QStringList removedReports;  // Relative paths of unpublished reports.
};

// Cancel flags of claimed jobs, shared with database tasks so a cancel
// recorded there reaches the SDK call even if the claim came first.
//
// A group flag serves one SDK call shared by several jobs: it is raised when
// every member that still needs the call has its own flag raised (a member
// that already has its result is settled), or at stopAll().
class CancelFlags {
public:
    std::shared_ptr<std::atomic_bool> get(const domain::JobId& id);  // Created raised after stopAll().
    void raise(const domain::JobId& id);                            // Creates it raised if absent.
    void raiseExisting(const domain::JobId& id);
    void raiseAll();
    void stopAll();
    void drop(const domain::JobId& id);

    std::shared_ptr<std::atomic_bool> group(const QList<domain::JobId>& members);
    void settle(const domain::JobId& id);  // The member no longer needs its group's call.

private:
    struct Group {
        std::shared_ptr<std::atomic_bool> flag;
        QSet<domain::JobId> open;
    };
    std::shared_ptr<std::atomic_bool> getLocked(const domain::JobId& id);
    void evaluateLocked(Group& group);
    void evaluateGroupOfLocked(const domain::JobId& id);

    QMutex m_mutex;
    bool m_stopped = false;
    QHash<domain::JobId, std::shared_ptr<std::atomic_bool>> m_flags;
    QHash<domain::JobId, std::shared_ptr<Group>> m_groups;
};

class ProcessingCoordinator : public QObject {
    Q_OBJECT

public:
    // `contents` or `exporter` may be null: those jobs then fail as "unsupported".
    ProcessingCoordinator(catalog::Library& library, std::shared_ptr<MetadataExtractor> metadata,
                          std::shared_ptr<ContentsAnalyzer> contents = {}, std::shared_ptr<BookExporter> exporter = {},
                          QObject* parent = nullptr);
    // stop(), then waits for the worker. Only for teardown: the window calls
    // stop() and waits for !busy() (idle) first, staying responsive.
    ~ProcessingCoordinator() override;

    // Closes jobs a previous process left Running (Interrupted, and queues a
    // replacement) or CancelRequested (Cancelled), then removes reports that
    // no run references. Call once, before start().
    QFuture<domain::Result<Recovery>> recover();

    // Queue a job of that kind (or return the pending one) and start the
    // worker. jobChanged() reports the queued job.
    void enqueueMetadata(const domain::BookId& book);
    void enqueueContents(const domain::BookId& book);
    // Validates the destination (outside the library, never a managed source
    // or an imported original, an existing file only if `replaceExisting`),
    // builds the plan from the book's effective contents and queues the
    // export: exportQueued() and jobChanged(), or exportRefused() with the
    // reason. The copy is written by the worker (exportFinished()).
    void enqueueExport(const domain::BookId& book, const QString& destination, bool replaceExisting = false);
    void cancelJob(const domain::JobId& job);
    void cancelAll();   // User "Cancel all": cancels running and queued jobs.
    void start();       // Starts the worker if it is not running.

    // Application shutdown, not a user cancel: stops the running SDK call and
    // claims no more jobs. Queued jobs stay queued; a job stopped this way is
    // closed as Interrupted and requeued, so the next session resumes it.
    // Final: start() does nothing afterwards.
    void stop();

    bool busy() const { return m_running.load(); }

signals:
    void jobChanged(const mbl::domain::JobRecord& job);
    void metadataPublished(const mbl::domain::BookId& book, const mbl::domain::RunId& run);
    void contentsPublished(const mbl::domain::BookId& book, const mbl::domain::RunId& run);
    // SDK stage and pages acquired in it (no total exists); throttled.
    void jobProgress(const mbl::domain::JobId& job, const QString& stage, int pagesAcquired);
    void enqueueFailed(const mbl::domain::BookId& book, const QString& error);
    void exportQueued(const mbl::domain::ExportRecord& record);
    // `fileExists`: refused only because a file is there and replacing was
    // not asked for, so the user can be asked to confirm.
    void exportRefused(const mbl::domain::BookId& book, const QString& error, bool fileExists);
    // The export job ended (written, failed, cancelled or interrupted); its job
    // holds the state and reason.
    void exportFinished(const mbl::domain::ExportRecord& record);
    void busyChanged();
    void idle();  // The worker stopped with nothing left to run.

private:
    void runLoop();                 // Worker thread.
    bool processNext();             // Worker thread; false when nothing is queued.
    void enqueue(const domain::BookId& book, domain::JobKind kind);
    void runMetadataJob(const domain::JobRecord& job, const QString& pdf);
    void runContentsJob(const domain::JobRecord& job, const QString& pdf);
    void runBookJobs(const domain::JobRecord& metadata, const domain::JobRecord& contents, const QString& pdf);
    void runExportJob(const domain::JobRecord& job, const QString& pdf);
    void finishExport(const domain::JobRecord& job, domain::JobState state, const QString& outcome, const QString& error,
                      const domain::ExportOutput& output);
    // Close a job from its SDK result: publish, or record why not.
    void finishMetadata(const domain::JobRecord& job, const MetadataExtraction& result);
    void finishContents(const domain::JobRecord& job, const ContentsAnalysis& result);
    void finishJob(const domain::JobRecord& job, domain::JobState state, const QString& outcome, const QString& error);
    void closeRefused(const domain::JobRecord& job, const domain::Error& error);
    ContentsAnalyzer::Progress progressFor(const domain::JobRecord& metadata, const domain::JobRecord& contents);
    void emitJob(const domain::JobId& id);  // Reloads and emits on the owner thread.

    catalog::Library& m_library;
    storage::LibraryLayout m_layout;
    std::shared_ptr<MetadataExtractor> m_metadata;
    std::shared_ptr<ContentsAnalyzer> m_contents;
    std::shared_ptr<BookExporter> m_exporter;
    QThreadPool m_pool;  // One thread: one SDK operation at a time.
    std::atomic_bool m_running{false};
    std::atomic_bool m_wake{false};
    std::atomic_bool m_stop{false};
    std::shared_ptr<CancelFlags> m_flags = std::make_shared<CancelFlags>();
};

} // namespace mbl::processing
