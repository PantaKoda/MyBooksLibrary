// SDK boundary (A4): the production MetadataExtractor. Runs
// pdfbookmark::extract_metadata with the located OCR models and converts the
// report into application contracts. SDK types stay in the .cpp.
#pragma once

#include "processing/metadataextractor.h"

namespace mbl::sdk {

struct SdkMetadataOptions {
    bool useModels = true;  // false: OCR unavailable (scanned pages are reported unreadable).
    int ocrThreads = 0;     // 0 = automatic.
};

class SdkMetadataExtractor : public processing::MetadataExtractor {
public:
    explicit SdkMetadataExtractor(SdkMetadataOptions options = {}) : m_options(options) {}
    processing::MetadataExtraction extract(const QString& pdfPath, const std::atomic_bool& cancel) override;

private:
    SdkMetadataOptions m_options;
};

} // namespace mbl::sdk
