// Table-of-contents contracts. Every parsed entry is kept, including entries
// without a resolved destination and entries omitted from an export plan.
#pragma once

#include <QList>
#include <QString>

#include <optional>

namespace mbl::domain {

enum class HierarchyState { Root, KnownParent, Unknown };
enum class DestinationState { Resolved, Ambiguous, Unresolved };

struct TocEntry {
    QString sdkEntryId;                     // Scoped to its run; not stable across reruns.
    int order = 0;                          // Order within the parsed TOC.
    QString title;                          // UTF-8, as parsed.
    HierarchyState hierarchy = HierarchyState::Unknown;
    std::optional<QString> parentSdkEntryId; // Only for KnownParent; may refer to a later entry.
    std::optional<QString> printedLabel;     // "iv", "12", "A-12": never a physical index.
    DestinationState destinationState = DestinationState::Unresolved;
    std::optional<int> destinationPage;      // Zero-based physical page; only when Resolved. 0 is valid.
    std::optional<int> sourceTocPage;        // Zero-based page where the entry was printed.
    bool inExportPlan = false;               // False for entries the plan omitted.
};

// Normalised result of one TOC analysis run.
struct TocAnalysis {
    QString outcome;          // SDK outcome name, e.g. "plan_ready", "no_toc_found_in_search".
    bool planReady = false;   // Stored separately from entry coverage.
    QList<TocEntry> entries;  // All parsed entries, joined to mappings by entry ID.
};

QString toCode(HierarchyState state);
std::optional<HierarchyState> hierarchyStateFromCode(const QString& code);
QString toCode(DestinationState state);
std::optional<DestinationState> destinationStateFromCode(const QString& code);

} // namespace mbl::domain
