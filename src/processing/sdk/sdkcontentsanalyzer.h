// SDK boundary (A4): the production ContentsAnalyzer. Runs pdfbookmark::analyze
// or, for metadata and contents together, pdfbookmark::analyze_book (one
// session, pages OCR'd once), with the located OCR models and the automatic
// settings of AGENTS.md section 4: titles as printed, no partial plan, no
// flattening of unknown hierarchy. Those settings only affect the plan; every
// parsed entry is still returned. SDK types stay in the .cpp files.
#pragma once

#include "processing/contentsanalyzer.h"

namespace mbl::sdk {

struct SdkContentsOptions {
    bool useModels = true;  // false: OCR unavailable (scanned pages are reported unreadable).
    int ocrThreads = 0;     // 0 = automatic.
};

class SdkContentsAnalyzer : public processing::ContentsAnalyzer {
public:
    explicit SdkContentsAnalyzer(SdkContentsOptions options = {}) : m_options(options) {}
    processing::ContentsAnalysis analyze(const QString& pdfPath, const std::atomic_bool& cancel,
                                         const Progress& progress) override;
    processing::ContentsAnalysis analyzeBook(const QString& pdfPath, const std::atomic_bool& cancel,
                                             const Progress& progress, const MetadataReady& onMetadata) override;

private:
    SdkContentsOptions m_options;
};

} // namespace mbl::sdk
