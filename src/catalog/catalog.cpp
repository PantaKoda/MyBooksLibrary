#include "catalog/catalog.h"

#include "catalog/catalog_internal.h"
#include "search/searchindex.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace mbl::catalog {

using namespace mbl::domain;
using detail::now;
using detail::sqlError;
using detail::Transaction;

namespace detail {

QString now()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

Error sqlError(const QSqlQuery& query)
{
    return makeError(ErrorCode::Database, query.lastError().text());
}

Error sqlError(const QSqlDatabase& db, const QString& what)
{
    return makeError(ErrorCode::Database, QStringLiteral("%1: %2").arg(what, db.lastError().text()));
}

} // namespace detail

namespace {

QVariant nullable(const std::optional<int>& v)
{
    return v ? QVariant(*v) : QVariant(QMetaType(QMetaType::Int));
}

QVariant nullable(const std::optional<QString>& v)
{
    return v ? QVariant(*v) : QVariant(QMetaType(QMetaType::QString));
}

QVariant nullableText(const QString& v)
{
    return v.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(v);
}

// For NOT NULL text columns: a null QString binds as SQL NULL, so an empty
// value (e.g. no model identity when OCR is unavailable) must bind as ''.
QVariant requiredText(const QString& v)
{
    return v.isNull() ? QVariant(QStringLiteral("")) : QVariant(v);
}

std::optional<int> optInt(const QVariant& v)
{
    return v.isNull() ? std::nullopt : std::optional<int>(v.toInt());
}

QJsonArray intArray(const QList<int>& values)
{
    QJsonArray out;
    for (int v : values)
        out << v;
    return out;
}

QList<int> intList(const QJsonValue& v)
{
    QList<int> out;
    for (const QJsonValue& x : v.toArray())
        out << x.toInt();
    return out;
}

QStringList stringList(const QJsonValue& v)
{
    QStringList out;
    for (const QJsonValue& x : v.toArray())
        out << x.toString();
    return out;
}

QString compactJson(const QJsonArray& a)
{
    return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
}

// toc_entries.evidence_json: an object; empty lists and absent values are omitted.
QString tocEvidenceJson(const TocEntryEvidence& e)
{
    QJsonObject o;
    if (!e.sourcePages.isEmpty())
        o.insert(QStringLiteral("source_pages"), intArray(e.sourcePages));
    if (!e.hierarchyReasons.isEmpty())
        o.insert(QStringLiteral("hierarchy_reasons"), QJsonArray::fromStringList(e.hierarchyReasons));
    if (e.printedLabelUncertain)
        o.insert(QStringLiteral("printed_label_uncertain"), true);
    if (!e.printedLabelReasons.isEmpty())
        o.insert(QStringLiteral("printed_label_reasons"), QJsonArray::fromStringList(e.printedLabelReasons));
    if (e.destinationMethod)
        o.insert(QStringLiteral("destination_method"), *e.destinationMethod);
    if (!e.destinationReasons.isEmpty())
        o.insert(QStringLiteral("destination_reasons"), QJsonArray::fromStringList(e.destinationReasons));
    if (!e.alternativePages.isEmpty())
        o.insert(QStringLiteral("alternative_pages"), intArray(e.alternativePages));
    if (e.omissionReason)
        o.insert(QStringLiteral("omission_reason"), *e.omissionReason);
    if (!e.diagnostics.isEmpty())
        o.insert(QStringLiteral("diagnostics"), QJsonArray::fromStringList(e.diagnostics));
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

TocEntryEvidence tocEvidenceFromJson(const QString& json)
{
    const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
    TocEntryEvidence e;
    e.sourcePages = intList(o.value(QStringLiteral("source_pages")));
    e.hierarchyReasons = stringList(o.value(QStringLiteral("hierarchy_reasons")));
    e.printedLabelUncertain = o.value(QStringLiteral("printed_label_uncertain")).toBool();
    e.printedLabelReasons = stringList(o.value(QStringLiteral("printed_label_reasons")));
    if (o.contains(QStringLiteral("destination_method")))
        e.destinationMethod = o.value(QStringLiteral("destination_method")).toString();
    e.destinationReasons = stringList(o.value(QStringLiteral("destination_reasons")));
    e.alternativePages = intList(o.value(QStringLiteral("alternative_pages")));
    if (o.contains(QStringLiteral("omission_reason")))
        e.omissionReason = o.value(QStringLiteral("omission_reason")).toString();
    e.diagnostics = stringList(o.value(QStringLiteral("diagnostics")));
    return e;
}

QVariant nullableBool(const std::optional<bool>& v)
{
    return v ? QVariant(*v ? 1 : 0) : QVariant(QMetaType(QMetaType::Int));
}

std::optional<QString> optText(const QVariant& v)
{
    return v.isNull() ? std::nullopt : std::optional<QString>(v.toString());
}

struct BookRow {
    BookId id;
    AssetRecord asset;
    Lifecycle lifecycle = Lifecycle::Active;
    qint64 revision = 0;
    qint64 metadataGeneration = 0;
    qint64 tocGeneration = 0;
    std::optional<RunId> activeMetadataRun;
    std::optional<RunId> activeTocRun;
    std::optional<TocRevisionId> activeTocRevision;
    QString originalFileName;
    QString originalPath;
};

Result<BookRow> loadBookRow(QSqlDatabase& db, const BookId& id)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT b.lifecycle, b.revision, b.metadata_generation, b.toc_generation, b.active_metadata_run_id, "
        "b.active_toc_run_id, b.original_file_name, b.original_path, a.id, a.sha256, a.byte_size, "
        "a.page_count, a.managed_path, b.active_toc_revision_id FROM books b JOIN assets a ON a.id = b.asset_id "
        "WHERE b.id = ?"));
    q.addBindValue(id.toString());
    if (!q.exec())
        return sqlError(q);
    if (!q.next())
        return makeError(ErrorCode::NotFound, QStringLiteral("No book %1.").arg(id.toString()));
    BookRow row;
    row.id = id;
    row.lifecycle = lifecycleFromCode(q.value(0).toString()).value_or(Lifecycle::Active);
    row.revision = q.value(1).toLongLong();
    row.metadataGeneration = q.value(2).toLongLong();
    row.tocGeneration = q.value(3).toLongLong();
    if (!q.value(4).isNull())
        row.activeMetadataRun = RunId::fromString(q.value(4).toString());
    if (!q.value(5).isNull())
        row.activeTocRun = RunId::fromString(q.value(5).toString());
    row.originalFileName = q.value(6).toString();
    row.originalPath = q.value(7).toString();
    row.asset.id = AssetId::fromString(q.value(8).toString());
    row.asset.sha256 = q.value(9).toString();
    row.asset.byteSize = q.value(10).toLongLong();
    row.asset.pageCount = optInt(q.value(11));
    row.asset.managedPath = q.value(12).toString();
    if (!q.value(13).isNull())
        row.activeTocRevision = TocRevisionId::fromString(q.value(13).toString());
    return row;
}

Result<ExtractedMetadata> loadExtracted(QSqlDatabase& db, const RunId& run)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT title_status, title, subtitle, contributors_status, edition_status, edition_statement, "
        "edition_ordinal, publication_year_status, publication_year, copyright_year_status, copyright_year "
        "FROM metadata_runs WHERE id = ?"));
    q.addBindValue(run.toString());
    if (!q.exec())
        return sqlError(q);
    if (!q.next())
        return makeError(ErrorCode::NotFound, QStringLiteral("No metadata run %1.").arg(run.toString()));
    const auto status = [](const QVariant& v) {
        return fieldStatusFromCode(v.toString()).value_or(FieldStatus::NotFoundInSearch);
    };
    ExtractedMetadata m;
    m.titleStatus = status(q.value(0));
    m.title = optText(q.value(1));
    m.subtitle = optText(q.value(2));
    m.contributorsStatus = status(q.value(3));
    m.editionStatus = status(q.value(4));
    m.editionStatement = optText(q.value(5));
    m.editionOrdinal = optInt(q.value(6));
    m.publicationYearStatus = status(q.value(7));
    m.publicationYear = optInt(q.value(8));
    m.copyrightYearStatus = status(q.value(9));
    m.copyrightYear = optInt(q.value(10));

    q.prepare(QStringLiteral(
        "SELECT name, role FROM metadata_run_contributors WHERE run_id = ? ORDER BY position"));
    q.addBindValue(run.toString());
    if (!q.exec())
        return sqlError(q);
    while (q.next()) {
        m.contributors << Contributor{q.value(0).toString(),
                                      contributorRoleFromCode(q.value(1).toString()).value_or(ContributorRole::Author)};
    }
    return m;
}

Result<MetadataOverrides> loadOverrides(QSqlDatabase& db, const BookId& book)
{
    MetadataOverrides overrides;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT field, mode, value_text, value_int FROM metadata_overrides WHERE book_id = ?"));
    q.addBindValue(book.toString());
    if (!q.exec())
        return sqlError(q);
    while (q.next()) {
        const auto field = metadataFieldFromCode(q.value(0).toString());
        const auto mode = overrideModeFromCode(q.value(1).toString());
        if (!field || !mode)
            continue;
        MetadataOverride& o = overrides[*field];
        o.mode = *mode;
        o.text = optText(q.value(2));
        o.year = optInt(q.value(3));
    }
    if (overrides.contributors.mode == OverrideMode::Value) {
        q.prepare(QStringLiteral(
            "SELECT name, role FROM metadata_override_contributors WHERE book_id = ? ORDER BY position"));
        q.addBindValue(book.toString());
        if (!q.exec())
            return sqlError(q);
        while (q.next()) {
            overrides.contributors.contributors
                << Contributor{q.value(0).toString(),
                               contributorRoleFromCode(q.value(1).toString()).value_or(ContributorRole::Author)};
        }
    }
    return overrides;
}

struct StoredToc {
    TocAnalysis analysis;
    QList<qint64> keys;  // toc_entries.id, parallel to analysis.entries.
};

Result<StoredToc> loadToc(QSqlDatabase& db, const RunId& run)
{
    StoredToc toc;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT outcome, plan_ready, parse_complete, search_covered_document, "
                             "plan_blockers_json, stop_reasons_json, plan_json FROM toc_runs WHERE id = ?"));
    q.addBindValue(run.toString());
    if (!q.exec())
        return sqlError(q);
    if (!q.next())
        return makeError(ErrorCode::NotFound, QStringLiteral("No TOC run %1.").arg(run.toString()));
    toc.analysis.outcome = q.value(0).toString();
    toc.analysis.planReady = q.value(1).toInt() != 0;
    if (const auto v = optInt(q.value(2)))
        toc.analysis.parseComplete = *v != 0;
    if (const auto v = optInt(q.value(3)))
        toc.analysis.searchCoveredDocument = *v != 0;
    toc.analysis.planBlockers = stringList(QJsonDocument::fromJson(q.value(4).toString().toUtf8()).array());
    toc.analysis.stopReasons = stringList(QJsonDocument::fromJson(q.value(5).toString().toUtf8()).array());
    toc.analysis.planJson = q.value(6).toString().toUtf8();

    q.prepare(QStringLiteral(
        "SELECT id, sdk_entry_id, entry_order, title, hierarchy, parent_sdk_entry_id, printed_label, "
        "destination_state, destination_page, source_toc_page, in_export_plan, evidence_json "
        "FROM toc_entries WHERE run_id = ? ORDER BY entry_order, id"));
    q.addBindValue(run.toString());
    if (!q.exec())
        return sqlError(q);
    while (q.next()) {
        TocEntry e;
        e.sdkEntryId = q.value(1).toString();
        e.order = q.value(2).toInt();
        e.title = q.value(3).toString();
        e.hierarchy = hierarchyStateFromCode(q.value(4).toString()).value_or(HierarchyState::Unknown);
        e.parentSdkEntryId = optText(q.value(5));
        e.printedLabel = optText(q.value(6));
        e.destinationState =
            destinationStateFromCode(q.value(7).toString()).value_or(DestinationState::Unresolved);
        e.destinationPage = optInt(q.value(8));
        e.sourceTocPage = optInt(q.value(9));
        e.inExportPlan = q.value(10).toInt() != 0;
        e.evidence = tocEvidenceFromJson(q.value(11).toString());
        toc.keys << q.value(0).toLongLong();
        toc.analysis.entries << e;
    }
    return toc;
}

QString fileNameTitle(const QString& originalFileName)
{
    const QFileInfo info(originalFileName);
    const QString base = info.completeBaseName();
    return base.isEmpty() ? originalFileName : base;
}

struct Computed {
    BookSummary summary;
    std::optional<ExtractedMetadata> extracted;
    MetadataOverrides overrides;
    std::optional<StoredToc> toc;                 // Effective contents (edited revision, else the run).
    std::optional<TocRevisionInfo> tocRevision;
    std::optional<TocAnalysis> analyzedToc;       // The run's own entries, when a revision is shown.
};

Result<Computed> compute(QSqlDatabase& db, const BookRow& row)
{
    Computed c;
    if (row.activeMetadataRun) {
        auto extracted = loadExtracted(db, *row.activeMetadataRun);
        if (!extracted)
            return extracted.error();
        c.extracted = extracted.value();
    }
    auto overrides = loadOverrides(db, row.id);
    if (!overrides)
        return overrides.error();
    c.overrides = overrides.value();
    if (row.activeTocRun) {
        auto toc = loadToc(db, *row.activeTocRun);
        if (!toc)
            return toc.error();
        c.toc = toc.value();
        if (row.activeTocRevision) {
            auto revision = detail::loadTocRevision(db, *row.activeTocRevision);
            if (!revision)
                return revision.error();
            const detail::StoredTocRevision& r = revision.value();
            c.analyzedToc = c.toc->analysis;
            c.toc->analysis.entries = r.entries;
            c.toc->keys = r.rowIds;
            c.tocRevision = TocRevisionInfo{r.id, r.number, r.baseRun, r.baseRun != *row.activeTocRun};
        }
    }

    BookSummary& s = c.summary;
    s.id = row.id;
    s.assetId = row.asset.id;
    s.lifecycle = row.lifecycle;
    s.revision = row.revision;
    s.metadata = effectiveMetadata(c.extracted, c.overrides);
    s.displayTitleFromFileName = !s.metadata.title;
    s.displayTitle = s.metadata.title ? *s.metadata.title : fileNameTitle(row.originalFileName);
    s.hasMetadataRun = row.activeMetadataRun.has_value();
    if (c.extracted)
        s.extractedTitleStatus = c.extracted->titleStatus;
    s.hasTocRun = row.activeTocRun.has_value();
    s.tocEntryCount = 0;
    if (c.toc) {
        for (const TocEntry& e : c.toc->analysis.entries)
            s.tocEntryCount += e.removed ? 0 : 1;
    }
    s.tocEdited = c.tocRevision.has_value();
    s.tocNeedsReconciliation = c.tocRevision && c.tocRevision->needsReconciliation;
    return c;
}

// Brings the book's search rows in line with the catalog. Caller holds the transaction.
Status refreshProjection(QSqlDatabase& db, const BookId& id)
{
    auto row = loadBookRow(db, id);
    if (!row)
        return row.error();
    QString error;
    if (row.value().lifecycle != Lifecycle::Active) {
        if (!search::removeBookProjection(db, id, &error))
            return makeError(ErrorCode::Database, error);
        return Done{};
    }
    auto computed = compute(db, row.value());
    if (!computed)
        return computed.error();
    const Computed& c = computed.value();

    search::BookProjection p;
    p.book = id;
    p.displayTitle = c.summary.displayTitle;
    p.titleText = c.summary.displayTitle;
    if (c.summary.metadata.subtitle)
        p.titleText += u'\n' + *c.summary.metadata.subtitle;
    for (const Contributor& contributor : c.summary.metadata.contributors)
        p.contributors << contributor.name;
    if (c.toc) {
        for (qsizetype i = 0; i < c.toc->analysis.entries.size(); ++i) {
            const TocEntry& e = c.toc->analysis.entries.at(i);
            if (e.removed)
                continue;  // Removed by the user: kept in the revision, not searched.
            search::ChapterProjection chapter;
            chapter.entryKey = c.toc->keys.at(i);
            chapter.title = e.title;
            chapter.order = e.order;
            chapter.printedLabel = e.printedLabel;
            chapter.destinationState = e.destinationState;
            chapter.destinationPage = e.destinationPage;
            chapter.sourceTocPage = e.sourceTocPage;
            chapter.inExportPlan = e.inExportPlan;
            p.chapters << chapter;
        }
    }
    if (!search::replaceBookProjection(db, p, &error))
        return makeError(ErrorCode::Database, error);
    return Done{};
}

Status touchBook(QSqlDatabase& db, const BookId& id)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE books SET revision = revision + 1, updated_at = ? WHERE id = ?"));
    q.addBindValue(now());
    q.addBindValue(id.toString());
    if (!q.exec())
        return sqlError(q);
    return Done{};
}

Result<PublishTicket> bumpGenerationColumn(QSqlDatabase& db, const BookId& id, const char* column)
{
    auto row = loadBookRow(db, id);
    if (!row)
        return row.error();
    if (row.value().lifecycle == Lifecycle::Trashed)
        return makeError(ErrorCode::Trashed, QStringLiteral("The book is in Trash."));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE books SET %1 = %1 + 1 WHERE id = ?").arg(QLatin1StringView(column)));
    q.addBindValue(id.toString());
    if (!q.exec())
        return sqlError(q);
    q.prepare(QStringLiteral("SELECT %1 FROM books WHERE id = ?").arg(QLatin1StringView(column)));
    q.addBindValue(id.toString());
    if (!q.exec() || !q.next())
        return sqlError(q);
    PublishTicket ticket{id, q.value(0).toLongLong(), row.value().asset.sha256};
    q.finish();
    return ticket;
}

Result<PublishTicket> requestRun(QSqlDatabase& db, const BookId& id, const char* column)
{
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    auto ticket = bumpGenerationColumn(db, id, column);
    if (!ticket)
        return ticket;
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return ticket;
}

// Common publication checks. Returns the book row when the result may be published.
Result<BookRow> checkPublishable(QSqlDatabase& db, const PublishTicket& ticket, const RunIdentity& run,
                                 bool metadata)
{
    auto row = loadBookRow(db, ticket.book);
    if (!row)
        return row.error();
    const BookRow& r = row.value();
    if (r.lifecycle == Lifecycle::Trashed)
        return makeError(ErrorCode::Trashed, QStringLiteral("The book was moved to Trash; result discarded."));
    const qint64 current = metadata ? r.metadataGeneration : r.tocGeneration;
    if (ticket.generation != current) {
        return makeError(ErrorCode::StaleGeneration,
                         QStringLiteral("Result for request %1 is stale; the current request is %2.")
                             .arg(ticket.generation)
                             .arg(current));
    }
    if (ticket.sourceSha256 != r.asset.sha256 || run.sourceSha256 != r.asset.sha256)
        return makeError(ErrorCode::SourceMismatch, QStringLiteral("The result was produced from different source bytes."));
    return row;
}

bool bindRunIdentity(QSqlQuery& q, const RunId& id, const PublishTicket& ticket, const RunIdentity& run)
{
    q.addBindValue(id.toString());
    q.addBindValue(ticket.book.toString());
    q.addBindValue(ticket.generation);
    q.addBindValue(run.sourceSha256);
    q.addBindValue(requiredText(run.sdkVersion));
    q.addBindValue(requiredText(run.modelIdentity));
    q.addBindValue(requiredText(run.optionsJson));
    q.addBindValue(requiredText(run.outcome));
    q.addBindValue(nullable(run.reportPath));
    q.addBindValue(now());
    return true;
}

// Downgrades KnownParent entries with a missing, self or cyclic parent to Unknown.
QList<TocEntry> normalizeHierarchy(QList<TocEntry> entries)
{
    QHash<QString, qsizetype> index;
    for (qsizetype i = 0; i < entries.size(); ++i)
        index.insert(entries.at(i).sdkEntryId, i);

    const auto dropParent = [](TocEntry& e) {
        e.hierarchy = HierarchyState::Unknown;
        e.parentSdkEntryId.reset();
    };
    for (TocEntry& e : entries) {
        if (e.hierarchy != HierarchyState::KnownParent) {
            e.parentSdkEntryId.reset();
            continue;
        }
        if (!e.parentSdkEntryId || *e.parentSdkEntryId == e.sdkEntryId || !index.contains(*e.parentSdkEntryId))
            dropParent(e);
    }
    // Two passes are not enough for cycles: walk each chain with a step bound.
    bool changed = true;
    while (changed) {
        changed = false;
        for (TocEntry& e : entries) {
            if (e.hierarchy != HierarchyState::KnownParent)
                continue;
            QSet<QString> seen{e.sdkEntryId};
            std::optional<QString> parent = e.parentSdkEntryId;
            while (parent) {
                if (seen.contains(*parent)) {
                    dropParent(e);
                    changed = true;
                    break;
                }
                seen.insert(*parent);
                const TocEntry& p = entries.at(index.value(*parent));
                parent = p.hierarchy == HierarchyState::KnownParent ? p.parentSdkEntryId : std::nullopt;
            }
        }
    }
    return entries;
}

Status validateOverride(MetadataField field, const MetadataOverride& o)
{
    if (o.mode != OverrideMode::Value)
        return Done{};
    switch (field) {
    case MetadataField::Title:
    case MetadataField::Subtitle:
    case MetadataField::Edition:
        if (!o.text || o.text->trimmed().isEmpty())
            return makeError(ErrorCode::InvalidArgument, QStringLiteral("A text value is required; use Cleared to empty the field."));
        return Done{};
    case MetadataField::PublicationYear:
    case MetadataField::CopyrightYear:
        if (!o.year)
            return makeError(ErrorCode::InvalidArgument, QStringLiteral("A year is required."));
        if (*o.year < 1 || *o.year > 9999)
            return makeError(ErrorCode::InvalidArgument, QStringLiteral("A year must be between 1 and 9999."));
        return Done{};
    case MetadataField::Contributors:
        if (o.contributors.isEmpty())
            return makeError(ErrorCode::InvalidArgument, QStringLiteral("At least one contributor is required; use Cleared to empty the field."));
        for (const Contributor& c : o.contributors) {
            if (c.name.trimmed().isEmpty())
                return makeError(ErrorCode::InvalidArgument, QStringLiteral("Contributor names cannot be empty."));
        }
        return Done{};
    }
    return Done{};
}

Status setLifecycle(QSqlDatabase& db, const BookId& id, Lifecycle lifecycle)
{
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    auto row = loadBookRow(db, id);
    if (!row)
        return row.error();
    if (row.value().lifecycle == lifecycle)
        return Done{};
    QSqlQuery q(db);
    if (lifecycle == Lifecycle::Trashed) {
        // Invalidate pending work so late completions cannot restore anything.
        q.prepare(QStringLiteral(
            "UPDATE books SET lifecycle = 'trashed', metadata_generation = metadata_generation + 1, "
            "toc_generation = toc_generation + 1 WHERE id = ?"));
    } else {
        q.prepare(QStringLiteral("UPDATE books SET lifecycle = 'active' WHERE id = ?"));
    }
    q.addBindValue(id.toString());
    if (!q.exec())
        return sqlError(q);
    if (auto s = touchBook(db, id); !s)
        return s;
    if (auto s = refreshProjection(db, id); !s)
        return s;
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return Done{};
}

} // namespace

Status detail::touchBook(QSqlDatabase& db, const BookId& book)
{
    return mbl::catalog::touchBook(db, book);
}

Status detail::refreshBookProjection(QSqlDatabase& db, const BookId& book)
{
    return refreshProjection(db, book);
}

QString detail::tocEvidenceToJson(const TocEntryEvidence& evidence)
{
    return tocEvidenceJson(evidence);
}

TocEntryEvidence detail::tocEvidenceFromJson(const QString& json)
{
    return mbl::catalog::tocEvidenceFromJson(json);
}

Result<QList<TocEntry>> detail::loadRunTocEntries(QSqlDatabase& db, const RunId& run)
{
    auto toc = loadToc(db, run);
    if (!toc)
        return toc.error();
    return toc.value().analysis.entries;
}

Result<BookId> detail::insertBook(QSqlDatabase& db, const NewBook& book)
{
    const AssetRecord& a = book.asset;
    if (a.id.isNull() || a.sha256.size() != 64 || a.managedPath.isEmpty())
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("Asset id, SHA-256 and managed path are required."));

    auto existing = findBookBySha256(db, a.sha256);
    if (!existing)
        return existing.error();
    if (existing.value())
        return makeError(ErrorCode::Duplicate, QStringLiteral("An asset with this SHA-256 is already registered."));

    const QString stamp = now();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO assets(id, sha256, byte_size, page_count, managed_path, created_at) VALUES (?, ?, ?, ?, ?, ?)"));
    q.addBindValue(a.id.toString());
    q.addBindValue(a.sha256.toLower());
    q.addBindValue(a.byteSize);
    q.addBindValue(nullable(a.pageCount));
    q.addBindValue(a.managedPath);
    q.addBindValue(stamp);
    if (!q.exec())
        return sqlError(q);

    const BookId id = BookId::create();
    q.prepare(QStringLiteral(
        "INSERT INTO books(id, asset_id, original_file_name, original_path, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?)"));
    q.addBindValue(id.toString());
    q.addBindValue(a.id.toString());
    q.addBindValue(book.originalFileName);
    q.addBindValue(book.originalPath);
    q.addBindValue(stamp);
    q.addBindValue(stamp);
    if (!q.exec())
        return sqlError(q);
    if (auto s = refreshProjection(db, id); !s)
        return s.error();
    return id;
}


Result<BookId> registerBook(QSqlDatabase& db, const NewBook& book)
{
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    auto id = detail::insertBook(db, book);
    if (!id)
        return id;
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return id;
}

Result<std::optional<BookId>> findBookBySha256(QSqlDatabase& db, const QString& sha256)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT b.id FROM books b JOIN assets a ON a.id = b.asset_id WHERE a.sha256 = ?"));
    q.addBindValue(sha256.toLower());
    if (!q.exec())
        return sqlError(q);
    if (!q.next())
        return std::optional<BookId>();
    return std::optional<BookId>(BookId::fromString(q.value(0).toString()));
}

Result<PublishTicket> requestMetadataRun(QSqlDatabase& db, const BookId& book)
{
    return requestRun(db, book, "metadata_generation");
}

Result<PublishTicket> requestTocRun(QSqlDatabase& db, const BookId& book)
{
    return requestRun(db, book, "toc_generation");
}

Result<PublishTicket> detail::bumpGeneration(QSqlDatabase& db, const BookId& book, bool metadata)
{
    return bumpGenerationColumn(db, book, metadata ? "metadata_generation" : "toc_generation");
}

Result<RunId> publishMetadata(QSqlDatabase& db, const PublishTicket& ticket, const RunIdentity& run,
                              const ExtractedMetadata& m)
{
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    auto id = detail::publishMetadataRun(db, RunId::create(), ticket, run, m);
    if (!id)
        return id;
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return id;
}

Result<RunId> detail::publishMetadataRun(QSqlDatabase& db, const RunId& id, const PublishTicket& ticket,
                                         const RunIdentity& run, const ExtractedMetadata& m)
{
    if (auto row = checkPublishable(db, ticket, run, true); !row)
        return row.error();

    // Values are stored only for resolved fields; ambiguity keeps its status.
    const auto resolvedText = [](FieldStatus s, const std::optional<QString>& v) {
        return s == FieldStatus::Resolved ? nullable(v) : QVariant(QMetaType(QMetaType::QString));
    };
    const auto resolvedInt = [](FieldStatus s, const std::optional<int>& v) {
        return s == FieldStatus::Resolved ? nullable(v) : QVariant(QMetaType(QMetaType::Int));
    };
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO metadata_runs(id, book_id, generation, source_sha256, sdk_version, model_identity, "
        "options_json, outcome, report_path, created_at, title_status, title, subtitle, contributors_status, "
        "edition_status, edition_statement, edition_ordinal, publication_year_status, publication_year, "
        "copyright_year_status, copyright_year) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    bindRunIdentity(q, id, ticket, run);
    q.addBindValue(toCode(m.titleStatus));
    q.addBindValue(resolvedText(m.titleStatus, m.title));
    q.addBindValue(resolvedText(m.titleStatus, m.subtitle));
    q.addBindValue(toCode(m.contributorsStatus));
    q.addBindValue(toCode(m.editionStatus));
    q.addBindValue(resolvedText(m.editionStatus, m.editionStatement));
    q.addBindValue(resolvedInt(m.editionStatus, m.editionOrdinal));
    q.addBindValue(toCode(m.publicationYearStatus));
    q.addBindValue(resolvedInt(m.publicationYearStatus, m.publicationYear));
    q.addBindValue(toCode(m.copyrightYearStatus));
    q.addBindValue(resolvedInt(m.copyrightYearStatus, m.copyrightYear));
    if (!q.exec())
        return sqlError(q);

    if (m.contributorsStatus == FieldStatus::Resolved) {
        q.prepare(QStringLiteral(
            "INSERT INTO metadata_run_contributors(run_id, position, name, role) VALUES (?, ?, ?, ?)"));
        for (qsizetype i = 0; i < m.contributors.size(); ++i) {
            q.addBindValue(id.toString());
            q.addBindValue(int(i));
            q.addBindValue(m.contributors.at(i).name);
            q.addBindValue(toCode(m.contributors.at(i).role));
            if (!q.exec())
                return sqlError(q);
        }
    }

    q.prepare(QStringLiteral("UPDATE books SET active_metadata_run_id = ? WHERE id = ?"));
    q.addBindValue(id.toString());
    q.addBindValue(ticket.book.toString());
    if (!q.exec())
        return sqlError(q);
    if (auto s = touchBook(db, ticket.book); !s)
        return s.error();
    if (auto s = refreshProjection(db, ticket.book); !s)
        return s.error();
    return id;
}

Result<RunId> publishToc(QSqlDatabase& db, const PublishTicket& ticket, const RunIdentity& run,
                         const TocAnalysis& toc)
{
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    auto id = detail::publishTocRun(db, RunId::create(), ticket, run, toc);
    if (!id)
        return id;
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return id;
}

Result<RunId> detail::publishTocRun(QSqlDatabase& db, const RunId& id, const PublishTicket& ticket,
                                    const RunIdentity& run, const TocAnalysis& toc)
{
    auto row = checkPublishable(db, ticket, run, false);
    if (!row)
        return row.error();
    const std::optional<int> pageCount = row.value().asset.pageCount;

    // The run identity is the one stored outcome; a differing analysis value
    // is a caller error, not something to choose between silently.
    if (toc.outcome != run.outcome) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("TOC outcome \"%1\" differs from the run outcome \"%2\".")
                             .arg(toc.outcome, run.outcome));
    }

    for (const TocEntry& e : toc.entries) {
        if (e.sdkEntryId.isEmpty())
            return makeError(ErrorCode::InvalidArgument, QStringLiteral("A TOC entry has no SDK entry ID."));
        if (e.destinationPage && pageCount && *e.destinationPage >= *pageCount) {
            return makeError(ErrorCode::InvalidArgument,
                             QStringLiteral("Entry %1 points to page index %2 beyond the %3-page document.")
                                 .arg(e.sdkEntryId)
                                 .arg(*e.destinationPage)
                                 .arg(*pageCount));
        }
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO toc_runs(id, book_id, generation, source_sha256, sdk_version, model_identity, "
        "options_json, outcome, report_path, created_at, plan_ready, parse_complete, search_covered_document, "
        "plan_blockers_json, stop_reasons_json, plan_json) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    bindRunIdentity(q, id, ticket, run);
    q.addBindValue(toc.planReady ? 1 : 0);
    q.addBindValue(nullableBool(toc.parseComplete));
    q.addBindValue(nullableBool(toc.searchCoveredDocument));
    q.addBindValue(compactJson(QJsonArray::fromStringList(toc.planBlockers)));
    q.addBindValue(compactJson(QJsonArray::fromStringList(toc.stopReasons)));
    q.addBindValue(toc.planJson.isEmpty() ? QVariant(QMetaType(QMetaType::QString))
                                          : QVariant(QString::fromUtf8(toc.planJson)));
    if (!q.exec())
        return sqlError(q);

    q.prepare(QStringLiteral(
        "INSERT INTO toc_entries(run_id, sdk_entry_id, entry_order, title, hierarchy, parent_sdk_entry_id, "
        "printed_label, destination_state, destination_page, source_toc_page, in_export_plan, evidence_json) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (const TocEntry& e : normalizeHierarchy(toc.entries)) {
        q.addBindValue(id.toString());
        q.addBindValue(e.sdkEntryId);
        q.addBindValue(e.order);
        q.addBindValue(e.title);
        q.addBindValue(toCode(e.hierarchy));
        q.addBindValue(nullable(e.parentSdkEntryId));
        q.addBindValue(nullable(e.printedLabel));
        q.addBindValue(toCode(e.destinationState));
        q.addBindValue(nullable(e.destinationPage));
        q.addBindValue(nullable(e.sourceTocPage));
        q.addBindValue(e.inExportPlan ? 1 : 0);
        q.addBindValue(tocEvidenceJson(e.evidence));
        if (!q.exec())
            return makeError(ErrorCode::InvalidArgument,
                             QStringLiteral("TOC entry %1 rejected: %2").arg(e.sdkEntryId, q.lastError().text()));
    }

    q.prepare(QStringLiteral("UPDATE books SET active_toc_run_id = ? WHERE id = ?"));
    q.addBindValue(id.toString());
    q.addBindValue(ticket.book.toString());
    if (!q.exec())
        return sqlError(q);
    // The user's edits stay; they move to this run only if its entries are the same.
    if (auto s = detail::carryTocEdits(db, ticket.book, id); !s)
        return s.error();
    if (auto s = touchBook(db, ticket.book); !s)
        return s.error();
    if (auto s = refreshProjection(db, ticket.book); !s)
        return s.error();
    return id;
}

Status setOverride(QSqlDatabase& db, const BookId& book, MetadataField field, const MetadataOverride& value)
{
    if (auto s = validateOverride(field, value); !s)
        return s;
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    if (auto row = loadBookRow(db, book); !row)
        return row.error();

    QSqlQuery q(db);
    const QString fieldCode = toCode(field);
    q.prepare(QStringLiteral("DELETE FROM metadata_overrides WHERE book_id = ? AND field = ?"));
    q.addBindValue(book.toString());
    q.addBindValue(fieldCode);
    if (!q.exec())
        return sqlError(q);
    if (field == MetadataField::Contributors) {
        q.prepare(QStringLiteral("DELETE FROM metadata_override_contributors WHERE book_id = ?"));
        q.addBindValue(book.toString());
        if (!q.exec())
            return sqlError(q);
    }

    if (value.mode != OverrideMode::Auto) {
        q.prepare(QStringLiteral(
            "INSERT INTO metadata_overrides(book_id, field, mode, value_text, value_int, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?)"));
        q.addBindValue(book.toString());
        q.addBindValue(fieldCode);
        q.addBindValue(toCode(value.mode));
        const bool isValue = value.mode == OverrideMode::Value;
        std::optional<QString> text;
        if (isValue && value.text)
            text = value.text->trimmed();
        q.addBindValue(nullable(text));
        q.addBindValue(isValue ? nullable(value.year) : QVariant(QMetaType(QMetaType::Int)));
        q.addBindValue(now());
        if (!q.exec())
            return sqlError(q);

        if (isValue && field == MetadataField::Contributors) {
            q.prepare(QStringLiteral(
                "INSERT INTO metadata_override_contributors(book_id, position, name, role) VALUES (?, ?, ?, ?)"));
            for (qsizetype i = 0; i < value.contributors.size(); ++i) {
                q.addBindValue(book.toString());
                q.addBindValue(int(i));
                q.addBindValue(value.contributors.at(i).name.trimmed());
                q.addBindValue(toCode(value.contributors.at(i).role));
                if (!q.exec())
                    return sqlError(q);
            }
        }
    }
    if (auto s = touchBook(db, book); !s)
        return s;
    if (auto s = refreshProjection(db, book); !s)
        return s;
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return Done{};
}

Status trashBook(QSqlDatabase& db, const BookId& book)
{
    return setLifecycle(db, book, Lifecycle::Trashed);
}

Status restoreBook(QSqlDatabase& db, const BookId& book)
{
    return setLifecycle(db, book, Lifecycle::Active);
}

Result<QList<BookSummary>> listBooks(QSqlDatabase& db, Lifecycle lifecycle)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id FROM books WHERE lifecycle = ? ORDER BY created_at, id"));
    q.addBindValue(toCode(lifecycle));
    if (!q.exec())
        return sqlError(q);
    QList<BookId> ids;
    while (q.next())
        ids << BookId::fromString(q.value(0).toString());

    QList<BookSummary> books;
    for (const BookId& id : ids) {
        auto row = loadBookRow(db, id);
        if (!row)
            return row.error();
        auto computed = compute(db, row.value());
        if (!computed)
            return computed.error();
        books << computed.value().summary;
    }
    return books;
}

Result<BookDetails> bookDetails(QSqlDatabase& db, const BookId& book)
{
    auto row = loadBookRow(db, book);
    if (!row)
        return row.error();
    auto computed = compute(db, row.value());
    if (!computed)
        return computed.error();
    BookDetails d;
    d.summary = computed.value().summary;
    d.asset = row.value().asset;
    d.originalFileName = row.value().originalFileName;
    d.originalPath = row.value().originalPath;
    d.extracted = computed.value().extracted;
    d.overrides = computed.value().overrides;
    if (computed.value().toc)
        d.toc = computed.value().toc->analysis;
    d.tocRevision = computed.value().tocRevision;
    d.analyzedToc = computed.value().analyzedToc;
    d.metadataRun = row.value().activeMetadataRun;
    d.tocRun = row.value().activeTocRun;
    d.metadataGeneration = row.value().metadataGeneration;
    d.tocGeneration = row.value().tocGeneration;
    return d;
}

Status rebuildSearchIndex(QSqlDatabase& db)
{
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    QString error;
    if (!search::clearProjections(db, &error))
        return makeError(ErrorCode::Database, error);
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT id FROM books WHERE lifecycle = 'active'")))
        return sqlError(q);
    QList<BookId> ids;
    while (q.next())
        ids << BookId::fromString(q.value(0).toString());
    q.finish();
    for (const BookId& id : ids) {
        if (auto s = refreshProjection(db, id); !s)
            return s;
    }
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return Done{};
}

} // namespace mbl::catalog
