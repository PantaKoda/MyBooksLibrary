// A2: durable processing jobs and the transactional publication of their
// results. A4 (processing::ProcessingCoordinator) drives these functions from
// its worker through the database thread.
#pragma once

#include "domain/book.h"
#include "domain/ids.h"
#include "domain/jobs.h"
#include "domain/metadata.h"
#include "domain/result.h"
#include "domain/toc.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include <optional>

namespace mbl::catalog {

// Queues a job of `kind` for the book, starting a new request generation for
// that component. If a queued or running job of that kind already exists,
// returns it unchanged (no new generation). A job whose cancel was requested
// does not count: the new request supersedes it. Fails with Trashed for a
// trashed book.
domain::Result<domain::JobRecord> enqueueJob(QSqlDatabase& db, const domain::BookId& book, domain::JobKind kind);

// Takes the next queued job (metadata before TOC, then oldest first), marks it
// Running and increments its attempt. Queued jobs of trashed books are
// cancelled on the way. Returns nullopt when nothing is queued.
domain::Result<std::optional<domain::JobRecord>> claimNextJob(QSqlDatabase& db);

// Queued -> Cancelled at once; Running -> CancelRequested (the worker then
// ends it). Terminal jobs are left unchanged. Returns the job as stored.
domain::Result<domain::JobRecord> requestJobCancel(QSqlDatabase& db, const domain::JobId& id);

// Running or CancelRequested -> a terminal state other than Succeeded.
domain::Status finishJob(QSqlDatabase& db, const domain::JobId& id, domain::JobState state,
                         const QString& outcome, const QString& error);

// In one transaction: publishes the metadata run (generation, lifecycle and
// digest checks), stores its field details, records the page count if it was
// unknown, and marks the job Succeeded. Refused, changing nothing, if the job
// is no longer Running (e.g. CancelRequested) or publication is refused
// (StaleGeneration, Trashed, SourceMismatch).
domain::Result<domain::RunId> completeMetadataJob(QSqlDatabase& db, const domain::JobRecord& job,
                                                  const domain::RunId& runId, const domain::RunIdentity& run,
                                                  const domain::ExtractedMetadata& metadata,
                                                  const QList<domain::MetadataFieldDetail>& details,
                                                  std::optional<int> pageCount);

// completeMetadataJob for a contents (TOC) job: records the page count if it
// was unknown (before the destinations are checked against it), publishes
// every parsed entry with its evidence, and marks the job Succeeded, in one
// transaction. Refused, changing nothing, like completeMetadataJob.
domain::Result<domain::RunId> completeTocJob(QSqlDatabase& db, const domain::JobRecord& job,
                                             const domain::RunId& runId, const domain::RunIdentity& run,
                                             const domain::TocAnalysis& toc, std::optional<int> pageCount);

// Claims the book's queued job of `kind`, if any (Running, attempt + 1), so the
// worker can serve it in the same SDK run as a job it already claimed.
domain::Result<std::optional<domain::JobRecord>> claimQueuedJob(QSqlDatabase& db, const domain::BookId& book,
                                                                domain::JobKind kind);

struct JobRecovery {
    int interrupted = 0;  // Running jobs closed as Interrupted...
    int requeued = 0;     // ...and replaced by a new queued job.
    int cancelled = 0;    // CancelRequested jobs closed as Cancelled.
};
// Run at startup, before the worker starts.
domain::Result<JobRecovery> recoverJobs(QSqlDatabase& db);

// Shutdown: closes a job whose SDK call was stopped because the application
// is closing, as recovery would after a crash (Running -> Interrupted plus a
// requeued job; CancelRequested -> Cancelled). Terminal jobs are unchanged.
domain::Result<JobRecovery> interruptJob(QSqlDatabase& db, const domain::JobId& id);

// The most recent job of each book and kind, whatever its state: what the
// library view shows as a book's processing state.
domain::Result<QList<domain::JobRecord>> latestJobs(QSqlDatabase& db);

// Report paths (relative to the library root) that metadata and TOC runs
// reference. Any other file in reports/ belongs to no run.
domain::Result<QStringList> referencedReportPaths(QSqlDatabase& db);

domain::Result<domain::JobRecord> job(QSqlDatabase& db, const domain::JobId& id);
// Newest first. `openOnly` restricts to Queued, Running and CancelRequested.
// A negative `limit` returns every matching job.
domain::Result<QList<domain::JobRecord>> listJobs(QSqlDatabase& db, bool openOnly, int limit = 200);

domain::Result<QList<domain::MetadataFieldDetail>> metadataDetails(QSqlDatabase& db, const domain::RunId& run);

} // namespace mbl::catalog
