// Table-of-contents contracts. Every parsed entry is kept, including entries
// without a resolved destination and entries omitted from an export plan.
//
// The user's edits never change an analysis run: they are saved as numbered
// revisions of the book's contents, each based on one run (TocRevisionInfo).
#pragma once

#include "domain/ids.h"

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
    // Scoped to its run; not stable across reruns. In edited contents: the
    // entry's key, stable across that book's revisions (parents refer to it).
    QString sdkEntryId;
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
    // Edited contents only: what the user changed ("title", "page", "level",
    // "added"), and whether the entry was removed (kept, but not searched).
    QStringList edits;
    bool removed = false;
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

// The edited revision a book's contents come from.
struct TocRevisionInfo {
    TocRevisionId id;
    int number = 0;       // 1, 2, ... per book; earlier revisions are kept.
    RunId baseRun;        // The analysis the edits were made on.
    // A newer analysis with different entries is active: the edits stay in
    // effect until the user keeps them (keepTocEdits) or returns to the
    // analysis (useAnalyzedToc). Never reconciled automatically.
    bool needsReconciliation = false;
};

// What the caller's view of the contents was based on. An edit is refused
// (StaleGeneration) when the book's active run or revision has changed since.
struct TocEditBase {
    std::optional<RunId> run;
    std::optional<TocRevisionId> revision;
};

// One change to a book's contents. Entries are named by key (TocEntry::sdkEntryId
// of the contents being edited); pages are zero-based physical indices.
struct TocEdit {
    enum class Kind {
        Rename,     // title
        SetPage,    // page: the entry now points there (Resolved)
        ClearPage,  // no page (Unresolved)
        SetParent,  // parentKey: the entry becomes its sub-entry
        MakeRoot,   // a top-level entry
        Remove,     // the entry and its sub-entries; kept and restorable
        Restore,    // the entry and its sub-entries; its parent must not be removed
        Add,        // title, optional page; inserted after entryKey (empty: first) as its sibling
    };
    Kind kind = Kind::Rename;
    QString entryKey;
    QString title;
    std::optional<int> page;
    QString parentKey;
};

QString toCode(HierarchyState state);
std::optional<HierarchyState> hierarchyStateFromCode(const QString& code);
QString toCode(DestinationState state);
std::optional<DestinationState> destinationStateFromCode(const QString& code);

} // namespace mbl::domain
