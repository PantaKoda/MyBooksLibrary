// A4: runs durable processing jobs. One worker thread runs one SDK operation
// at a time; catalog work goes through the library's database thread; results
// are published only if the request is still current (generation, lifecycle,
// source digest) and the job was not cancelled. Signals are delivered on the
// thread that owns the coordinator (the GUI thread in the application).
//
// M04 part 1 runs metadata jobs. TOC jobs (M05) use the same queue.
#pragma once

#include "catalog/jobs.h"
#include "domain/ids.h"
#include "domain/jobs.h"
#include "domain/result.h"
#include "processing/metadataextractor.h"
#include "storage/librarylayout.h"

#include <QFuture>
#include <QHash>
#include <QMutex>
#include <QObject>
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

class ProcessingCoordinator : public QObject {
    Q_OBJECT

public:
    ProcessingCoordinator(catalog::Library& library, std::shared_ptr<MetadataExtractor> metadata,
                          QObject* parent = nullptr);
    // Stops the worker: cancels the running job and waits for it. Only for
    // teardown; the window waits for !busy() first (cancelAll + idle).
    ~ProcessingCoordinator() override;

    // Closes jobs a previous process left Running (Interrupted, and queues a
    // replacement) or CancelRequested (Cancelled), then removes reports that
    // no run references. Call once, before start().
    QFuture<domain::Result<Recovery>> recover();

    // Queues a metadata job (or returns the open one) and starts the worker.
    // jobChanged() reports the queued job.
    void enqueueMetadata(const domain::BookId& book);
    void cancelJob(const domain::JobId& job);
    void cancelAll();   // Cancels running and queued jobs.
    void start();       // Starts the worker if it is not running.

    bool busy() const { return m_running.load(); }

signals:
    void jobChanged(const mbl::domain::JobRecord& job);
    void metadataPublished(const mbl::domain::BookId& book, const mbl::domain::RunId& run);
    void enqueueFailed(const mbl::domain::BookId& book, const QString& error);
    void busyChanged();
    void idle();  // The worker stopped with nothing left to run.

private:
    void runLoop();                 // Worker thread.
    bool processNext();             // Worker thread; false when nothing is queued.
    void runMetadataJob(const domain::JobRecord& job);
    void emitJob(const domain::JobId& id);  // Reloads and emits on the owner thread.
    std::shared_ptr<std::atomic_bool> flagFor(const domain::JobId& id);
    void dropFlag(const domain::JobId& id);

    catalog::Library& m_library;
    storage::LibraryLayout m_layout;
    std::shared_ptr<MetadataExtractor> m_metadata;
    QThreadPool m_pool;  // One thread: one SDK operation at a time.
    std::atomic_bool m_running{false};
    std::atomic_bool m_wake{false};
    std::atomic_bool m_stop{false};
    QMutex m_flagsMutex;
    QHash<domain::JobId, std::shared_ptr<std::atomic_bool>> m_flags;  // Cancel flags of claimed jobs.
};

} // namespace mbl::processing
