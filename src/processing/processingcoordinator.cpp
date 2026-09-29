#include "processing/processingcoordinator.h"

#include "catalog/catalog.h"
#include "catalog/exports.h"
#include "catalog/jobs.h"
#include "catalog/library.h"
#include "storage/exportdestination.h"
#include "storage/reportstore.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QMutexLocker>
#include <QScopeGuard>

#include <exception>

namespace mbl::processing {

using namespace mbl::domain;

namespace {

template <typename T>
T wait(QFuture<T> future)
{
    return future.result();
}

RunIdentity identityOf(const QString& sha256, const QString& sdkVersion, const QString& modelIdentity,
                       const QString& optionsJson, const QString& outcome, const QString& reportPath)
{
    RunIdentity identity;
    identity.sourceSha256 = sha256.toLower();
    identity.sdkVersion = sdkVersion;
    identity.modelIdentity = modelIdentity;
    identity.optionsJson = optionsJson;
    identity.outcome = outcome;
    identity.reportPath = reportPath;
    return identity;
}

// A1's rules for an export destination, with every managed source and
// imported original of the library protected under any name.
storage::ExportDestinationRules destinationRules(const storage::LibraryLayout& layout,
                                                 const catalog::ProtectedFiles& files, bool replaceExisting)
{
    storage::ExportDestinationRules rules;
    for (const QString& managed : files.managedPaths)
        rules.protectedFiles << layout.absolute(managed);
    rules.protectedFiles << files.originalPaths;
    rules.replaceExisting = replaceExisting;
    return rules;
}

// How a failed export is recorded: a stable outcome code for the SDK's refusal.
QString exportFailureOutcome(const QString& errorCode)
{
    if (errorCode == QLatin1String("OutputExists"))
        return QStringLiteral("output_exists");
    if (errorCode == QLatin1String("InputChanged"))
        return QStringLiteral("source_mismatch");
    if (errorCode == QLatin1String("InvalidPlan"))
        return QStringLiteral("invalid_plan");
    return QStringLiteral("export_failed");
}

} // namespace

ProcessingCoordinator::ProcessingCoordinator(catalog::Library& library, std::shared_ptr<MetadataExtractor> metadata,
                                             std::shared_ptr<ContentsAnalyzer> contents,
                                             std::shared_ptr<BookExporter> exporter, QObject* parent)
    : QObject(parent), m_library(library), m_layout(library.rootDir()), m_metadata(std::move(metadata)),
      m_contents(std::move(contents)), m_exporter(std::move(exporter))
{
    m_pool.setMaxThreadCount(1);
    m_pool.setObjectName(QStringLiteral("mbl-processing"));
}

ProcessingCoordinator::~ProcessingCoordinator()
{
    stop();
    m_pool.waitForDone();
}

void ProcessingCoordinator::stop()
{
    m_stop.store(true);
    m_flags->stopAll();
}

QFuture<Result<Recovery>> ProcessingCoordinator::recover()
{
    // Runs on the database thread so that no publication can interleave
    // between reading the referenced reports and removing the others.
    const bool workerIdle = !m_running.load();
    return m_library.run([layout = m_layout, workerIdle](QSqlDatabase& db) -> Result<Recovery> {
        Recovery recovery;
        auto jobs = catalog::recoverJobs(db);
        if (!jobs)
            return jobs.error();
        recovery.jobs = jobs.value();
        if (workerIdle) {  // A running job may hold a report it has not published yet.
            auto referenced = catalog::referencedReportPaths(db);
            if (!referenced)
                return referenced.error();
            recovery.removedReports = storage::removeUnreferencedReports(layout, referenced.value());
        }
        return recovery;
    });
}

void ProcessingCoordinator::enqueueMetadata(const BookId& book)
{
    enqueue(book, JobKind::Metadata);
}

void ProcessingCoordinator::enqueueContents(const BookId& book)
{
    enqueue(book, JobKind::Toc);
}

void ProcessingCoordinator::enqueue(const BookId& book, JobKind kind)
{
    m_library.run([book, kind](QSqlDatabase& db) { return catalog::enqueueJob(db, book, kind); })
        .then(this, [this, book](const Result<JobRecord>& job) {
            if (!job) {
                emit enqueueFailed(book, job.error().message);
                return;
            }
            emit jobChanged(job.value());
            start();
        });
}

void ProcessingCoordinator::enqueueExport(const BookId& book, const QString& destination, bool replaceExisting)
{
    struct Queued {
        ExportRecord record;
        JobRecord job;
    };
    m_library
        .run([book, destination, replaceExisting, layout = m_layout](QSqlDatabase& db) -> Result<Queued> {
            auto files = catalog::protectedFiles(db);
            if (!files)
                return files.error();
            auto checked = storage::validateExportDestination(
                destination, layout, destinationRules(layout, files.value(), replaceExisting));
            if (!checked)
                return checked.error();
            auto record = catalog::enqueueExport(db, book, checked.value(), replaceExisting);
            if (!record)
                return record.error();
            auto job = catalog::job(db, record.value().job);
            if (!job)
                return job.error();
            return Queued{record.value(), job.value()};
        })
        .then(this, [this, book](const Result<Queued>& queued) {
            if (!queued) {
                emit exportRefused(book, queued.error().message);
                return;
            }
            emit exportQueued(queued.value().record);
            emit jobChanged(queued.value().job);
            start();
        });
}

void ProcessingCoordinator::cancelJob(const JobId& id)
{
    // Raise a claimed job's flag at once so its SDK call stops early...
    m_flags->raiseExisting(id);
    // ...and again where the cancel is recorded: the worker may claim the job
    // after the line above but before this task runs.
    m_library.run([id, flags = m_flags](QSqlDatabase& db) {
                 auto job = catalog::requestJobCancel(db, id);
                 if (job && job.value().state == JobState::CancelRequested)
                     flags->raise(id);
                 return job;
             })
        .then(this, [this](const Result<JobRecord>& job) {
            if (job)
                emit jobChanged(job.value());
        });
}

void ProcessingCoordinator::cancelAll()
{
    m_flags->raiseAll();
    m_library.run([flags = m_flags](QSqlDatabase& db) -> QList<JobRecord> {
                 QList<JobRecord> changed;
                 auto open = catalog::listJobs(db, true, -1);
                 if (!open)
                     return changed;
                 for (const JobRecord& j : open.value()) {
                     if (auto after = catalog::requestJobCancel(db, j.id)) {
                         if (after.value().state == JobState::CancelRequested)
                             flags->raise(j.id);  // See cancelJob().
                         changed << after.value();
                     }
                 }
                 return changed;
             })
        .then(this, [this](const QList<JobRecord>& changed) {
            for (const JobRecord& j : changed)
                emit jobChanged(j);
        });
}

void ProcessingCoordinator::start()
{
    if (m_stop.load())
        return;
    m_wake.store(true);
    if (!m_running.exchange(true)) {
        emit busyChanged();
        m_pool.start([this] { runLoop(); });
    }
}

void ProcessingCoordinator::runLoop()
{
    for (;;) {
        m_wake.store(false);
        bool more = true;
        while (more && !m_stop.load()) {
            // Worker boundary: nothing may escape into the pool. A job left
            // Running here is closed by recover() in the next session.
            try {
                more = processNext();
            } catch (const std::exception& e) {
                qWarning("Processing worker stopped by an unexpected error: %s", e.what());
                more = false;
            } catch (...) {
                qWarning("Processing worker stopped by an unexpected error.");
                more = false;
            }
        }
        m_running.store(false);
        // A start() that raced with the last empty check set m_wake: go again.
        if (m_stop.load() || !m_wake.load() || m_running.exchange(true)) {
            QMetaObject::invokeMethod(this, [this] {
                emit busyChanged();
                if (!m_running.load())
                    emit idle();
            }, Qt::QueuedConnection);
            return;
        }
    }
}

bool ProcessingCoordinator::processNext()
{
    auto claimed = wait(m_library.run([](QSqlDatabase& db) { return catalog::claimNextJob(db); }));
    if (!claimed || !claimed.value())
        return false;
    const JobRecord first = *claimed.value();

    // A metadata job whose book also waits for its contents: serve both with
    // one SDK call, so the pages are read and OCR'd once.
    std::optional<JobRecord> paired;
    if (first.kind == JobKind::Metadata && m_contents) {
        auto other = wait(m_library.run([book = first.book](QSqlDatabase& db) {
            return catalog::claimQueuedJob(db, book, JobKind::Toc);
        }));
        if (other && other.value())
            paired = *other.value();
    }
    QList<JobRecord> jobs{first};
    if (paired)
        jobs << *paired;
    for (const JobRecord& job : jobs)
        m_flags->get(job.id);
    const auto dropFlags = qScopeGuard([&] {
        for (const JobRecord& job : jobs)
            m_flags->drop(job.id);
    });
    for (const JobRecord& job : jobs)
        emitJob(job.id);

    // An exception fails whichever jobs are still open (finishJob leaves
    // closed ones unchanged).
    const auto failAll = [&](const QString& error) {
        for (const JobRecord& job : jobs)
            finishJob(job, JobState::Failed, QStringLiteral("exception"), error);
    };
    try {
        auto details = wait(m_library.run([book = first.book](QSqlDatabase& db) { return catalog::bookDetails(db, book); }));
        if (!details) {
            for (const JobRecord& job : jobs)
                finishJob(job, JobState::Failed, QStringLiteral("book_unavailable"), details.error().message);
        } else {
            const QString pdf = m_layout.absolute(details.value().asset.managedPath);
            if (first.kind == JobKind::Export)
                runExportJob(first, pdf);
            else if (paired)
                runBookJobs(first, *paired, pdf);
            else if (first.kind == JobKind::Metadata)
                runMetadataJob(first, pdf);
            else if (m_contents)
                runContentsJob(first, pdf);
            else
                finishJob(first, JobState::Failed, QStringLiteral("unsupported"),
                          QStringLiteral("Contents analysis is not available in this build."));
        }
    } catch (const std::exception& e) {
        failAll(QStringLiteral("Unexpected error: %1").arg(QString::fromLocal8Bit(e.what())));
    } catch (...) {
        failAll(QStringLiteral("Unexpected error."));
    }
    for (const JobRecord& job : jobs)
        emitJob(job.id);
    return true;
}

void ProcessingCoordinator::runMetadataJob(const JobRecord& job, const QString& pdf)
{
    // The SDK call: the long-running step, on this worker thread.
    const MetadataExtraction result = m_metadata->extract(pdf, *m_flags->get(job.id));
    finishMetadata(job, result);
}

void ProcessingCoordinator::runContentsJob(const JobRecord& job, const QString& pdf)
{
    const ContentsAnalysis result = m_contents->analyze(pdf, *m_flags->get(job.id), progressFor(job, job));
    finishContents(job, result);
}

void ProcessingCoordinator::runBookJobs(const JobRecord& metadata, const JobRecord& contents, const QString& pdf)
{
    // One call, two jobs: the call is cancelled only when every job that still
    // needs it is cancelled (or at shutdown). Each job then closes on its own.
    const auto runFlag = m_flags->group({metadata.id, contents.id});
    bool metadataClosed = false;
    const auto closeMetadata = [&](const MetadataExtraction& result) {
        if (metadataClosed)
            return;
        metadataClosed = true;
        // Runs inside the SDK's callback: nothing may propagate into it.
        try {
            finishMetadata(metadata, result);
        } catch (const std::exception& e) {
            finishJob(metadata, JobState::Failed, QStringLiteral("exception"),
                      QStringLiteral("Unexpected error: %1").arg(QString::fromLocal8Bit(e.what())));
        } catch (...) {
            finishJob(metadata, JobState::Failed, QStringLiteral("exception"), QStringLiteral("Unexpected error."));
        }
        m_flags->settle(metadata.id);
        emitJob(metadata.id);
    };

    const ContentsAnalysis result = m_contents->analyzeBook(pdf, *runFlag, progressFor(metadata, contents), closeMetadata);
    if (!metadataClosed) {  // The analyzer promises one callback; never leave the job running.
        MetadataExtraction missing;
        missing.status = result.status == ContentsAnalysis::Status::Cancelled ? MetadataExtraction::Status::Cancelled
                                                                              : MetadataExtraction::Status::Failed;
        missing.error = result.error.isEmpty() ? QStringLiteral("The analysis ended before the metadata was ready.")
                                               : result.error;
        closeMetadata(missing);
    }
    finishContents(contents, result);
}

void ProcessingCoordinator::runExportJob(const JobRecord& job, const QString& pdf)
{
    auto record = wait(m_library.run([id = job.id](QSqlDatabase& db) { return catalog::exportRecord(db, id); }));
    if (!record) {
        finishExport(job, JobState::Failed, QStringLiteral("record_unavailable"), record.error().message, {});
        return;
    }
    if (!m_exporter) {
        finishExport(job, JobState::Failed, QStringLiteral("unsupported"),
                     QStringLiteral("Writing bookmarked copies is not available in this build."), {});
        return;
    }
    // Again right before the write: the folder, or a file in it, may have
    // changed since the request.
    auto files = wait(m_library.run([](QSqlDatabase& db) { return catalog::protectedFiles(db); }));
    if (!files) {
        finishExport(job, JobState::Failed, QStringLiteral("export_failed"), files.error().message, {});
        return;
    }
    const auto destination = storage::validateExportDestination(
        record.value().destination, m_layout,
        destinationRules(m_layout, files.value(), record.value().replaceExisting));
    if (!destination) {
        finishExport(job, JobState::Failed,
                     destination.error().code == ErrorCode::Duplicate ? QStringLiteral("output_exists")
                                                                      : QStringLiteral("destination_refused"),
                     destination.error().message, {});
        return;
    }

    // The SDK call, on this worker thread. It verifies the copy before it
    // commits, and refuses a source whose bytes differ from the plan's digest.
    const ExportResult result = m_exporter->exportCopy(pdf, destination.value(), record.value().plan,
                                                       record.value().replaceExisting, *m_flags->get(job.id));
    ExportOutput output;
    output.committed = result.committed;
    output.outputSha256 = result.outputSha256;
    output.outlineItems = result.outlineItems;
    output.pageCount = result.pageCount;
    output.structureMatches = result.structureMatches;
    output.sourceUnchanged = result.sourceUnchanged;
    output.sdkVersion = result.sdkVersion;
    output.sdkPlanJson = result.planJson;
    if (result.committed) {  // Written: a cancel or shutdown after the commit changes nothing.
        finishExport(job, JobState::Succeeded, QStringLiteral("written"), {}, output);
        return;
    }
    if (m_stop.load()) {
        finishExport(job, JobState::Interrupted, QStringLiteral("interrupted"),
                     QStringLiteral("The application closed before the copy was written."), output);
        return;
    }
    // A cancel recorded while the SDK ran (by the user or the trash) wins over
    // how the call ended; finishExportJob keeps a "trashed" reason.
    const auto current = wait(m_library.run([id = job.id](QSqlDatabase& db) { return catalog::job(db, id); }));
    if (result.status == ExportResult::Status::Cancelled || m_flags->get(job.id)->load()
        || (current && current.value().state == JobState::CancelRequested)) {
        finishExport(job, JobState::Cancelled, QStringLiteral("cancelled"),
                     QStringLiteral("Cancelled; no copy was written."), output);
        return;
    }
    QString error = result.error;
    if (!result.planIssues.isEmpty())
        error += QStringLiteral(" (%1)").arg(result.planIssues.join(QStringLiteral("; ")));
    finishExport(job, JobState::Failed, exportFailureOutcome(result.errorCode), error, output);
}

void ProcessingCoordinator::finishExport(const JobRecord& job, JobState state, const QString& outcome,
                                         const QString& error, const ExportOutput& output)
{
    auto finished = wait(m_library.run([id = job.id, state, outcome, error, output](QSqlDatabase& db) -> Result<ExportRecord> {
        if (auto s = catalog::finishExportJob(db, id, state, outcome, error, output); !s)
            return s.error();
        return catalog::exportRecord(db, id);
    }));
    if (!finished) {
        qWarning("Export %s could not be recorded: %s", qPrintable(job.id.toString()),
                 qPrintable(finished.error().message));
        return;
    }
    QMetaObject::invokeMethod(this, [this, record = finished.value()] { emit exportFinished(record); },
                              Qt::QueuedConnection);
}

ContentsAnalyzer::Progress ProcessingCoordinator::progressFor(const JobRecord& metadata, const JobRecord& contents)
{
    // At most about four updates a second, plus every change of stage. The
    // "metadata" stage of a combined run belongs to the metadata job.
    struct Throttle {
        QElapsedTimer sinceLast;
        QString stage;
    };
    auto throttle = std::make_shared<Throttle>();
    return [this, throttle, metadataId = metadata.id, contentsId = contents.id](const QString& stage, int pages) {
        if (stage == throttle->stage && throttle->sinceLast.isValid() && throttle->sinceLast.elapsed() < 250)
            return;
        throttle->stage = stage;
        throttle->sinceLast.restart();
        const JobId id = stage == QLatin1String("metadata") ? metadataId : contentsId;
        QMetaObject::invokeMethod(this, [this, id, stage, pages] { emit jobProgress(id, stage, pages); },
                                  Qt::QueuedConnection);
    };
}

void ProcessingCoordinator::finishMetadata(const JobRecord& job, const MetadataExtraction& result)
{
    // Read once: stop() sets m_stop before raising the flags, so a call stopped
    // by shutdown is always seen as such here.
    const bool stopping = m_stop.load();
    if (stopping && result.status != MetadataExtraction::Status::Completed) {
        // Stopped by shutdown, not by the user: requeue it for the next session.
        wait(m_library.run([id = job.id](QSqlDatabase& db) { return catalog::interruptJob(db, id); }));
        return;
    }
    if (!stopping && (result.status == MetadataExtraction::Status::Cancelled || m_flags->get(job.id)->load())) {
        finishJob(job, JobState::Cancelled, QStringLiteral("cancelled"), QStringLiteral("Cancelled."));
        return;
    }
    if (result.status == MetadataExtraction::Status::Failed) {
        finishJob(job, JobState::Failed, QStringLiteral("sdk_error"), result.error);
        return;
    }
    if (result.sourceSha256.compare(job.sourceSha256, Qt::CaseInsensitive) != 0) {
        finishJob(job, JobState::Failed, QStringLiteral("source_mismatch"),
                  QStringLiteral("The managed copy no longer matches the imported file."));
        return;
    }

    const RunId runId = RunId::create();
    auto report = storage::writeReport(m_layout, runId, result.reportJson);
    if (!report) {
        finishJob(job, JobState::Failed, QStringLiteral("report_write_failed"), report.error().message);
        return;
    }
    const RunIdentity identity = identityOf(result.sourceSha256, result.sdkVersion, result.modelIdentity,
                                            result.optionsJson, result.outcome, report.value());
    auto published = wait(m_library.run([job, runId, identity, result](QSqlDatabase& db) {
        return catalog::completeMetadataJob(db, job, runId, identity, result.metadata, result.details, result.pageCount);
    }));
    if (published) {
        QMetaObject::invokeMethod(this, [this, book = job.book, runId] { emit metadataPublished(book, runId); },
                                  Qt::QueuedConnection);
        return;
    }
    storage::removeUnpublishedReport(m_layout, runId);  // It belongs to no run.
    closeRefused(job, published.error());
}

void ProcessingCoordinator::finishContents(const JobRecord& job, const ContentsAnalysis& result)
{
    const bool stopping = m_stop.load();  // See finishMetadata().
    if (stopping && result.status != ContentsAnalysis::Status::Completed) {
        wait(m_library.run([id = job.id](QSqlDatabase& db) { return catalog::interruptJob(db, id); }));
        return;
    }
    if (!stopping && (result.status == ContentsAnalysis::Status::Cancelled || m_flags->get(job.id)->load())) {
        finishJob(job, JobState::Cancelled, QStringLiteral("cancelled"), QStringLiteral("Cancelled."));
        return;
    }
    if (result.status == ContentsAnalysis::Status::Failed) {
        finishJob(job, JobState::Failed, QStringLiteral("sdk_error"), result.error);
        return;
    }
    if (result.sourceSha256.compare(job.sourceSha256, Qt::CaseInsensitive) != 0) {
        finishJob(job, JobState::Failed, QStringLiteral("source_mismatch"),
                  QStringLiteral("The managed copy no longer matches the imported file."));
        return;
    }

    const RunId runId = RunId::create();
    auto report = storage::writeReport(m_layout, runId, result.reportJson);
    if (!report) {
        finishJob(job, JobState::Failed, QStringLiteral("report_write_failed"), report.error().message);
        return;
    }
    const RunIdentity identity = identityOf(result.sourceSha256, result.sdkVersion, result.modelIdentity,
                                            result.optionsJson, result.outcome, report.value());
    auto published = wait(m_library.run([job, runId, identity, result](QSqlDatabase& db) {
        return catalog::completeTocJob(db, job, runId, identity, result.toc, result.pageCount);
    }));
    if (published) {
        QMetaObject::invokeMethod(this, [this, book = job.book, runId] { emit contentsPublished(book, runId); },
                                  Qt::QueuedConnection);
        return;
    }
    storage::removeUnpublishedReport(m_layout, runId);
    closeRefused(job, published.error());
}

void ProcessingCoordinator::finishJob(const JobRecord& job, JobState state, const QString& outcome, const QString& error)
{
    wait(m_library.run([id = job.id, state, outcome, error](QSqlDatabase& db) {
        return catalog::finishJob(db, id, state, outcome, error);
    }));
}

void ProcessingCoordinator::closeRefused(const JobRecord& job, const Error& error)
{
    switch (error.code) {
    case ErrorCode::StaleGeneration:
        finishJob(job, JobState::Cancelled, QStringLiteral("superseded"),
                  QStringLiteral("A newer request replaced this one; its result was not used."));
        return;
    case ErrorCode::Trashed:
        finishJob(job, JobState::Cancelled, QStringLiteral("trashed"), QStringLiteral("The book was moved to Trash."));
        return;
    case ErrorCode::InvalidArgument: {
        // Either the job was cancelled while the SDK ran (it is no longer
        // running), or the result itself was rejected (e.g. a contents entry
        // pointing past the last page): only the first is a cancellation.
        const auto current = wait(m_library.run([id = job.id](QSqlDatabase& db) { return catalog::job(db, id); }));
        if (current && current.value().state == JobState::CancelRequested)
            finishJob(job, JobState::Cancelled, QStringLiteral("cancelled"), QStringLiteral("Cancelled."));
        else
            finishJob(job, JobState::Failed, QStringLiteral("publish_failed"), error.message);
        return;
    }
    default:
        finishJob(job, JobState::Failed,
                  error.code == ErrorCode::SourceMismatch ? QStringLiteral("source_mismatch")
                                                          : QStringLiteral("publish_failed"),
                  error.message);
        return;
    }
}

void ProcessingCoordinator::emitJob(const JobId& id)
{
    auto job = wait(m_library.run([id](QSqlDatabase& db) { return catalog::job(db, id); }));
    if (!job)
        return;
    QMetaObject::invokeMethod(this, [this, record = job.value()] { emit jobChanged(record); }, Qt::QueuedConnection);
}

std::shared_ptr<std::atomic_bool> CancelFlags::getLocked(const JobId& id)
{
    auto& flag = m_flags[id];
    if (!flag)
        flag = std::make_shared<std::atomic_bool>(m_stopped);
    return flag;
}

void CancelFlags::evaluateLocked(Group& group)
{
    if (group.open.isEmpty())
        return;  // Every member has its result; the call is ending anyway.
    for (const JobId& id : std::as_const(group.open)) {
        const auto flag = m_flags.value(id);
        if (!flag || !flag->load())
            return;  // Someone still needs the call.
    }
    group.flag->store(true);
}

void CancelFlags::evaluateGroupOfLocked(const JobId& id)
{
    if (const auto group = m_groups.value(id))
        evaluateLocked(*group);
}

std::shared_ptr<std::atomic_bool> CancelFlags::get(const JobId& id)
{
    QMutexLocker lock(&m_mutex);
    return getLocked(id);
}

void CancelFlags::raise(const JobId& id)
{
    QMutexLocker lock(&m_mutex);
    getLocked(id)->store(true);
    evaluateGroupOfLocked(id);
}

void CancelFlags::raiseExisting(const JobId& id)
{
    QMutexLocker lock(&m_mutex);
    if (const auto flag = m_flags.value(id)) {
        flag->store(true);
        evaluateGroupOfLocked(id);
    }
}

void CancelFlags::raiseAll()
{
    QMutexLocker lock(&m_mutex);
    for (const auto& flag : std::as_const(m_flags))
        flag->store(true);
    for (const auto& group : std::as_const(m_groups))
        evaluateLocked(*group);
}

void CancelFlags::stopAll()
{
    QMutexLocker lock(&m_mutex);
    m_stopped = true;
    for (const auto& flag : std::as_const(m_flags))
        flag->store(true);
    for (const auto& group : std::as_const(m_groups))
        group->flag->store(true);
}

void CancelFlags::drop(const JobId& id)
{
    QMutexLocker lock(&m_mutex);
    m_flags.remove(id);
    m_groups.remove(id);
}

std::shared_ptr<std::atomic_bool> CancelFlags::group(const QList<JobId>& members)
{
    QMutexLocker lock(&m_mutex);
    auto group = std::make_shared<Group>();
    group->flag = std::make_shared<std::atomic_bool>(m_stopped);
    for (const JobId& id : members) {
        getLocked(id);
        group->open.insert(id);
        m_groups.insert(id, group);
    }
    evaluateLocked(*group);  // Members may have been cancelled before the call started.
    return group->flag;
}

void CancelFlags::settle(const JobId& id)
{
    QMutexLocker lock(&m_mutex);
    if (const auto group = m_groups.value(id)) {
        group->open.remove(id);
        evaluateLocked(*group);
    }
}

} // namespace mbl::processing
