// SDK boundary (A4): identity of the pdfbookmark SDK the app was built with and
// actually loaded, plus a small blocking probe used by the M00 baseline check.
// pdfbookmark headers stay in the .cpp; callers see only Qt value types.
#pragma once

#include <QString>

#include <atomic>

namespace mbl::sdk {

struct SdkIdentity {
    QString headerVersion;    // PDFBOOKMARK_VERSION_STRING the app was compiled against.
    QString loadedVersion;    // pdfbookmark::version() of the loaded library.
    bool modelsFound = false; // find_models() located the OCR models.
    QString detectorModel;    // Detector model path when found (diagnostics only).
};

SdkIdentity querySdkIdentity();

struct PdfProbe {
    bool ok = false;
    QString error;             // SDK error message when !ok.
    QString sha256;            // Lower-case hex digest from read_pdf_identity().
    int pageCount = 0;
    QString titleStatus;       // Resolved, Ambiguous or NotFoundInSearch.
    QString title;             // Only when the title is Resolved.
    int pagesSearched = 0;
    bool metadataCancelled = false;
    qsizetype metadataJsonBytes = 0;  // Size of metadata_report_json(), proves the serializer.
};

// Blocking: reads the PDF identity, then runs extract_metadata() with the
// located models. Never call on the GUI thread. `cancel` must outlive the call.
PdfProbe probePdf(const QString& localPath, const std::atomic_bool* cancel);

} // namespace mbl::sdk
