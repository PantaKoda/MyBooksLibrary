// A4: writing a bookmarked copy of a book, as seen by the coordinator. The
// production implementation calls the pdfbookmark SDK (sdk::SdkBookExporter);
// tests substitute fakes. The source is never modified: the copy is a new
// file, and the SDK refuses a source whose bytes differ from the plan's.
#pragma once

#include "domain/export.h"

#include <QString>
#include <QStringList>

#include <atomic>

namespace mbl::processing {

struct ExportResult {
    enum class Status { Committed, Cancelled, Failed };
    Status status = Status::Failed;
    // Whether the copy was written. True only for Committed: a cancel that
    // arrives after the write is not a cancelled export.
    bool committed = false;
    QString error;            // Failed: why, in plain language where known.
    QString errorCode;        // Failed: the SDK's code (e.g. "OutputExists"), for callers.
    QStringList planIssues;   // Failed validation: one line per issue.

    // Committed only:
    QString output;           // Absolute path written.
    QString outputSha256;
    int outlineItems = 0;     // Bookmarks in the reopened copy.
    int pageCount = 0;
    bool structureMatches = false;  // Reopened outline equals the plan.
    bool sourceUnchanged = false;   // Source digest rechecked before the commit.
    bool sourceHadBookmarks = false;  // Replaced in the copy, never merged.
    QStringList diagnostics;  // The SDK's notes on the write.
    QString planJson;         // The plan as written (plan_to_json), to keep with the export.
    QString sdkVersion;
};

class BookExporter {
public:
    virtual ~BookExporter() = default;

    // Writes `output` from `source` with the plan's bookmarks. `output` must
    // already be validated (storage::validateExportDestination); an existing
    // file is replaced only when `replaceExisting`. `cancel` must outlive the
    // call and is honoured only before the copy is committed.
    virtual ExportResult exportCopy(const QString& source, const QString& output, const domain::ExportPlan& plan,
                                    bool replaceExisting, const std::atomic_bool& cancel) = 0;
};

} // namespace mbl::processing
