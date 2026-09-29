// SDK boundary (A4): the production BookExporter. Turns the application's
// export plan into a pdfbookmark::BookmarkPlan bound to the source's digest
// and page count, validates it (validate_plan), and writes the copy with
// pdfbookmark::apply, which verifies the reopened copy and refuses a changed
// source or an existing output (unless replacing). The committed flag is
// reported exactly as the SDK returns it. SDK types stay in the .cpp file.
#pragma once

#include "processing/bookexporter.h"

namespace mbl::sdk {

class SdkBookExporter : public processing::BookExporter {
public:
    processing::ExportResult exportCopy(const QString& source, const QString& output, const domain::ExportPlan& plan,
                                        bool replaceExisting, const std::atomic_bool& cancel) override;
};

} // namespace mbl::sdk
