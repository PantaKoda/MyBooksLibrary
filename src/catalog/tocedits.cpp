#include "catalog/tocedits.h"

#include "catalog/catalog_internal.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace mbl::catalog {

using namespace mbl::domain;
using detail::now;
using detail::sqlError;
using detail::Transaction;

namespace {

QVariant nullableText(const std::optional<QString>& v)
{
    return v ? QVariant(*v) : QVariant(QMetaType(QMetaType::QString));
}

QVariant nullableInt(const std::optional<int>& v)
{
    return v ? QVariant(*v) : QVariant(QMetaType(QMetaType::Int));
}

std::optional<QString> optText(const QVariant& v)
{
    return v.isNull() ? std::nullopt : std::optional<QString>(v.toString());
}

std::optional<int> optInt(const QVariant& v)
{
    return v.isNull() ? std::nullopt : std::optional<int>(v.toInt());
}

struct BookTocState {
    Lifecycle lifecycle = Lifecycle::Active;
    std::optional<int> pageCount;
    std::optional<RunId> activeRun;
    std::optional<TocRevisionId> activeRevision;
};

Result<BookTocState> loadState(QSqlDatabase& db, const BookId& book)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT b.lifecycle, a.page_count, b.active_toc_run_id, b.active_toc_revision_id "
                             "FROM books b JOIN assets a ON a.id = b.asset_id WHERE b.id = ?"));
    q.addBindValue(book.toString());
    if (!q.exec())
        return sqlError(q);
    if (!q.next())
        return makeError(ErrorCode::NotFound, QStringLiteral("No book %1.").arg(book.toString()));
    BookTocState s;
    s.lifecycle = lifecycleFromCode(q.value(0).toString()).value_or(Lifecycle::Active);
    s.pageCount = optInt(q.value(1));
    if (!q.value(2).isNull())
        s.activeRun = RunId::fromString(q.value(2).toString());
    if (!q.value(3).isNull())
        s.activeRevision = TocRevisionId::fromString(q.value(3).toString());
    return s;
}

// Everything that changes the book's contents is refused for a trashed book
// and when the caller's view is out of date.
Status checkEditable(const BookTocState& s, const TocEditBase& base)
{
    if (s.lifecycle == Lifecycle::Trashed)
        return makeError(ErrorCode::Trashed, QStringLiteral("The book is in Trash."));
    if (s.activeRun != base.run || s.activeRevision != base.revision) {
        return makeError(ErrorCode::StaleGeneration,
                         QStringLiteral("The contents changed since they were shown; reload them and try again."));
    }
    return Done{};
}

// An entry being edited, with the base run's entry it corresponds to.
struct Working {
    TocEntry entry;
    std::optional<QString> baseSdkEntryId;
};

struct Current {
    RunId baseRun;
    QList<Working> entries;
};

Result<Current> currentEntries(QSqlDatabase& db, const BookTocState& s)
{
    Current c;
    if (s.activeRevision) {
        auto revision = detail::loadTocRevision(db, *s.activeRevision);
        if (!revision)
            return revision.error();
        c.baseRun = revision.value().baseRun;
        for (qsizetype i = 0; i < revision.value().entries.size(); ++i)
            c.entries << Working{revision.value().entries.at(i), revision.value().baseSdkEntryIds.at(i)};
        return c;
    }
    auto entries = detail::loadRunTocEntries(db, *s.activeRun);
    if (!entries)
        return entries.error();
    c.baseRun = *s.activeRun;
    for (const TocEntry& e : entries.value())
        c.entries << Working{e, e.sdkEntryId};  // The first revision keys entries by their SDK ID.
    return c;
}

qsizetype indexOf(const QList<Working>& list, const QString& key)
{
    for (qsizetype i = 0; i < list.size(); ++i) {
        if (list.at(i).entry.sdkEntryId == key)
            return i;
    }
    return -1;
}

std::optional<QString> parentOf(const TocEntry& e)
{
    return e.hierarchy == HierarchyState::KnownParent ? e.parentSdkEntryId : std::nullopt;
}

// The entry's sub-entries at any depth. Parents may come after their children.
QList<qsizetype> descendantsOf(const QList<Working>& list, const QString& key)
{
    QHash<QString, QList<qsizetype>> children;
    for (qsizetype i = 0; i < list.size(); ++i) {
        if (const auto parent = parentOf(list.at(i).entry))
            children[*parent] << i;
    }
    QList<qsizetype> out;
    QList<QString> pending{key};
    QSet<QString> seen{key};
    while (!pending.isEmpty()) {
        const QString next = pending.takeFirst();
        for (qsizetype child : children.value(next)) {
            const QString childKey = list.at(child).entry.sdkEntryId;
            if (seen.contains(childKey))
                continue;
            seen.insert(childKey);
            out << child;
            pending << childKey;
        }
    }
    return out;
}

// True if `ancestor` is `key` or above it in the hierarchy.
bool isSelfOrAncestor(const QList<Working>& list, const QString& ancestor, const QString& key)
{
    std::optional<QString> current = key;
    for (qsizetype steps = 0; current && steps <= list.size(); ++steps) {
        if (*current == ancestor)
            return true;
        const qsizetype i = indexOf(list, *current);
        current = i < 0 ? std::nullopt : parentOf(list.at(i).entry);
    }
    return false;
}

void markEdited(TocEntry& e, const char* what)
{
    const QString tag = QLatin1String(what);
    if (!e.edits.contains(tag))
        e.edits << tag;
}

Error invalid(const QString& message)
{
    return makeError(ErrorCode::InvalidArgument, message);
}

Status checkPage(int page, const std::optional<int>& pageCount)
{
    if (page < 0 || (pageCount && page >= *pageCount)) {
        return invalid(pageCount ? QStringLiteral("Page index %1 is outside the %2-page document.").arg(page).arg(*pageCount)
                                 : QStringLiteral("Page index %1 is not a page.").arg(page));
    }
    return Done{};
}

Status apply(QList<Working>& list, const TocEdit& edit, const std::optional<int>& pageCount)
{
    const qsizetype at = edit.entryKey.isEmpty() ? -1 : indexOf(list, edit.entryKey);
    if (at < 0 && !(edit.kind == TocEdit::Kind::Add && edit.entryKey.isEmpty()))
        return invalid(QStringLiteral("No contents entry \"%1\".").arg(edit.entryKey));

    switch (edit.kind) {
    case TocEdit::Kind::Rename: {
        const QString title = edit.title.trimmed();
        if (title.isEmpty())
            return invalid(QStringLiteral("A contents entry needs a title."));
        list[at].entry.title = title;
        markEdited(list[at].entry, "title");
        return Done{};
    }
    case TocEdit::Kind::SetPage: {
        if (!edit.page)
            return invalid(QStringLiteral("A page is required."));
        if (auto s = checkPage(*edit.page, pageCount); !s)
            return s;
        TocEntry& e = list[at].entry;
        e.destinationState = DestinationState::Resolved;
        e.destinationPage = *edit.page;
        markEdited(e, "page");
        return Done{};
    }
    case TocEdit::Kind::ClearPage: {
        TocEntry& e = list[at].entry;
        e.destinationState = DestinationState::Unresolved;
        e.destinationPage.reset();
        markEdited(e, "page");
        return Done{};
    }
    case TocEdit::Kind::SetParent: {
        const qsizetype parent = indexOf(list, edit.parentKey);
        if (parent < 0)
            return invalid(QStringLiteral("No contents entry \"%1\" to place it under.").arg(edit.parentKey));
        if (isSelfOrAncestor(list, edit.entryKey, edit.parentKey))
            return invalid(QStringLiteral("An entry cannot be placed under itself or one of its sub-entries."));
        TocEntry& e = list[at].entry;
        e.hierarchy = HierarchyState::KnownParent;
        e.parentSdkEntryId = edit.parentKey;
        markEdited(e, "level");
        return Done{};
    }
    case TocEdit::Kind::MakeRoot: {
        TocEntry& e = list[at].entry;
        e.hierarchy = HierarchyState::Root;
        e.parentSdkEntryId.reset();
        markEdited(e, "level");
        return Done{};
    }
    case TocEdit::Kind::Remove:
    case TocEdit::Kind::Restore: {
        const bool remove = edit.kind == TocEdit::Kind::Remove;
        if (!remove) {
            if (const auto parent = parentOf(list.at(at).entry)) {
                const qsizetype p = indexOf(list, *parent);
                if (p >= 0 && list.at(p).entry.removed)
                    return invalid(QStringLiteral("Restore the entry it belongs under first."));
            }
        }
        list[at].entry.removed = remove;
        for (qsizetype d : descendantsOf(list, edit.entryKey))
            list[d].entry.removed = remove;
        return Done{};
    }
    case TocEdit::Kind::Add: {
        TocEntry e;
        e.sdkEntryId = QStringLiteral("added-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        e.title = edit.title.trimmed();
        if (e.title.isEmpty())
            return invalid(QStringLiteral("A contents entry needs a title."));
        if (edit.page) {
            if (auto s = checkPage(*edit.page, pageCount); !s)
                return s;
            e.destinationState = DestinationState::Resolved;
            e.destinationPage = *edit.page;
        }
        e.edits << QStringLiteral("added");
        qsizetype insertAt = 0;
        if (at >= 0) {
            // A sibling of the anchor, after the anchor and its sub-entries.
            const TocEntry& anchor = list.at(at).entry;
            e.hierarchy = anchor.hierarchy;
            e.parentSdkEntryId = anchor.parentSdkEntryId;
            // Visible unless its parent is removed; a removed neighbour does not hide it.
            const auto parent = parentOf(anchor);
            const qsizetype p = parent ? indexOf(list, *parent) : -1;
            e.removed = p >= 0 && list.at(p).entry.removed;
            insertAt = at + 1;
            for (qsizetype d : descendantsOf(list, edit.entryKey))
                insertAt = qMax(insertAt, d + 1);
        } else {
            e.hierarchy = HierarchyState::Root;
        }
        list.insert(insertAt, Working{e, std::nullopt});
        return Done{};
    }
    }
    return invalid(QStringLiteral("Unknown contents edit."));
}

Result<TocRevisionInfo> insertRevision(QSqlDatabase& db, const BookId& book, const RunId& baseRun,
                                       const std::optional<TocRevisionId>& previous, const QList<Working>& entries)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT COALESCE(MAX(number), 0) + 1 FROM toc_edit_revisions WHERE book_id = ?"));
    q.addBindValue(book.toString());
    if (!q.exec() || !q.next())
        return sqlError(q);
    TocRevisionInfo info;
    info.id = TocRevisionId::create();
    info.number = q.value(0).toInt();
    info.baseRun = baseRun;
    q.finish();

    q.prepare(QStringLiteral("INSERT INTO toc_edit_revisions(id, book_id, number, base_run_id, previous_revision_id, "
                             "created_at) VALUES (?, ?, ?, ?, ?, ?)"));
    q.addBindValue(info.id.toString());
    q.addBindValue(book.toString());
    q.addBindValue(info.number);
    q.addBindValue(baseRun.toString());
    q.addBindValue(previous ? QVariant(previous->toString()) : QVariant(QMetaType(QMetaType::QString)));
    q.addBindValue(now());
    if (!q.exec())
        return sqlError(q);

    q.prepare(QStringLiteral(
        "INSERT INTO toc_edit_entries(revision_id, entry_key, base_sdk_entry_id, entry_order, title, hierarchy, "
        "parent_entry_key, printed_label, destination_state, destination_page, source_toc_page, in_export_plan, "
        "evidence_json, edits_json, removed) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (qsizetype i = 0; i < entries.size(); ++i) {
        const TocEntry& e = entries.at(i).entry;
        q.addBindValue(info.id.toString());
        q.addBindValue(e.sdkEntryId);
        q.addBindValue(nullableText(entries.at(i).baseSdkEntryId));
        q.addBindValue(int(i));
        q.addBindValue(e.title);
        q.addBindValue(toCode(e.hierarchy));
        q.addBindValue(nullableText(e.hierarchy == HierarchyState::KnownParent ? e.parentSdkEntryId : std::nullopt));
        q.addBindValue(nullableText(e.printedLabel));
        q.addBindValue(toCode(e.destinationState));
        q.addBindValue(nullableInt(e.destinationState == DestinationState::Resolved ? e.destinationPage : std::nullopt));
        q.addBindValue(nullableInt(e.sourceTocPage));
        q.addBindValue(e.inExportPlan ? 1 : 0);
        q.addBindValue(detail::tocEvidenceToJson(e.evidence));
        q.addBindValue(QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(e.edits)).toJson(QJsonDocument::Compact)));
        q.addBindValue(e.removed ? 1 : 0);
        if (!q.exec())
            return invalid(QStringLiteral("Contents entry \"%1\" rejected: %2").arg(e.title, q.lastError().text()));
    }

    q.prepare(QStringLiteral("UPDATE books SET active_toc_revision_id = ? WHERE id = ?"));
    q.addBindValue(info.id.toString());
    q.addBindValue(book.toString());
    if (!q.exec())
        return sqlError(q);
    return info;
}

Status finish(QSqlDatabase& db, const BookId& book, Transaction& tx)
{
    if (auto s = detail::touchBook(db, book); !s)
        return s;
    if (auto s = detail::refreshBookProjection(db, book); !s)
        return s;
    if (!tx.commit())
        return sqlError(db, QStringLiteral("commit"));
    return Done{};
}

// Same entries in content: titles, labels, pages, source pages and structure
// (parents compared by position in each list). Plan membership and evidence
// may differ; they are taken from the new run.
bool sameContents(const QList<TocEntry>& a, const QList<TocEntry>& b)
{
    if (a.size() != b.size())
        return false;
    QHash<QString, qsizetype> indexA;
    QHash<QString, qsizetype> indexB;
    for (qsizetype i = 0; i < a.size(); ++i) {
        indexA.insert(a.at(i).sdkEntryId, i);
        indexB.insert(b.at(i).sdkEntryId, i);
    }
    for (qsizetype i = 0; i < a.size(); ++i) {
        const TocEntry& x = a.at(i);
        const TocEntry& y = b.at(i);
        if (x.title != y.title || x.printedLabel != y.printedLabel || x.destinationState != y.destinationState
            || x.destinationPage != y.destinationPage || x.sourceTocPage != y.sourceTocPage
            || x.hierarchy != y.hierarchy)
            return false;
        const auto px = parentOf(x);
        const auto py = parentOf(y);
        if (px.has_value() != py.has_value())
            return false;
        if (px && indexA.value(*px, -1) != indexB.value(*py, -2))
            return false;
    }
    return true;
}

} // namespace

Result<detail::StoredTocRevision> detail::loadTocRevision(QSqlDatabase& db, const TocRevisionId& id)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT book_id, number, base_run_id FROM toc_edit_revisions WHERE id = ?"));
    q.addBindValue(id.toString());
    if (!q.exec())
        return sqlError(q);
    if (!q.next())
        return makeError(ErrorCode::NotFound, QStringLiteral("No contents revision %1.").arg(id.toString()));
    StoredTocRevision r;
    r.id = id;
    r.book = BookId::fromString(q.value(0).toString());
    r.number = q.value(1).toInt();
    r.baseRun = RunId::fromString(q.value(2).toString());

    q.prepare(QStringLiteral(
        "SELECT id, entry_key, base_sdk_entry_id, entry_order, title, hierarchy, parent_entry_key, printed_label, "
        "destination_state, destination_page, source_toc_page, in_export_plan, evidence_json, edits_json, removed "
        "FROM toc_edit_entries WHERE revision_id = ? ORDER BY entry_order, id"));
    q.addBindValue(id.toString());
    if (!q.exec())
        return sqlError(q);
    while (q.next()) {
        TocEntry e;
        e.sdkEntryId = q.value(1).toString();
        e.order = q.value(3).toInt();
        e.title = q.value(4).toString();
        e.hierarchy = hierarchyStateFromCode(q.value(5).toString()).value_or(HierarchyState::Unknown);
        e.parentSdkEntryId = optText(q.value(6));
        e.printedLabel = optText(q.value(7));
        e.destinationState = destinationStateFromCode(q.value(8).toString()).value_or(DestinationState::Unresolved);
        e.destinationPage = optInt(q.value(9));
        e.sourceTocPage = optInt(q.value(10));
        e.inExportPlan = q.value(11).toInt() != 0;
        e.evidence = detail::tocEvidenceFromJson(q.value(12).toString());
        for (const QJsonValue& v : QJsonDocument::fromJson(q.value(13).toString().toUtf8()).array())
            e.edits << v.toString();
        e.removed = q.value(14).toInt() != 0;
        r.rowIds << q.value(0).toLongLong();
        r.baseSdkEntryIds << optText(q.value(2));
        r.entries << e;
    }
    return r;
}

Status detail::carryTocEdits(QSqlDatabase& db, const BookId& book, const RunId& newRun)
{
    auto state = loadState(db, book);
    if (!state)
        return state.error();
    if (!state.value().activeRevision)
        return Done{};
    auto revision = loadTocRevision(db, *state.value().activeRevision);
    if (!revision)
        return revision.error();
    const StoredTocRevision& r = revision.value();
    if (r.baseRun == newRun)
        return Done{};
    auto oldBase = loadRunTocEntries(db, r.baseRun);
    if (!oldBase)
        return oldBase.error();
    auto fresh = loadRunTocEntries(db, newRun);
    if (!fresh)
        return fresh.error();
    if (!sameContents(oldBase.value(), fresh.value()))
        return Done{};  // Changed entries: the user reconciles.

    // Entry for entry, the new run says the same: tie each edited entry to
    // the new run's entry at the same place.
    QHash<QString, qsizetype> oldIndex;
    for (qsizetype i = 0; i < oldBase.value().size(); ++i)
        oldIndex.insert(oldBase.value().at(i).sdkEntryId, i);
    QList<Working> entries;
    for (qsizetype i = 0; i < r.entries.size(); ++i) {
        Working w{r.entries.at(i), std::nullopt};
        if (const auto base = r.baseSdkEntryIds.at(i); base && oldIndex.contains(*base)) {
            const TocEntry& match = fresh.value().at(oldIndex.value(*base));
            w.baseSdkEntryId = match.sdkEntryId;
            w.entry.inExportPlan = match.inExportPlan;
            w.entry.evidence = match.evidence;
        }
        entries << w;
    }
    auto inserted = insertRevision(db, book, newRun, r.id, entries);
    if (!inserted)
        return inserted.error();
    return Done{};
}

Result<TocRevisionInfo> editToc(QSqlDatabase& db, const BookId& book, const TocEditBase& base,
                                const QList<TocEdit>& edits)
{
    if (edits.isEmpty())
        return invalid(QStringLiteral("No contents edits."));
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    auto state = loadState(db, book);
    if (!state)
        return state.error();
    if (auto s = checkEditable(state.value(), base); !s)
        return s.error();
    if (!state.value().activeRun)
        return invalid(QStringLiteral("This book has no analyzed contents to edit yet."));
    auto current = currentEntries(db, state.value());
    if (!current)
        return current.error();
    QList<Working> entries = current.value().entries;
    for (const TocEdit& edit : edits) {
        if (auto s = apply(entries, edit, state.value().pageCount); !s)
            return s.error();
    }
    auto info = insertRevision(db, book, current.value().baseRun, state.value().activeRevision, entries);
    if (!info)
        return info;
    info.value().needsReconciliation = info.value().baseRun != *state.value().activeRun;
    if (auto s = finish(db, book, tx); !s)
        return s.error();
    return info;
}

Result<TocRevisionInfo> keepTocEdits(QSqlDatabase& db, const BookId& book, const TocEditBase& base)
{
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    auto state = loadState(db, book);
    if (!state)
        return state.error();
    if (auto s = checkEditable(state.value(), base); !s)
        return s.error();
    if (!state.value().activeRevision || !state.value().activeRun)
        return invalid(QStringLiteral("This book's contents are not edited."));
    auto revision = detail::loadTocRevision(db, *state.value().activeRevision);
    if (!revision)
        return revision.error();
    if (revision.value().baseRun == *state.value().activeRun)
        return invalid(QStringLiteral("The edits are already based on the current analysis."));
    // The edited entries as they are, no longer tied to any analyzed entry.
    QList<Working> entries;
    for (const TocEntry& e : revision.value().entries)
        entries << Working{e, std::nullopt};
    auto info = insertRevision(db, book, *state.value().activeRun, state.value().activeRevision, entries);
    if (!info)
        return info;
    if (auto s = finish(db, book, tx); !s)
        return s.error();
    return info;
}

Status useAnalyzedToc(QSqlDatabase& db, const BookId& book, const TocEditBase& base)
{
    Transaction tx(db);
    if (!tx.begun())
        return sqlError(db, QStringLiteral("begin"));
    auto state = loadState(db, book);
    if (!state)
        return state.error();
    if (auto s = checkEditable(state.value(), base); !s)
        return s;
    if (!state.value().activeRevision)
        return Done{};  // Already the analyzed contents.
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE books SET active_toc_revision_id = NULL WHERE id = ?"));
    q.addBindValue(book.toString());
    if (!q.exec())
        return sqlError(q);
    return finish(db, book, tx);
}

Result<QList<TocRevisionInfo>> tocRevisions(QSqlDatabase& db, const BookId& book)
{
    auto state = loadState(db, book);
    if (!state)
        return state.error();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id, number, base_run_id FROM toc_edit_revisions WHERE book_id = ? "
                             "ORDER BY number DESC"));
    q.addBindValue(book.toString());
    if (!q.exec())
        return sqlError(q);
    QList<TocRevisionInfo> out;
    while (q.next()) {
        TocRevisionInfo info;
        info.id = TocRevisionId::fromString(q.value(0).toString());
        info.number = q.value(1).toInt();
        info.baseRun = RunId::fromString(q.value(2).toString());
        info.needsReconciliation = state.value().activeRun && info.baseRun != *state.value().activeRun;
        out << info;
    }
    return out;
}

} // namespace mbl::catalog
