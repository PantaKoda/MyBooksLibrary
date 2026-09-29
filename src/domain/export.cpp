#include "domain/export.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace mbl::domain {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("ExportPlan", text);
}

std::optional<QString> parentOf(const TocEntry& e)
{
    return e.hierarchy == HierarchyState::KnownParent ? e.parentSdkEntryId : std::nullopt;
}

} // namespace

Result<ExportPlan> buildExportPlan(const TocAnalysis& contents, const AssetRecord& asset)
{
    if (!asset.pageCount || *asset.pageCount <= 0)
        return makeError(ErrorCode::InvalidArgument, tr("The book's page count is not known yet."));

    ExportPlan plan;
    plan.sourceSha256 = asset.sha256;
    plan.pageCount = *asset.pageCount;

    QList<TocEntry> entries = contents.entries;
    std::stable_sort(entries.begin(), entries.end(), [](const TocEntry& a, const TocEntry& b) { return a.order < b.order; });
    QHash<QString, const TocEntry*> byKey;
    for (const TocEntry& e : entries)
        byKey.insert(e.sdkEntryId, &e);

    // Which entries become bookmarks.
    QSet<QString> kept;
    for (const TocEntry& e : entries) {
        if (e.removed) {
            ++plan.removedByUser;
            continue;
        }
        if (e.destinationState != DestinationState::Resolved || !e.destinationPage) {
            plan.omitted << ExportOmission{e.sdkEntryId, e.title,
                                           e.destinationState == DestinationState::Ambiguous
                                               ? tr("More than one possible page; none was chosen.")
                                               : tr("No page was found for it.")};
            continue;
        }
        if (*e.destinationPage < 0 || *e.destinationPage >= plan.pageCount) {
            return makeError(ErrorCode::InvalidArgument,
                             tr("“%1” points to page %2, outside the %3-page book.")
                                 .arg(e.title)
                                 .arg(*e.destinationPage + 1)
                                 .arg(plan.pageCount));
        }
        if (e.title.trimmed().isEmpty()) {
            plan.omitted << ExportOmission{e.sdkEntryId, e.title, tr("It has no title.")};
            continue;
        }
        kept.insert(e.sdkEntryId);
    }

    // Levels: the nearest kept ancestor, recorded when it is not the entry's own parent.
    for (const TocEntry& e : entries) {
        if (!kept.contains(e.sdkEntryId))
            continue;
        ExportNode node{e.sdkEntryId, std::nullopt, e.title.trimmed(), *e.destinationPage};
        const std::optional<QString> original = parentOf(e);
        if (e.hierarchy == HierarchyState::Unknown) {
            plan.uncertainLevels << ExportUncertainLevel{
                node.id, node.title, tr("Its level in the contents is uncertain; it is placed at the top level.")};
        } else if (original) {
            std::optional<QString> parent = original;
            for (qsizetype steps = 0; parent && !kept.contains(*parent) && steps <= entries.size(); ++steps) {
                const TocEntry* p = byKey.value(*parent, nullptr);
                parent = p ? parentOf(*p) : std::nullopt;
            }
            if (parent && !kept.contains(*parent))
                parent.reset();  // A broken chain: the top level.
            node.parentId = parent;
            if (parent != original) {
                const TocEntry* was = byKey.value(*original, nullptr);
                const QString wasTitle = was ? was->title : *original;
                const TocEntry* now = parent ? byKey.value(*parent, nullptr) : nullptr;
                plan.promotions << ExportPromotion{
                    node.id, original, parent,
                    now ? tr("“%1” has no bookmark; it is placed under “%2”.").arg(wasTitle, now->title)
                        : tr("“%1” has no bookmark; it is placed at the top level.").arg(wasTitle)};
            }
        }
        plan.nodes << node;
    }

    if (plan.nodes.isEmpty()) {
        return makeError(ErrorCode::InvalidArgument,
                         tr("No contents entry has a confirmed page, so there is nothing to bookmark."));
    }
    return plan;
}

} // namespace mbl::domain
