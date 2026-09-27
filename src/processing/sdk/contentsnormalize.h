// SDK boundary (A4), internal: converts the SDK's analysis report into the
// application's contents contract.
//
// - Every parsed entry is kept, whether or not it was mapped or put in the plan.
// - Parsed entries and mapping entries are joined by entry ID, never by position.
// - Hierarchy (Root / KnownParent / Unknown) and destination (Resolved /
//   Ambiguous / Unresolved) states are kept; a page is given only when Resolved.
// - Printed labels stay labels; destinations are zero-based physical pages.
// - "In the export plan" means the plan (draft or ready) has a node for the
//   entry; the plan's reason is kept for omitted entries.
#pragma once

#include "domain/toc.h"

#include <pdfbookmark/pdfbookmark.hpp>

namespace mbl::sdk {

domain::TocAnalysis normalizeContents(const pdfbookmark::AnalysisReport& report);

} // namespace mbl::sdk
