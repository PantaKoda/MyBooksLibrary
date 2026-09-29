// Export contracts: the bookmarks a new PDF copy will get, built from a
// book's effective contents (its edited revision, else its analysis). Every
// choice that makes the bookmarks differ from the contents is recorded, so
// partial coverage and flattening are visible, never silent.
#pragma once

#include "domain/book.h"
#include "domain/ids.h"
#include "domain/result.h"
#include "domain/toc.h"

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

#include <optional>

namespace mbl::domain {

struct ExportNode {
    QString id;                       // The contents entry's key.
    std::optional<QString> parentId;  // nullopt: top level. A parent may come later in the list.
    QString title;
    int page = 0;                     // Zero-based physical page.
};

// An entry that gets no bookmark, and why.
struct ExportOmission {
    QString entryId;
    QString title;
    QString reason;
};

// An entry placed under another parent than the contents give it, and why.
// The original and new parents always differ.
struct ExportPromotion {
    QString nodeId;
    std::optional<QString> originalParentId;
    std::optional<QString> newParentId;  // nullopt: top level.
    QString reason;
};

// An entry whose level in the contents is uncertain: it has no parent to be
// moved from, and gets its bookmark at the top level.
struct ExportUncertainLevel {
    QString nodeId;
    QString title;
    QString reason;
};

struct ExportPlan {
    QString sourceSha256;  // The managed source the plan is bound to.
    int pageCount = 0;
    QList<ExportNode> nodes;              // Contents order: sibling order.
    QList<ExportOmission> omitted;        // Entries without a confirmed page.
    QList<ExportPromotion> promotions;    // Moved because their parent has no bookmark.
    QList<ExportUncertainLevel> uncertainLevels;  // Placed at the top level.
    int removedByUser = 0;                // Removed entries: not bookmarks, by choice.

    // Every entry the user kept has a bookmark at its own level.
    bool complete() const { return omitted.isEmpty() && promotions.isEmpty() && uncertainLevels.isEmpty(); }
};

// Builds the plan for the book's effective contents and its asset (the
// digest and page count bind it). Entries with a confirmed page (Resolved)
// become bookmarks; others are listed as omitted. An entry whose parent is
// omitted or removed goes to its nearest kept ancestor (or the top level),
// recorded as a promotion; an entry whose level is uncertain goes to the top
// level, recorded in uncertainLevels. Fails with InvalidArgument when no entry can be a
// bookmark, the page count is unknown, or a page lies outside the document.
Result<ExportPlan> buildExportPlan(const TocAnalysis& contents, const AssetRecord& asset);

// The application plan as stored with an export: nodes, omissions,
// promotions, uncertain levels and removals, which the SDK's plan JSON does
// not all carry. exportPlanFromJson fails with InvalidArgument on JSON that is
// not such a plan.
QString exportPlanToJson(const ExportPlan& plan);
Result<ExportPlan> exportPlanFromJson(const QString& json);

// What a finished export wrote, as the SDK reported it after verifying the
// copy. `committed` is exact: a cancel received after the commit does not
// change it.
struct ExportOutput {
    bool committed = false;
    QString outputSha256;
    int outlineItems = 0;
    int pageCount = 0;
    bool structureMatches = false;
    bool sourceUnchanged = false;
    QString sdkVersion;
    QString sdkPlanJson;  // The plan as the SDK serialized it, when it got that far.
};

// One request to write a bookmarked copy of a book, and what came of it. Its
// job (same ID) holds the state; the record holds what was asked and written.
struct ExportRecord {
    JobId job;
    BookId book;
    QString destination;             // Absolute path, as validated when requested.
    bool replaceExisting = false;
    ExportPlan plan;                 // Built from the effective contents when requested.
    std::optional<RunId> tocRun;     // The analysis the contents came from...
    std::optional<TocRevisionId> tocRevision;  // ...and the edited revision, if one was active.
    // nullopt: not known, because the export has not finished or the
    // application stopped while writing (the file may or may not exist).
    std::optional<bool> committed;
    ExportOutput output;             // When the export finished.
    QDateTime createdAt;
    QDateTime finishedAt;
};

} // namespace mbl::domain

Q_DECLARE_METATYPE(mbl::domain::ExportRecord)
