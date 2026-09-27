// A4: the metadata extraction boundary as seen by the coordinator. The
// production implementation (processing/sdk/sdkmetadataextractor.h) wraps the
// pdfbookmark SDK; tests substitute fakes. Only Qt and domain types cross it.
#pragma once

#include "domain/metadata.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <atomic>
#include <optional>

namespace mbl::processing {

struct MetadataExtraction {
    enum class Status { Completed, Cancelled, Failed };
    Status status = Status::Failed;
    QString error;                     // For Failed.

    QString sourceSha256;              // Digest of the bytes actually analysed.
    std::optional<int> pageCount;
    domain::ExtractedMetadata metadata;
    QList<domain::MetadataFieldDetail> details;

    QByteArray reportJson;             // The SDK's own report, kept as evidence.
    QString sdkVersion;
    QString modelIdentity;             // Empty when OCR models were not available.
    QString optionsJson;               // The options the run used.
    QString outcome;                   // Stable summary code, e.g. "completed".
};

class MetadataExtractor {
public:
    virtual ~MetadataExtractor() = default;
    // Blocking; called on the coordinator's worker thread, never the GUI
    // thread. Must observe `cancel` (which outlives the call) cooperatively.
    virtual MetadataExtraction extract(const QString& pdfPath, const std::atomic_bool& cancel) = 0;
};

} // namespace mbl::processing
