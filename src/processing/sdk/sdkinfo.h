// SDK boundary (A4): identity of the pdfbookmark SDK the app was built with and
// actually loaded, plus blocking probes used by the M00/M01 checks.
// pdfbookmark headers stay in the .cpp files; callers see only Qt value types.
#pragma once

#include <QString>

#include <atomic>
#include <functional>

namespace mbl::sdk {

struct SdkIdentity {
    QString headerVersion;    // PDFBOOKMARK_VERSION_STRING the app was compiled against.
    QString loadedVersion;    // pdfbookmark::version() of the loaded library.
    bool modelsFound = false; // find_models() located the OCR models.
    QString detectorModel;    // Detector model path when found (diagnostics only).
    int ocrThreadsAuto = 0;   // OCR threads the SDK uses for ocr_threads = 0 on this machine.
};

SdkIdentity querySdkIdentity();

struct ProbeOptions {
    bool useModels = true;             // false: models = nullopt, so OCR is unavailable.
    int ocrThreads = 0;                // OCR CPU threads (SDK 0.2); 0 = automatic.
    std::function<void()> onProgress;  // Called on the worker thread for each analysis progress callback.
};

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
    QString modelIdentity;     // Empty when no models were used.
    int ocrAttemptsUsed = 0;
    QString semanticSnapshot;  // Canonical text of the bibliographic results (see sdksnapshot.h).
    QString semanticDigest;    // SHA-256 of semanticSnapshot.
};

// Blocking: reads the PDF identity, then runs extract_metadata(). Never call
// on the GUI thread. `cancel` must outlive the call.
PdfProbe probePdf(const QString& localPath, const std::atomic_bool* cancel, const ProbeOptions& options = {});

struct AnalysisProbe {
    bool ok = false;
    QString error;               // SDK error message when !ok (includes cancellation errors).
    bool cancelledError = false; // The SDK returned ErrorCode::Cancelled.
    QString outcome;             // outcome_name(), e.g. "plan_ready".
    int parsedEntries = 0;       // All parsed TOC entries (0 when none were parsed).
    int resolvedEntries = 0;     // Mapping entries with status Resolved.
    bool planReady = false;
    int pagesAcquired = 0;
    int progressCallbacks = 0;   // Progress callbacks received on the worker thread.
    QString modelIdentity;       // Empty when no models were used.
    int ocrAttemptsUsed = 0;     // Run-wide OCR attempts counted by the SDK.
    int ocrPagesCompleted = 0;   // Pages with a Completed OCR attempt.
    int ocrPagesFailed = 0;      // Pages with a Failed OCR attempt.
    int ocrPagesSkipped = 0;     // Pages whose OCR attempt was Skipped (no models, budget, ...).
    QString semanticSnapshot;    // Canonical text of outcome, entries, mappings and plan.
    QString semanticDigest;      // SHA-256 of semanticSnapshot.
};

// Blocking: runs analyze() with automatic acquisition, AsPrinted titles,
// allow_partial = false and no flattening. Never call on the GUI thread.
// `cancel` must outlive the call.
AnalysisProbe probeAnalysis(const QString& localPath, const std::atomic_bool* cancel,
                            const ProbeOptions& options = {});

} // namespace mbl::sdk
