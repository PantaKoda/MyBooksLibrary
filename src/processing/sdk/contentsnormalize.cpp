#include "processing/sdk/contentsnormalize.h"

#include <QHash>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace mbl::sdk {

namespace pb = pdfbookmark;
using namespace mbl::domain;

namespace {

QString text(const std::string& s)
{
    return QString::fromStdString(s);
}

QStringList strings(const std::vector<std::string>& in)
{
    QStringList out;
    for (const std::string& s : in)
        out << text(s);
    return out;
}

HierarchyState hierarchy(pb::parsing::HierarchyKind kind)
{
    switch (kind) {
    case pb::parsing::HierarchyKind::Root: return HierarchyState::Root;
    case pb::parsing::HierarchyKind::KnownParent: return HierarchyState::KnownParent;
    case pb::parsing::HierarchyKind::Unknown: return HierarchyState::Unknown;
    }
    return HierarchyState::Unknown;
}

DestinationState destination(pb::mapping::MappingStatus status)
{
    switch (status) {
    case pb::mapping::MappingStatus::Resolved: return DestinationState::Resolved;
    case pb::mapping::MappingStatus::Ambiguous: return DestinationState::Ambiguous;
    case pb::mapping::MappingStatus::Unresolved: return DestinationState::Unresolved;
    }
    return DestinationState::Unresolved;
}

// Stable codes, as in the SDK's JSON reports.
QString methodCode(pb::mapping::ResolutionMethod method)
{
    switch (method) {
    case pb::mapping::ResolutionMethod::ManualEntry: return QStringLiteral("manual_entry");
    case pb::mapping::ResolutionMethod::ManualOffset: return QStringLiteral("manual_offset");
    case pb::mapping::ResolutionMethod::AssociatedLocalLink: return QStringLiteral("associated_local_link");
    case pb::mapping::ResolutionMethod::ViewerLabel: return QStringLiteral("viewer_label");
    case pb::mapping::ResolutionMethod::InferredOffset: return QStringLiteral("inferred_offset");
    case pb::mapping::ResolutionMethod::HeadingMatch: return QStringLiteral("heading_match");
    }
    return QStringLiteral("unknown");
}

// Distinct pages of the sources, in first-seen order.
QList<int> pagesOf(const std::vector<pb::text::SourceReference>& sources)
{
    QList<int> pages;
    for (const auto& s : sources) {
        const int page = int(s.page_index);
        if (!pages.contains(page))
            pages << page;
    }
    return pages;
}

} // namespace

TocAnalysis normalizeContents(const pb::AnalysisReport& report)
{
    TocAnalysis toc;
    toc.outcome = QString::fromUtf8(pb::outcome_name(report.outcome));
    toc.planReady = report.plan.ready;
    toc.searchCoveredDocument = report.search_covered_document;
    toc.planBlockers = strings(report.plan.blockers);
    toc.stopReasons = strings(report.stop_reasons);
    if (report.plan.plan)
        toc.planJson = QByteArray::fromStdString(pb::plan_to_json(*report.plan.plan));
    if (!report.parsed)
        return toc;  // No TOC found: no entries, not an error.

    const auto& parsed = *report.parsed;
    toc.parseComplete = parsed.completeness == pb::parsing::ParseCompleteness::Complete;

    QHash<QString, const pb::mapping::EntryMapping*> mappings;
    if (report.mapping) {
        for (const auto& m : report.mapping->entries)
            mappings.insert(text(m.entry_id), &m);
    }
    QSet<QString> inPlan;
    QHash<QString, QString> omitted;
    if (report.plan.plan) {
        for (const auto& node : report.plan.plan->nodes)
            inPlan.insert(text(node.id));
        for (const auto& o : report.plan.plan->omitted_entries)
            omitted.insert(text(o.entry_id), text(o.reason));
    }

    for (const auto& p : parsed.entries) {
        TocEntry e;
        e.sdkEntryId = text(p.id);
        e.order = int(p.order);
        e.title = text(p.title);
        e.hierarchy = hierarchy(p.hierarchy.kind);
        if (e.hierarchy == HierarchyState::KnownParent && p.hierarchy.parent_id)
            e.parentSdkEntryId = text(*p.hierarchy.parent_id);
        e.evidence.hierarchyReasons = strings(p.hierarchy.reasons);
        if (p.printed_reference) {
            e.printedLabel = text(p.printed_reference->literal);
            e.evidence.printedLabelUncertain = p.printed_reference->uncertain;
            e.evidence.printedLabelReasons = strings(p.printed_reference->reasons);
        }
        e.evidence.sourcePages = pagesOf(p.sources);
        if (!e.evidence.sourcePages.isEmpty())
            e.sourceTocPage = e.evidence.sourcePages.first();
        e.evidence.diagnostics = strings(p.diagnostics);

        if (const auto* m = mappings.value(e.sdkEntryId)) {
            e.destinationState = destination(m->status);
            if (e.destinationState == DestinationState::Resolved && m->pdf_page_index)
                e.destinationPage = int(*m->pdf_page_index);
            else if (e.destinationState == DestinationState::Resolved)
                e.destinationState = DestinationState::Unresolved;  // Resolved without a page: not usable.
            if (m->method)
                e.evidence.destinationMethod = methodCode(*m->method);
            e.evidence.destinationReasons = strings(m->reasons);
            for (const auto& alternative : m->alternatives) {
                const int page = int(alternative.pdf_page_index);
                if (!e.evidence.alternativePages.contains(page))
                    e.evidence.alternativePages << page;
            }
        } else {
            e.destinationState = DestinationState::Unresolved;
            e.evidence.destinationReasons << QStringLiteral("The entry was not mapped to a page.");
        }

        e.inExportPlan = inPlan.contains(e.sdkEntryId);
        if (const auto reason = omitted.constFind(e.sdkEntryId); reason != omitted.cend())
            e.evidence.omissionReason = reason.value();
        toc.entries << e;
    }
    std::stable_sort(toc.entries.begin(), toc.entries.end(),
                     [](const TocEntry& a, const TocEntry& b) { return a.order < b.order; });
    return toc;
}

} // namespace mbl::sdk
