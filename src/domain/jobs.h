// Durable processing jobs. Job state (whether work is waiting, running or
// finished) is separate from the book's available content: a failed or
// cancelled job never removes previously published results.
#pragma once

#include "domain/ids.h"

#include <QDateTime>
#include <QMetaType>
#include <QString>

#include <optional>

namespace mbl::domain {

// Export: writing a bookmarked copy (what was asked and written is in its
// ExportRecord). It has no request generation or run, and is never requeued
// by itself: the user asks again.
enum class JobKind { Metadata, Toc, Export };

// Queued -> Running -> Succeeded | Failed | Cancelled
// Running -> CancelRequested -> Cancelled (a job cancelled while its SDK call
//            ran never publishes, even if the call completed)
// Running | CancelRequested -> Interrupted (the process ended; recovery
//            queues a replacement job).
enum class JobState { Queued, Running, CancelRequested, Succeeded, Failed, Cancelled, Interrupted };

bool isOpen(JobState state);  // Queued, Running or CancelRequested.

struct JobRecord {
    JobId id;
    BookId book;
    JobKind kind = JobKind::Metadata;
    JobState state = JobState::Queued;
    qint64 generation = 0;    // Component request generation captured at enqueue (0 for exports).
    QString sourceSha256;     // Asset digest captured at enqueue.
    int attempt = 0;          // Incremented each time the job starts running.
    QString outcome;          // Stable code, e.g. "published", "superseded", "trashed", "sdk_error".
    QString error;            // Plain-language reason for Failed/Cancelled/Interrupted.
    std::optional<RunId> run; // Published run, for a Succeeded metadata or contents job.
    QDateTime createdAt;
    QDateTime updatedAt;
};

QString toCode(JobKind kind);
std::optional<JobKind> jobKindFromCode(const QString& code);
QString toCode(JobState state);
std::optional<JobState> jobStateFromCode(const QString& code);

} // namespace mbl::domain

Q_DECLARE_METATYPE(mbl::domain::JobRecord)
