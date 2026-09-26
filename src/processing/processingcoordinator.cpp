#include "processing/processingcoordinator.h"

#include "catalog/catalog.h"
#include "catalog/jobs.h"
#include "catalog/library.h"
#include "storage/reportstore.h"

#include <QMetaObject>
#include <QMutexLocker>

#include <exception>

namespace mbl::processing {

using namespace mbl::domain;

namespace {

template <typename T>
T wait(QFuture<T> future)
{
    return future.result();
}

} // namespace

ProcessingCoordinator::ProcessingCoordinator(catalog::Library& library, std::shared_ptr<MetadataExtractor> metadata,
                                             QObject* parent)
    : QObject(parent), m_library(library), m_layout(library.rootDir()), m_metadata(std::move(metadata))
{
    m_pool.setMaxThreadCount(1);
    m_pool.setObjectName(QStringLiteral("mbl-processing"));
}

ProcessingCoordinator::~ProcessingCoordinator()
{
    m_stop.store(true);
    {
        QMutexLocker lock(&m_flagsMutex);
        for (const auto& flag : std::as_const(m_flags))
            flag->store(true);
    }
    m_pool.waitForDone();
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
    m_library.run([book](QSqlDatabase& db) { return catalog::enqueueJob(db, book, JobKind::Metadata); })
        .then(this, [this, book](const Result<JobRecord>& job) {
            if (!job) {
                emit enqueueFailed(book, job.error().message);
                return;
            }
            emit jobChanged(job.value());
            start();
        });
}

void ProcessingCoordinator::cancelJob(const JobId& id)
{
    // Set the flag first so a running SDK call stops as soon as possible.
    {
        QMutexLocker lock(&m_flagsMutex);
        if (const auto flag = m_flags.value(id))
            flag->store(true);
    }
    m_library.run([id](QSqlDatabase& db) { return catalog::requestJobCancel(db, id); })
        .then(this, [this](const Result<JobRecord>& job) {
            if (job)
                emit jobChanged(job.value());
        });
}

void ProcessingCoordinator::cancelAll()
{
    {
        QMutexLocker lock(&m_flagsMutex);
        for (const auto& flag : std::as_const(m_flags))
            flag->store(true);
    }
    m_library.run([](QSqlDatabase& db) -> QList<JobRecord> {
                 QList<JobRecord> changed;
                 auto open = catalog::listJobs(db, true);
                 if (!open)
                     return changed;
                 for (const JobRecord& j : open.value()) {
                     if (auto after = catalog::requestJobCancel(db, j.id))
                         changed << after.value();
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
        while (!m_stop.load() && processNext()) {
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
    const JobRecord job = *claimed.value();
    flagFor(job.id);
    emitJob(job.id);
    try {
        switch (job.kind) {
        case JobKind::Metadata:
            runMetadataJob(job);
            break;
        case JobKind::Toc:
            wait(m_library.run([id = job.id](QSqlDatabase& db) {
                return catalog::finishJob(db, id, JobState::Failed, QStringLiteral("unsupported"),
                                          QStringLiteral("Contents analysis is not available yet."));
            }));
            break;
        }
    } catch (const std::exception& e) {
        const QString error = QStringLiteral("Unexpected error: %1").arg(QString::fromLocal8Bit(e.what()));
        wait(m_library.run([id = job.id, error](QSqlDatabase& db) {
            return catalog::finishJob(db, id, JobState::Failed, QStringLiteral("exception"), error);
        }));
    } catch (...) {
        wait(m_library.run([id = job.id](QSqlDatabase& db) {
            return catalog::finishJob(db, id, JobState::Failed, QStringLiteral("exception"),
                                      QStringLiteral("Unexpected error."));
        }));
    }
    dropFlag(job.id);
    emitJob(job.id);
    return true;
}

void ProcessingCoordinator::runMetadataJob(const JobRecord& job)
{
    const auto finish = [this, id = job.id](JobState state, const QString& outcome, const QString& error) {
        wait(m_library.run([id, state, outcome, error](QSqlDatabase& db) {
            return catalog::finishJob(db, id, state, outcome, error);
        }));
    };

    auto details = wait(m_library.run([book = job.book](QSqlDatabase& db) { return catalog::bookDetails(db, book); }));
    if (!details) {
        finish(JobState::Failed, QStringLiteral("book_unavailable"), details.error().message);
        return;
    }
    const QString pdf = m_layout.absolute(details.value().asset.managedPath);
    const auto cancel = flagFor(job.id);

    // The SDK call: the only long-running step, on this worker thread.
    const MetadataExtraction result = m_metadata->extract(pdf, *cancel);

    if (result.status == MetadataExtraction::Status::Cancelled || cancel->load()) {
        finish(JobState::Cancelled, QStringLiteral("cancelled"), QStringLiteral("Cancelled."));
        return;
    }
    if (result.status == MetadataExtraction::Status::Failed) {
        finish(JobState::Failed, QStringLiteral("sdk_error"), result.error);
        return;
    }
    if (result.sourceSha256.compare(job.sourceSha256, Qt::CaseInsensitive) != 0) {
        finish(JobState::Failed, QStringLiteral("source_mismatch"),
               QStringLiteral("The managed copy no longer matches the imported file."));
        return;
    }

    const RunId runId = RunId::create();
    auto report = storage::writeReport(m_layout, runId, result.reportJson);
    if (!report) {
        finish(JobState::Failed, QStringLiteral("report_write_failed"), report.error().message);
        return;
    }
    RunIdentity identity;
    identity.sourceSha256 = result.sourceSha256.toLower();
    identity.sdkVersion = result.sdkVersion;
    identity.modelIdentity = result.modelIdentity;
    identity.optionsJson = result.optionsJson;
    identity.outcome = result.outcome;
    identity.reportPath = report.value();

    auto published = wait(m_library.run([job, runId, identity, result](QSqlDatabase& db) {
        return catalog::completeMetadataJob(db, job, runId, identity, result.metadata, result.details, result.pageCount);
    }));
    if (published) {
        QMetaObject::invokeMethod(this, [this, book = job.book, runId] { emit metadataPublished(book, runId); },
                                  Qt::QueuedConnection);
        return;
    }

    // Not published: the report belongs to no run.
    storage::removeUnpublishedReport(m_layout, runId);
    switch (published.error().code) {
    case ErrorCode::StaleGeneration:
        finish(JobState::Cancelled, QStringLiteral("superseded"),
               QStringLiteral("A newer request replaced this one; its result was not used."));
        break;
    case ErrorCode::Trashed:
        finish(JobState::Cancelled, QStringLiteral("trashed"), QStringLiteral("The book was moved to Trash."));
        break;
    case ErrorCode::InvalidArgument:  // The job was cancelled while the SDK ran.
        finish(JobState::Cancelled, QStringLiteral("cancelled"), QStringLiteral("Cancelled."));
        break;
    default:
        finish(JobState::Failed, published.error().code == ErrorCode::SourceMismatch ? QStringLiteral("source_mismatch")
                                                                                     : QStringLiteral("publish_failed"),
               published.error().message);
        break;
    }
}

void ProcessingCoordinator::emitJob(const JobId& id)
{
    auto job = wait(m_library.run([id](QSqlDatabase& db) { return catalog::job(db, id); }));
    if (!job)
        return;
    QMetaObject::invokeMethod(this, [this, record = job.value()] { emit jobChanged(record); }, Qt::QueuedConnection);
}

std::shared_ptr<std::atomic_bool> ProcessingCoordinator::flagFor(const JobId& id)
{
    QMutexLocker lock(&m_flagsMutex);
    auto& flag = m_flags[id];
    if (!flag)
        flag = std::make_shared<std::atomic_bool>(m_stop.load());
    return flag;
}

void ProcessingCoordinator::dropFlag(const JobId& id)
{
    QMutexLocker lock(&m_flagsMutex);
    m_flags.remove(id);
}

} // namespace mbl::processing
