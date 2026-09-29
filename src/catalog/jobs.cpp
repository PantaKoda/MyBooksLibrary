#include "catalog/jobs.h"

#include "catalog/catalog_internal.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QVariant>

namespace mbl::catalog {

using namespace mbl::domain;
using detail::now;
using detail::sqlError;
using detail::Transaction;

namespace {

const QString kColumns = QStringLiteral(
    "id, book_id, kind, state, generation, source_sha256, attempt, outcome, error, run_id, created_at, updated_at");

JobRecord readJob(const QSqlQuery& q)
{
    JobRecord j;
    j.id = JobId::fromString(q.value(0).toString());
    j.book = BookId::fromString(q.value(1).toString());
    j.kind = jobKindFromCode(q.value(2).toString()).value_or(JobKind::Metadata);
    j.state = jobStateFromCode(q.value(3).toString()).value_or(JobState::Failed);
    j.generation = q.value(4).toLongLong();
    j.sourceSha256 = q.value(5).toString();
    j.attempt = q.value(6).toInt();
    j.outcome = q.value(7).toString();
    j.error = q.value(8).toString();
    if (!q.value(9).isNull())
        j.run = RunId::fromString(q.value(9).toString());
    j.createdAt = QDateTime::fromString(q.value(10).toString(), Qt::ISODateWithMs);
    j.updatedAt = QDateTime::fromString(q.value(11).toString(), Qt::ISODateWithMs);
    return j;
}

QVariant textOrNull(const QString& text)
{
    return text.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(text);
}

// The pending request (queued or running) of that kind, if any. A job whose
// cancel was requested is ending and never publishes, so it does not count.
Result<std::optional<JobRecord>> openJobFor(QSqlDatabase& db, const BookId& book, JobKind kind)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT %1 FROM jobs WHERE book_id = ? AND kind = ? "
                             "AND state IN ('queued', 'running')")
                  .arg(kColumns));
    q.addBindValue(book.toString());
    q.addBindValue(toCode(kind));
    if (!q.exec())
        return sqlError(q);
    if (!q.next())
        return std::optional<JobRecord>();
    return std::optional<JobRecord>(readJob(q));
}

Result<JobRecord> insertQueuedJob(QSqlDatabase& db, const PublishTicket& ticket, JobKind kind)
{
    const JobId id = JobId::create();
    const QString stamp = now();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO jobs(id, book_id, kind, state, generation, source_sha256, created_at, "
                             "updated_at) VALUES (?, ?, ?, 'queued', ?, ?, ?, ?)"));
    q.addBindValue(id.toString());
    q.addBindValue(ticket.book.toString());
    q.addBindValue(toCode(kind));
    q.addBindValue(ticket.generation);
    q.addBindValue(ticket.sourceSha256);
    q.addBindValue(stamp);
    q.addBindValue(stamp);
    if (!q.exec())
        return sqlError(q);
    return job(db, id);
}

Status transition(QSqlDatabase& db, const JobId& id, std::initializer_list<JobState> from, JobState to,
                  const QString& outcome, const QString& error, const std::optional<RunId>& run = std::nullopt)
{
    QStringList states;
    for (JobState s : from)
        states << QStringLiteral("'%1'").arg(toCode(s));
    // The right-hand side of SET reads the old row, so the new time is bound twice.
    const QString stamp = now();
    const QVariant finished = isOpen(to) ? QVariant(QMetaType(QMetaType::QString)) : QVariant(stamp);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE jobs SET state = ?, outcome = ?, error = ?, run_id = COALESCE(?, run_id), "
                             "updated_at = ?, finished_at = COALESCE(?, finished_at) WHERE id = ? AND state IN (%1)")
                  .arg(states.join(u',')));
    q.addBindValue(toCode(to));
    q.addBindValue(textOrNull(outcome));
    q.addBindValue(textOrNull(error));
    q.addBindValue(run ? QVariant(run->toString()) : QVariant(QMetaType(QMetaType::QString)));
    q.addBindValue(stamp);
    q.addBindValue(finished);
    q.addBindValue(id.toString());
    if (!q.exec())
        return sqlError(q);
    if (q.numRowsAffected() != 1)
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Job %1 is not in the expected state for %2.").arg(id.toString(), toCode(to)));
    return Done{};
}

// JSON shape: evidence [{page, text, reason}], alternatives [{value, score, evidence, reasons}], reasons [..].
QJsonArray evidenceJson(const QList<MetadataEvidence>& evidence)
{
    QJsonArray out;
    for (const MetadataEvidence& e : evidence)
        out.append(QJsonObject{{QStringLiteral("page"), e.pageIndex},
                               {QStringLiteral("text"), e.text},
                               {QStringLiteral("reason"), e.reason}});
    return out;
}

QList<MetadataEvidence> evidenceFromJson(const QJsonArray& array)
{
    QList<MetadataEvidence> out;
    for (const QJsonValue& v : array) {
        const QJsonObject o = v.toObject();
        out << MetadataEvidence{o.value(QStringLiteral("page")).toInt(), o.value(QStringLiteral("text")).toString(),
                                o.value(QStringLiteral("reason")).toString()};
    }
    return out;
}

QString compact(const QJsonArray& array)
{
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

} // namespace

Status detail::transitionJob(QSqlDatabase& db, const JobId& id, std::initializer_list<JobState> from, JobState to,
                             const QString& outcome, const QString& error, const std::optional<RunId>& run)
{
    return transition(db, id, from, to, outcome, error, run);
}

Result<JobRecord> detail::queueJob(QSqlDatabase& db, const BookId& book, JobKind kind)
{
    if (kind == JobKind::Export)
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("Exports are queued with enqueueExport."));
    auto open = openJobFor(db, book, kind);
    if (!open)
        return open.error();
    if (open.value())
        return *open.value();  // Already waiting or running: nothing new to request.
    auto ticket = detail::bumpGeneration(db, book, kind == JobKind::Metadata);
    if (!ticket)
        return ticket.error();
    return insertQueuedJob(db, ticket.value(), kind);
}

Result<JobRecord> enqueueJob(QSqlDatabase& db, const BookId& book, JobKind kind)
{
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    auto queued = detail::queueJob(db, book, kind);
    if (!queued)
        return queued;
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return queued;
}

Result<std::optional<JobRecord>> claimNextJob(QSqlDatabase& db)
{
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    QSqlQuery q(db);
    // Jobs of trashed books are never run.
    const QString stamp = now();
    q.prepare(QStringLiteral("UPDATE jobs SET state = 'cancelled', outcome = 'trashed', "
                             "error = 'The book was moved to Trash.', updated_at = ?, finished_at = ? "
                             "WHERE state = 'queued' AND book_id IN (SELECT id FROM books WHERE lifecycle = 'trashed')"));
    q.addBindValue(stamp);
    q.addBindValue(stamp);
    if (!q.exec())
        return sqlError(q);
    // Exports first: the user is waiting for them and they take seconds.
    if (!q.exec(QStringLiteral("SELECT id FROM jobs WHERE state = 'queued' ORDER BY CASE kind WHEN 'export' THEN 0 "
                               "WHEN 'metadata' THEN 1 ELSE 2 END, created_at, id LIMIT 1")))
        return sqlError(q);
    if (!q.next()) {
        if (!tx.commit())
            return sqlError(db, QStringLiteral("commit"));
        return std::optional<JobRecord>();
    }
    const JobId id = JobId::fromString(q.value(0).toString());
    q.finish();
    q.prepare(QStringLiteral("UPDATE jobs SET state = 'running', attempt = attempt + 1, started_at = ?, "
                             "updated_at = ? WHERE id = ? AND state = 'queued'"));
    q.addBindValue(stamp);
    q.addBindValue(stamp);
    q.addBindValue(id.toString());
    if (!q.exec())
        return sqlError(q);
    auto claimed = job(db, id);
    if (!claimed)
        return claimed.error();
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return std::optional<JobRecord>(claimed.value());
}

Result<JobRecord> requestJobCancel(QSqlDatabase& db, const JobId& id)
{
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    auto current = job(db, id);
    if (!current)
        return current;
    if (current.value().state == JobState::Queued) {
        if (auto s = transition(db, id, {JobState::Queued}, JobState::Cancelled, QStringLiteral("cancelled"),
                                QStringLiteral("Cancelled before it started."));
            !s)
            return s.error();
    } else if (current.value().state == JobState::Running) {
        if (auto s = transition(db, id, {JobState::Running}, JobState::CancelRequested, {}, {}); !s)
            return s.error();
    }
    auto after = job(db, id);
    if (!after)
        return after;
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return after;
}

Status finishJob(QSqlDatabase& db, const JobId& id, JobState state, const QString& outcome, const QString& error)
{
    if (isOpen(state) || state == JobState::Succeeded)
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("finishJob needs a terminal state other than Succeeded."));
    // A job ended because its book was moved to Trash keeps that reason,
    // whichever way the worker then reports the cancellation.
    auto current = job(db, id);
    if (!current)
        return current.error();
    if (state == JobState::Cancelled && current.value().state == JobState::CancelRequested
        && current.value().outcome == QLatin1String("trashed"))
        return transition(db, id, {JobState::CancelRequested}, state, current.value().outcome, current.value().error);
    return transition(db, id, {JobState::Running, JobState::CancelRequested}, state, outcome, error);
}

namespace {

// Records the page count reported by a run if the asset's is still unknown.
Status fillPageCount(QSqlDatabase& db, const BookId& book, std::optional<int> pageCount)
{
    if (!pageCount)
        return Done{};
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE assets SET page_count = ? WHERE page_count IS NULL AND id = "
                             "(SELECT asset_id FROM books WHERE id = ?)"));
    q.addBindValue(*pageCount);
    q.addBindValue(book.toString());
    if (!q.exec())
        return sqlError(q);
    return Done{};
}

// Publication requires the job to be still running: a cancel requested while
// the SDK ran wins over its result.
Status requireRunning(QSqlDatabase& db, const JobRecord& job)
{
    auto current = catalog::job(db, job.id);
    if (!current)
        return current.error();
    if (current.value().state != JobState::Running) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Job %1 is %2, not running.").arg(job.id.toString(), toCode(current.value().state)));
    }
    return Done{};
}

} // namespace

Result<RunId> completeMetadataJob(QSqlDatabase& db, const JobRecord& job, const RunId& runId, const RunIdentity& run,
                                  const ExtractedMetadata& metadata, const QList<MetadataFieldDetail>& details,
                                  std::optional<int> pageCount)
{
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    if (auto s = requireRunning(db, job); !s)
        return s.error();
    if (job.kind != JobKind::Metadata)
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("Job %1 is not a metadata job.").arg(job.id.toString()));
    const PublishTicket ticket{job.book, job.generation, job.sourceSha256};
    auto published = detail::publishMetadataRun(db, runId, ticket, run, metadata);
    if (!published)
        return published;

    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO metadata_field_details(run_id, field, evidence_json, alternatives_json, "
                             "reasons_json) VALUES (?, ?, ?, ?, ?)"));
    for (const MetadataFieldDetail& d : details) {
        if (d.field == MetadataField::Subtitle)
            continue;  // Part of the title field.
        QJsonArray alternatives;
        for (const MetadataCandidate& c : d.alternatives)
            alternatives.append(QJsonObject{{QStringLiteral("value"), c.value},
                                            {QStringLiteral("score"), c.score},
                                            {QStringLiteral("evidence"), evidenceJson(c.evidence)},
                                            {QStringLiteral("reasons"), QJsonArray::fromStringList(c.reasons)}});
        q.addBindValue(runId.toString());
        q.addBindValue(toCode(d.field));
        q.addBindValue(compact(evidenceJson(d.evidence)));
        q.addBindValue(compact(alternatives));
        q.addBindValue(compact(QJsonArray::fromStringList(d.reasons)));
        if (!q.exec())
            return sqlError(q);
    }

    if (auto s = fillPageCount(db, job.book, pageCount); !s)
        return s.error();
    if (auto s = transition(db, job.id, {JobState::Running}, JobState::Succeeded, QStringLiteral("published"), {}, runId);
        !s)
        return s.error();
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return runId;
}

Result<RunId> completeTocJob(QSqlDatabase& db, const JobRecord& job, const RunId& runId, const RunIdentity& run,
                             const TocAnalysis& toc, std::optional<int> pageCount)
{
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    if (auto s = requireRunning(db, job); !s)
        return s.error();
    if (job.kind != JobKind::Toc)
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("Job %1 is not a contents job.").arg(job.id.toString()));
    // First, so that destinations are checked against the page count.
    if (auto s = fillPageCount(db, job.book, pageCount); !s)
        return s.error();
    const PublishTicket ticket{job.book, job.generation, job.sourceSha256};
    auto published = detail::publishTocRun(db, runId, ticket, run, toc);
    if (!published)
        return published;
    if (auto s = transition(db, job.id, {JobState::Running}, JobState::Succeeded, QStringLiteral("published"), {}, runId);
        !s)
        return s.error();
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return runId;
}

Result<std::optional<JobRecord>> claimQueuedJob(QSqlDatabase& db, const BookId& book, JobKind kind)
{
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id FROM jobs WHERE book_id = ? AND kind = ? AND state = 'queued'"));
    q.addBindValue(book.toString());
    q.addBindValue(toCode(kind));
    if (!q.exec())
        return sqlError(q);
    if (!q.next()) {
        if (!tx.commit())
            return sqlError(db, QStringLiteral("commit"));
        return std::optional<JobRecord>();
    }
    const JobId id = JobId::fromString(q.value(0).toString());
    q.finish();
    const QString stamp = now();
    q.prepare(QStringLiteral("UPDATE jobs SET state = 'running', attempt = attempt + 1, started_at = ?, "
                             "updated_at = ? WHERE id = ? AND state = 'queued'"));
    q.addBindValue(stamp);
    q.addBindValue(stamp);
    q.addBindValue(id.toString());
    if (!q.exec())
        return sqlError(q);
    auto claimed = catalog::job(db, id);
    if (!claimed)
        return claimed.error();
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return std::optional<JobRecord>(claimed.value());
}

namespace {

// Closes a job whose worker ended without a result (no transaction):
// CancelRequested -> Cancelled; Running -> Interrupted plus a fresh queued job
// under a new generation, unless the book is trashed or gone. An export is
// Interrupted either way and never requeued: whether its copy was committed
// is not known (its record keeps committed NULL), and the user decides
// whether to export again.
Status closeUnfinished(QSqlDatabase& db, const JobRecord& j, JobRecovery* counts)
{
    if (j.kind == JobKind::Export) {
        if (!isOpen(j.state) || j.state == JobState::Queued)
            return Done{};
        if (auto s = transition(db, j.id, {JobState::Running, JobState::CancelRequested}, JobState::Interrupted,
                                QStringLiteral("interrupted"),
                                QStringLiteral("The application closed while the copy was being written; "
                                               "the file may or may not have been saved."));
            !s)
            return s;
        ++counts->interrupted;
        return Done{};
    }
    if (j.state == JobState::CancelRequested) {
        if (auto s = transition(db, j.id, {JobState::CancelRequested}, JobState::Cancelled, QStringLiteral("cancelled"),
                                QStringLiteral("Cancelled; the application closed before it stopped."));
            !s)
            return s;
        ++counts->cancelled;
        return Done{};
    }
    if (j.state != JobState::Running)
        return Done{};
    if (auto s = transition(db, j.id, {JobState::Running}, JobState::Interrupted, QStringLiteral("interrupted"),
                            QStringLiteral("The application closed while this job was running."));
        !s)
        return s;
    ++counts->interrupted;
    auto ticket = detail::bumpGeneration(db, j.book, j.kind == JobKind::Metadata);
    if (!ticket) {
        if (ticket.error().code == ErrorCode::Trashed || ticket.error().code == ErrorCode::NotFound)
            return Done{};
        return ticket.error();
    }
    if (auto inserted = insertQueuedJob(db, ticket.value(), j.kind); !inserted)
        return inserted.error();
    ++counts->requeued;
    return Done{};
}

} // namespace

Result<JobRecovery> recoverJobs(QSqlDatabase& db)
{
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    JobRecovery recovery;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT %1 FROM jobs WHERE state IN ('running', 'cancel_requested')").arg(kColumns)))
        return sqlError(q);
    QList<JobRecord> stuck;
    while (q.next())
        stuck << readJob(q);
    q.finish();
    for (const JobRecord& j : stuck) {
        if (auto s = closeUnfinished(db, j, &recovery); !s)
            return s.error();
    }
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return recovery;
}

Result<JobRecovery> interruptJob(QSqlDatabase& db, const JobId& id)
{
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    auto current = job(db, id);
    if (!current)
        return current.error();
    JobRecovery counts;
    if (auto s = closeUnfinished(db, current.value(), &counts); !s)
        return s.error();
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return counts;
}

Result<QList<JobRecord>> latestJobs(QSqlDatabase& db)
{
    QSqlQuery q(db);
    // One pass with a window function. A correlated subquery per row is
    // quadratic: no index covers (book_id, kind, created_at), and this runs on
    // every library refresh.
    if (!q.exec(QStringLiteral("SELECT %1 FROM (SELECT *, ROW_NUMBER() OVER (PARTITION BY book_id, kind "
                               "ORDER BY created_at DESC, rowid DESC) AS latest FROM jobs) WHERE latest = 1")
                    .arg(kColumns)))
        return sqlError(q);
    QList<JobRecord> jobs;
    while (q.next())
        jobs << readJob(q);
    return jobs;
}

Result<QStringList> referencedReportPaths(QSqlDatabase& db)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT report_path FROM metadata_runs WHERE report_path IS NOT NULL "
                               "UNION SELECT report_path FROM toc_runs WHERE report_path IS NOT NULL")))
        return sqlError(q);
    QStringList paths;
    while (q.next())
        paths << q.value(0).toString();
    return paths;
}

Result<JobRecord> job(QSqlDatabase& db, const JobId& id)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT %1 FROM jobs WHERE id = ?").arg(kColumns));
    q.addBindValue(id.toString());
    if (!q.exec())
        return sqlError(q);
    if (!q.next())
        return makeError(ErrorCode::NotFound, QStringLiteral("No job %1.").arg(id.toString()));
    return readJob(q);
}

Result<QList<JobRecord>> listJobs(QSqlDatabase& db, bool openOnly, int limit)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT %1 FROM jobs %2 ORDER BY created_at DESC, id LIMIT ?")
                  .arg(kColumns, openOnly ? QStringLiteral("WHERE state IN ('queued', 'running', 'cancel_requested')")
                                          : QString()));
    q.addBindValue(limit);
    if (!q.exec())
        return sqlError(q);
    QList<JobRecord> jobs;
    while (q.next())
        jobs << readJob(q);
    return jobs;
}

Result<QList<MetadataFieldDetail>> metadataDetails(QSqlDatabase& db, const RunId& run)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT field, evidence_json, alternatives_json, reasons_json FROM metadata_field_details "
                             "WHERE run_id = ? ORDER BY field"));
    q.addBindValue(run.toString());
    if (!q.exec())
        return sqlError(q);
    QList<MetadataFieldDetail> out;
    while (q.next()) {
        MetadataFieldDetail d;
        d.field = metadataFieldFromCode(q.value(0).toString()).value_or(MetadataField::Title);
        d.evidence = evidenceFromJson(QJsonDocument::fromJson(q.value(1).toString().toUtf8()).array());
        for (const QJsonValue& v : QJsonDocument::fromJson(q.value(2).toString().toUtf8()).array()) {
            const QJsonObject o = v.toObject();
            MetadataCandidate c;
            c.value = o.value(QStringLiteral("value")).toString();
            c.score = o.value(QStringLiteral("score")).toDouble();
            c.evidence = evidenceFromJson(o.value(QStringLiteral("evidence")).toArray());
            for (const QJsonValue& r : o.value(QStringLiteral("reasons")).toArray())
                c.reasons << r.toString();
            d.alternatives << c;
        }
        for (const QJsonValue& r : QJsonDocument::fromJson(q.value(3).toString().toUtf8()).array())
            d.reasons << r.toString();
        out << d;
    }
    return out;
}

} // namespace mbl::catalog
