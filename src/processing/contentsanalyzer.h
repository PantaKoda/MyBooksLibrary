// A4: the contents (TOC) analysis step of processing, as seen by the
// coordinator. The production implementation calls the pdfbookmark SDK
// (sdk::SdkContentsAnalyzer); tests substitute fakes.
#pragma once

#include "domain/toc.h"
#include "processing/metadataextractor.h"

#include <QByteArray>
#include <QString>

#include <atomic>
#include <functional>
#include <optional>

namespace mbl::processing {

struct ContentsAnalysis {
    enum class Status { Completed, Cancelled, Failed };
    Status status = Status::Failed;
    QString error;              // Failed: why (plain text from the SDK).

    // Completed only:
    QString sourceSha256;       // Of the bytes analyzed.
    std::optional<int> pageCount;
    domain::TocAnalysis toc;    // Every parsed entry, joined to its mapping by entry ID.
    QByteArray reportJson;      // analysis_report_json, stored as the run's report.
    QString sdkVersion;
    QString modelIdentity;      // Empty when OCR was not used.
    QString optionsJson;
    QString outcome;            // SDK outcome name; equals toc.outcome.
};

class ContentsAnalyzer {
public:
    // Called on the worker thread with the SDK's stage and the pages acquired
    // in that stage so far (no total exists).
    using Progress = std::function<void(const QString& stage, int pagesAcquired)>;
    using MetadataReady = std::function<void(const MetadataExtraction&)>;

    virtual ~ContentsAnalyzer() = default;

    // Contents analysis alone. `cancel` must outlive the call.
    virtual ContentsAnalysis analyze(const QString& pdfPath, const std::atomic_bool& cancel,
                                     const Progress& progress) = 0;

    // Metadata, then contents, in one run: pages are read and OCR'd once.
    // `onMetadata` is called exactly once, on the calling thread, before this
    // returns: as soon as the metadata stage is done (before the contents
    // analysis starts), or with a Failed/Cancelled extraction if that stage
    // did not complete. The contents result is Failed or Cancelled if the
    // analysis did not complete; the metadata result stands on its own.
    virtual ContentsAnalysis analyzeBook(const QString& pdfPath, const std::atomic_bool& cancel,
                                         const Progress& progress, const MetadataReady& onMetadata) = 0;
};

} // namespace mbl::processing
