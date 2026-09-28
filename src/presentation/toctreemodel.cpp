#include "presentation/toctreemodel.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace mbl::presentation {

using namespace mbl::domain;
using namespace Qt::StringLiterals;

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("TocTreeModel", text);
}

// Stored index to the internal id of a QModelIndex (entry + 1; 0 is unused).
quintptr idOf(int entry)
{
    return quintptr(entry + 1);
}

int entryOf(const QModelIndex& index)
{
    return int(index.internalId()) - 1;
}

QString methodText(const QString& code)
{
    if (code == QLatin1String("inferred_offset"))
        return tr("found by matching the printed page numbers");
    if (code == QLatin1String("viewer_label"))
        return tr("found from the PDF's page labels");
    if (code == QLatin1String("heading_match"))
        return tr("found by matching the chapter heading");
    if (code == QLatin1String("associated_local_link"))
        return tr("found from a link in the contents");
    if (code == QLatin1String("manual_entry") || code == QLatin1String("manual_offset"))
        return tr("set by hand");
    return {};
}

} // namespace

TocTreeModel::TocTreeModel(QObject* parent) : QAbstractItemModel(parent) {}

void TocTreeModel::setEntries(const QList<TocEntry>& entries)
{
    beginResetModel();
    m_entries = entries;
    std::stable_sort(m_entries.begin(), m_entries.end(),
                     [](const TocEntry& a, const TocEntry& b) { return a.order < b.order; });
    m_nodes = QList<Node>(m_entries.size());
    m_roots.clear();

    // Pass 1: every entry's position by ID, so a parent listed later is found.
    QHash<QString, int> byId;
    for (int i = 0; i < m_entries.size(); ++i)
        byId.insert(m_entries.at(i).sdkEntryId, i);

    // Pass 2: attach each entry to its named parent, unless the parent is
    // missing or the entry is part of a cycle (its chain of parents comes back
    // to it; a self-reference is the shortest cycle). Cycle members go to the
    // top level, so what remains is a tree; an entry below a cycle member
    // stays attached to it.
    const auto parentOf = [&](int i) {
        const TocEntry& e = m_entries.at(i);
        return e.hierarchy == HierarchyState::KnownParent && e.parentSdkEntryId ? byId.value(*e.parentSdkEntryId, -1)
                                                                               : -1;
    };
    for (int i = 0; i < m_entries.size(); ++i) {
        const TocEntry& e = m_entries.at(i);
        if (e.hierarchy != HierarchyState::KnownParent || !e.parentSdkEntryId)
            continue;
        const int parent = parentOf(i);
        bool inCycle = false;
        QSet<int> seen;
        for (int p = parent; p >= 0 && !seen.contains(p); p = parentOf(p)) {
            if (p == i) {
                inCycle = true;
                break;
            }
            seen.insert(p);
        }
        if (parent >= 0 && !inCycle)
            m_nodes[i].parent = parent;
        else
            m_nodes[i].parentProblem = true;
    }
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_nodes.at(i).parent >= 0)
            m_nodes[m_nodes.at(i).parent].children << i;
        else
            m_roots << i;
    }
    endResetModel();
    emit entriesChanged();
}

int TocTreeModel::rowOf(int entry) const
{
    const int parent = m_nodes.at(entry).parent;
    return int((parent < 0 ? m_roots : m_nodes.at(parent).children).indexOf(entry));
}

QModelIndex TocTreeModel::index(int row, int column, const QModelIndex& parent) const
{
    if (column != 0 || row < 0)
        return {};
    const QList<int>& siblings = parent.isValid() ? m_nodes.at(entryOf(parent)).children : m_roots;
    if (row >= siblings.size())
        return {};
    return createIndex(row, 0, idOf(siblings.at(row)));
}

QModelIndex TocTreeModel::parent(const QModelIndex& child) const
{
    if (!child.isValid())
        return {};
    const int parent = m_nodes.at(entryOf(child)).parent;
    if (parent < 0)
        return {};
    return createIndex(rowOf(parent), 0, idOf(parent));
}

int TocTreeModel::rowCount(const QModelIndex& parent) const
{
    if (parent.column() > 0)
        return 0;
    return int(parent.isValid() ? m_nodes.at(entryOf(parent)).children.size() : m_roots.size());
}

int TocTreeModel::columnCount(const QModelIndex&) const
{
    return 1;
}

QString TocTreeModel::pageText(const TocEntry& e)
{
    switch (e.destinationState) {
    case DestinationState::Resolved:
        return e.destinationPage ? tr("Page %1").arg(*e.destinationPage + 1) : tr("Page not found");
    case DestinationState::Ambiguous: {
        QStringList pages;
        for (int p : e.evidence.alternativePages)
            pages << QString::number(p + 1);
        return pages.isEmpty() ? tr("Page uncertain")
                               : tr("Page %1?").arg(pages.join(QStringLiteral(" %1 ").arg(tr("or"))));
    }
    case DestinationState::Unresolved:
        return tr("Page not found");
    }
    return {};
}

QVariant TocTreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    const int i = entryOf(index);
    if (i < 0 || i >= m_entries.size())
        return {};
    const TocEntry& e = m_entries.at(i);
    const Node& node = m_nodes.at(i);
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
        return e.title;
    case EntryIdRole:
        return e.sdkEntryId;
    case PageTextRole:
        return pageText(e);
    case PageRole:
        return e.destinationState == DestinationState::Resolved && e.destinationPage ? *e.destinationPage + 1 : -1;
    case SourcePageRole:
        return e.sourceTocPage ? *e.sourceTocPage + 1 : -1;
    case PrintedLabelRole:
        return e.printedLabel.value_or(QString());
    case UncertainRole:
        if (e.removed)
            return false;
        return e.destinationState != DestinationState::Resolved || e.hierarchy == HierarchyState::Unknown
               || node.parentProblem || (e.evidence.printedLabelUncertain && !e.edits.contains(u"page"_s));
    case RemovedRole:
        return e.removed;
    case EditedTextRole: {
        if (e.edits.contains(u"added"_s))
            return tr("Added by you");
        QStringList what;
        if (e.edits.contains(u"title"_s))
            what << tr("title");
        if (e.edits.contains(u"page"_s))
            what << tr("page");
        if (e.edits.contains(u"level"_s))
            what << tr("level");
        return what.isEmpty() ? QString() : tr("Changed by you: %1").arg(what.join(QStringLiteral(", ")));
    }
    case InPlanRole:
        return e.inExportPlan;
    case StateTextRole: {
        if (e.removed)
            return tr("removed by you · not searched");
        QStringList parts;
        const bool pageByUser = e.edits.contains(u"page"_s) || e.edits.contains(u"added"_s);
        switch (e.destinationState) {
        case DestinationState::Resolved:
            parts << (pageByUser ? tr("page set by you") : tr("page confirmed"));
            break;
        case DestinationState::Ambiguous:
            parts << tr("several possible pages");
            break;
        case DestinationState::Unresolved:
            parts << (pageByUser ? tr("no page") : tr("page not found"));
            break;
        }
        if (e.hierarchy == HierarchyState::Unknown)
            parts << tr("level uncertain");
        if (node.parentProblem)
            parts << tr("parent not found");
        if (!e.inExportPlan)
            parts << tr("not in the bookmarks");
        return parts.join(QStringLiteral(" · "));
    }
    case DetailRole: {
        QStringList lines;
        // The user's changes first; they replace the analysis's reasons for
        // the same thing, which described values that no longer apply.
        const bool added = e.edits.contains(u"added"_s);
        const bool pageEdited = e.edits.contains(u"page"_s) || added;
        const bool levelEdited = e.edits.contains(u"level"_s) || added;
        if (e.removed)
            lines << tr("You removed this entry. It is kept, but not searched; restore it to bring it back.");
        if (added)
            lines << tr("You added this entry.");
        if (e.edits.contains(u"title"_s))
            lines << tr("You changed its title.");
        if (e.edits.contains(u"page"_s)) {
            lines << (e.destinationState == DestinationState::Resolved
                          ? tr("You set its page to %1.").arg(*e.destinationPage + 1)
                          : tr("You removed its page."));
        }
        if (e.edits.contains(u"level"_s))
            lines << tr("You changed its level.");
        if (e.printedLabel) {
            lines << (e.evidence.printedLabelUncertain ? tr("Printed page number “%1” (uncertain)").arg(*e.printedLabel)
                                                        : tr("Printed page number “%1”").arg(*e.printedLabel));
        }
        if (e.sourceTocPage)
            lines << tr("Listed on page %1 of the PDF").arg(*e.sourceTocPage + 1);
        if (!pageEdited) {
            if (e.destinationState == DestinationState::Resolved && e.evidence.destinationMethod) {
                const QString how = methodText(*e.evidence.destinationMethod);
                if (!how.isEmpty())
                    lines << tr("Page %1, %2").arg(*e.destinationPage + 1).arg(how);
            }
            for (const QString& r : e.evidence.destinationReasons)
                lines << r;
        }
        if (e.hierarchy == HierarchyState::Unknown)
            lines << tr("Its level in the contents could not be determined.");
        if (node.parentProblem)
            lines << tr("It names a parent entry that could not be found; it is shown at the top level.");
        if (!levelEdited) {
            for (const QString& r : e.evidence.hierarchyReasons)
                lines << r;
        }
        if (e.evidence.omissionReason)
            lines << tr("Left out of the bookmarks: %1").arg(*e.evidence.omissionReason);
        return lines.join(u'\n');
    }
    case TechnicalRole: {
        QStringList lines{QStringLiteral("id=%1").arg(e.sdkEntryId)};
        if (!e.edits.isEmpty())
            lines << QStringLiteral("edits=%1").arg(e.edits.join(u','));
        if (e.evidence.destinationMethod)
            lines << QStringLiteral("method=%1").arg(*e.evidence.destinationMethod);
        if (!e.evidence.sourcePages.isEmpty()) {
            QStringList pages;
            for (int p : e.evidence.sourcePages)
                pages << QString::number(p);
            lines << QStringLiteral("source_page_indices=%1").arg(pages.join(u','));
        }
        for (const QString& d : e.evidence.diagnostics)
            lines << d;
        return lines.join(u'\n');
    }
    }
    return {};
}

QHash<int, QByteArray> TocTreeModel::roleNames() const
{
    return {
        {TitleRole, "title"},
        {EntryIdRole, "entryId"},
        {PageTextRole, "pageText"},
        {PageRole, "page"},
        {SourcePageRole, "sourcePage"},
        {PrintedLabelRole, "printedLabel"},
        {StateTextRole, "stateText"},
        {UncertainRole, "uncertain"},
        {InPlanRole, "inPlan"},
        {DetailRole, "detail"},
        {TechnicalRole, "technical"},
        {RemovedRole, "removed"},
        {EditedTextRole, "editedText"},
    };
}

QVariantMap TocTreeModel::entryAt(const QModelIndex& index) const
{
    QVariantMap out;
    if (!index.isValid())
        return out;
    const auto roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        out.insert(QString::fromLatin1(it.value()), data(index, it.key()));
    return out;
}

QModelIndex TocTreeModel::indexOfEntry(const QString& key) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).sdkEntryId == key)
            return createIndex(rowOf(i), 0, idOf(i));
    }
    return {};
}

} // namespace mbl::presentation
