#include "catalog/exports.h"

#include "catalog/catalog.h"
#include "catalog/catalog_internal.h"
#include "catalog/jobs.h"

#include <QSqlQuery>
#include <QVariant>

namespace mbl::catalog {

using namespace mbl::domain;
using detail::now;
using detail::sqlError;
using detail::Transaction;

namespace {

// `committed` reads as false for a job that ended before it ever ran (e.g.
// cancelled while queued): nothing can have been written.
const QString kSelect = QStringLiteral(
    "SELECT e.job_id, e.book_id, e.destination, e.replace_existing, e.toc_run_id, e.toc_revision_id, e.plan_json, "
    "CASE WHEN e.committed IS NULL AND j.started_at IS NULL AND j.state NOT IN ('queued', 'running', "
    "'cancel_requested') THEN 0 ELSE e.committed END, e.output_sha256, e.outline_items, e.output_page_count, "
    "e.structure_matches, e.source_unchanged, e.sdk_version, e.sdk_plan_json, j.created_at, j.finished_at, "
    "e.replace_size, e.replace_modified "
    "FROM exports e JOIN jobs j ON j.id = e.job_id");

QVariant textOrNull(const QString& text)
{
    return text.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(text);
}

Result<ExportRecord> readRecord(const QSqlQuery& q)
{
    ExportRecord r;
    r.job = JobId::fromString(q.value(0).toString());
    r.book = BookId::fromString(q.value(1).toString());
    r.destination = q.value(2).toString();
    r.replaceExisting = q.value(3).toBool();
    if (!q.value(4).isNull())
        r.tocRun = RunId::fromString(q.value(4).toString());
    if (!q.value(5).isNull())
        r.tocRevision = TocRevisionId::fromString(q.value(5).toString());
    auto plan = exportPlanFromJson(q.value(6).toString());
    if (!plan)
        return plan.error();
    r.plan = plan.value();
    if (!q.value(7).isNull())
        r.committed = q.value(7).toBool();
    r.output.committed = r.committed.value_or(false);
    r.output.outputSha256 = q.value(8).toString();
    r.output.outlineItems = q.value(9).toInt();
    r.output.pageCount = q.value(10).toInt();
    r.output.structureMatches = q.value(11).toBool();
    r.output.sourceUnchanged = q.value(12).toBool();
    r.output.sdkVersion = q.value(13).toString();
    r.output.sdkPlanJson = q.value(14).toString();
    r.createdAt = QDateTime::fromString(q.value(15).toString(), Qt::ISODateWithMs);
    r.finishedAt = QDateTime::fromString(q.value(16).toString(), Qt::ISODateWithMs);
    if (!q.value(17).isNull())
        r.confirmedFile = FileIdentity{q.value(17).toLongLong(),
                                       QDateTime::fromString(q.value(18).toString(), Qt::ISODateWithMs).toUTC()};
    return r;
}

} // namespace

Result<ProtectedFiles> protectedFiles(QSqlDatabase& db)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT a.managed_path, b.original_path FROM books b JOIN assets a ON a.id = b.asset_id")))
        return sqlError(q);
    ProtectedFiles files;
    while (q.next()) {
        files.managedPaths << q.value(0).toString();
        if (!q.value(1).toString().isEmpty())
            files.originalPaths << q.value(1).toString();
    }
    return files;
}

Result<ExportRecord> enqueueExport(QSqlDatabase& db, const BookId& book, const QString& destination,
                                   bool replaceExisting, const std::optional<FileIdentity>& confirmedFile)
{
    if (destination.trimmed().isEmpty())
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("Choose where to save the copy."));
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    auto details = bookDetails(db, book);
    if (!details)
        return details.error();
    if (details.value().summary.lifecycle == Lifecycle::Trashed)
        return makeError(ErrorCode::Trashed, QStringLiteral("The book is in Trash; restore it to export it."));
    if (!details.value().toc)
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("The book's contents have not been analyzed yet, so there is nothing to bookmark."));
    auto plan = buildExportPlan(*details.value().toc, details.value().asset);
    if (!plan)
        return plan.error();

    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM jobs WHERE book_id = ? AND kind = 'export' AND state IN ('queued', 'running')"));
    q.addBindValue(book.toString());
    if (!q.exec())
        return sqlError(q);
    if (q.next())
        return makeError(ErrorCode::Duplicate, QStringLiteral("A copy of this book is already being written."));
    q.finish();

    const JobId id = JobId::create();
    const QString stamp = now();
    q.prepare(QStringLiteral("INSERT INTO jobs(id, book_id, kind, state, generation, source_sha256, created_at, "
                             "updated_at) VALUES (?, ?, 'export', 'queued', 0, ?, ?, ?)"));
    q.addBindValue(id.toString());
    q.addBindValue(book.toString());
    q.addBindValue(details.value().asset.sha256);
    q.addBindValue(stamp);
    q.addBindValue(stamp);
    if (!q.exec())
        return sqlError(q);
    q.prepare(QStringLiteral("INSERT INTO exports(job_id, book_id, destination, replace_existing, toc_run_id, "
                             "toc_revision_id, plan_json, replace_size, replace_modified) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(id.toString());
    q.addBindValue(book.toString());
    q.addBindValue(destination);
    q.addBindValue(replaceExisting ? 1 : 0);
    q.addBindValue(details.value().tocRun ? textOrNull(details.value().tocRun->toString()) : textOrNull({}));
    q.addBindValue(details.value().tocRevision ? textOrNull(details.value().tocRevision->id.toString()) : textOrNull({}));
    q.addBindValue(exportPlanToJson(plan.value()));
    const bool confirmed = replaceExisting && confirmedFile;
    q.addBindValue(confirmed ? QVariant(confirmedFile->size) : QVariant(QMetaType(QMetaType::LongLong)));
    q.addBindValue(confirmed ? QVariant(confirmedFile->modified.toUTC().toString(Qt::ISODateWithMs))
                             : QVariant(QMetaType(QMetaType::QString)));
    if (!q.exec())
        return sqlError(q);
    auto record = exportRecord(db, id);
    if (!record)
        return record;
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return record;
}

Status finishExportJob(QSqlDatabase& db, const JobId& id, JobState state, const QString& outcome,
                       const QString& error, const ExportOutput& output)
{
    if (!output.committed && (isOpen(state) || state == JobState::Succeeded))
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("An export that wrote nothing ends Failed, Cancelled or Interrupted."));
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    auto current = job(db, id);
    if (!current)
        return current.error();
    if (current.value().kind != JobKind::Export)
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("Job %1 is not an export.").arg(id.toString()));

    const std::initializer_list<JobState> open{JobState::Running, JobState::CancelRequested};
    Status moved = Done{};
    if (output.committed) {
        // The file exists: that is what the record says, whatever came after.
        moved = detail::transitionJob(db, id, open, JobState::Succeeded, QStringLiteral("written"), {});
    } else if (state == JobState::Cancelled && current.value().state == JobState::CancelRequested
               && current.value().outcome == QLatin1String("trashed")) {
        moved = detail::transitionJob(db, id, {JobState::CancelRequested}, state, current.value().outcome,
                                      current.value().error);
    } else {
        moved = detail::transitionJob(db, id, open, state, outcome, error);
    }
    if (!moved)
        return moved;

    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE exports SET committed = ?, output_sha256 = ?, outline_items = ?, "
                             "output_page_count = ?, structure_matches = ?, source_unchanged = ?, sdk_version = ?, "
                             "sdk_plan_json = ? WHERE job_id = ?"));
    q.addBindValue(output.committed ? 1 : 0);
    q.addBindValue(output.committed ? textOrNull(output.outputSha256.toLower()) : textOrNull({}));
    q.addBindValue(output.committed ? QVariant(output.outlineItems) : QVariant(QMetaType(QMetaType::Int)));
    q.addBindValue(output.committed ? QVariant(output.pageCount) : QVariant(QMetaType(QMetaType::Int)));
    q.addBindValue(output.committed ? QVariant(output.structureMatches ? 1 : 0) : QVariant(QMetaType(QMetaType::Int)));
    q.addBindValue(output.committed ? QVariant(output.sourceUnchanged ? 1 : 0) : QVariant(QMetaType(QMetaType::Int)));
    q.addBindValue(textOrNull(output.sdkVersion));
    q.addBindValue(textOrNull(output.sdkPlanJson));
    q.addBindValue(id.toString());
    if (!q.exec())
        return sqlError(q);
    if (q.numRowsAffected() != 1)
        return makeError(ErrorCode::NotFound, QStringLiteral("No export record for job %1.").arg(id.toString()));
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return Done{};
}

Result<int> closeExportsAfterRestore(QSqlDatabase& db)
{
    const QString stamp = now();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE jobs SET state = 'cancelled', outcome = 'restored', error = ?, updated_at = ?, "
                             "finished_at = ? WHERE kind = 'export' AND state = 'queued'"));
    q.addBindValue(QStringLiteral("The library was restored from a backup; nothing was written. "
                                  "Ask again if you still want this copy."));
    q.addBindValue(stamp);
    q.addBindValue(stamp);
    if (!q.exec())
        return sqlError(q);
    return q.numRowsAffected();
}

Result<ExportRecord> exportRecord(QSqlDatabase& db, const JobId& id)
{
    QSqlQuery q(db);
    q.prepare(kSelect + QStringLiteral(" WHERE e.job_id = ?"));
    q.addBindValue(id.toString());
    if (!q.exec())
        return sqlError(q);
    if (!q.next())
        return makeError(ErrorCode::NotFound, QStringLiteral("No export %1.").arg(id.toString()));
    return readRecord(q);
}

Result<QList<ExportRecord>> bookExports(QSqlDatabase& db, const BookId& book)
{
    QSqlQuery q(db);
    q.prepare(kSelect + QStringLiteral(" WHERE e.book_id = ? ORDER BY j.created_at DESC, j.rowid DESC"));
    q.addBindValue(book.toString());
    if (!q.exec())
        return sqlError(q);
    QList<ExportRecord> records;
    while (q.next()) {
        auto record = readRecord(q);
        if (!record)
            return record.error();
        records << record.value();
    }
    return records;
}

} // namespace mbl::catalog
