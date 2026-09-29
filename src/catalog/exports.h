// A2: export requests and their records. An export is a job of kind Export
// (queued, claimed, cancelled and recovered with the other jobs) plus a
// record of what was asked and what was written. A4 (ProcessingCoordinator)
// validates destinations (A1) and runs the SDK.
#pragma once

#include "domain/export.h"
#include "domain/ids.h"
#include "domain/jobs.h"
#include "domain/result.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

namespace mbl::catalog {

// Files an export must never write over: every book's managed source
// (relative to the library root) and imported original (as recorded).
struct ProtectedFiles {
    QStringList managedPaths;
    QStringList originalPaths;
};
domain::Result<ProtectedFiles> protectedFiles(QSqlDatabase& db);

// In one transaction: builds the plan from the book's effective contents
// (its edited revision, else its analysis) and queues an export job with its
// record. The caller has validated `destination`. With `replaceExisting`,
// `confirmedFile` is the file there that the user agreed to replace (nullopt:
// none was there); it is recorded so that a different file is never replaced.
// Fails with NotFound, Trashed, InvalidArgument (no contents, or nothing to
// bookmark), or Duplicate when an export of the book is already waiting or
// running.
domain::Result<domain::ExportRecord> enqueueExport(QSqlDatabase& db, const domain::BookId& book,
                                                   const QString& destination, bool replaceExisting,
                                                   const std::optional<domain::FileIdentity>& confirmedFile = std::nullopt);

// In one transaction: closes a Running or CancelRequested export job with
// what the SDK reported. A committed copy always closes Succeeded ("written"),
// even when a cancel or the trash came after the commit; `state` is then
// ignored. Otherwise `state` must be Failed, Cancelled or Interrupted, and a
// job ended by the trash keeps its "trashed" outcome when it is Cancelled.
domain::Status finishExportJob(QSqlDatabase& db, const domain::JobId& job, domain::JobState state,
                               const QString& outcome, const QString& error, const domain::ExportOutput& output);

domain::Result<domain::ExportRecord> exportRecord(QSqlDatabase& db, const domain::JobId& job);
// Newest first.
domain::Result<QList<domain::ExportRecord>> bookExports(QSqlDatabase& db, const domain::BookId& book);

} // namespace mbl::catalog
