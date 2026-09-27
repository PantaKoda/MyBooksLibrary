// Table-of-contents contracts. Every parsed entry is kept, including entries
// without a resolved destination and entries omitted from an export plan.
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace mbl::domain {

enum class HierarchyState { Root, KnownParent, Unknown };
enum class DestinationState { Resolved, Ambiguous, Unresolved };

// Why an entry has its hierarchy and destination: shown to the user as the
// reasons for uncertainty, with technical detail on request. Page indices are
// zero-based physical pages.
struct TocEntryEvidence {
    QList<int> sourcePages;                 // Pages the entry was printed on (usually the TOC page).
    QStringList hierarchyReasons;
    bool printedLabelUncertain = false;
    QStringList printedLabelReasons;
    std::optional<QString> destinationMethod;  // SDK method code, e.g. "inferred_offset".
    QStringList destinationReasons;
    QList<int> alternativePages;            // Candidate destinations of an ambiguous entry.
    std::optional<QString> omissionReason;  // Why the plan left the entry out.
    QStringList diagnostics;
};

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
    TocEntryEvidence evidence;
};

// Normalised result of one TOC analysis run.
struct TocAnalysis {
    QString outcome;          // SDK outcome name, e.g. "plan_ready"; must equal RunIdentity::outcome.
    bool planReady = false;   // Stored separately from entry coverage.
    QList<TocEntry> entries;  // All parsed entries, joined to mappings by entry ID.
    std::optional<bool> parseComplete;         // Absent when no TOC was parsed.
    std::optional<bool> searchCoveredDocument; // The search reached the last page.
    QStringList planBlockers;                  // Why no ready plan was produced.
    QStringList stopReasons;                   // Why the search stopped.
    QByteArray planJson;                       // The SDK's plan (draft or ready), empty if none.
};

QString toCode(HierarchyState state);
std::optional<HierarchyState> hierarchyStateFromCode(const QString& code);
QString toCode(DestinationState state);
std::optional<DestinationState> destinationStateFromCode(const QString& code);

} // namespace mbl::domain
