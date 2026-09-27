#include "processing/sdk/sdkmetadataextractor.h"

#include "processing/sdk/sdkcommon.h"

#include <pdfbookmark/pdfbookmark.hpp>

#include <QJsonDocument>
#include <QJsonObject>

namespace mbl::sdk {

using processing::MetadataExtraction;

MetadataExtraction SdkMetadataExtractor::extract(const QString& pdfPath, const std::atomic_bool& cancel)
{
    pdfbookmark::MetadataRunOptions options;  // Finite defaults: 10 pages, then up to 30; 16 OCR attempts.
    options.models = m_options.useModels ? pdfbookmark::find_models() : std::nullopt;
    options.ocr_threads = m_options.ocrThreads;
    const QString optionsJson = QString::fromUtf8(
        QJsonDocument(QJsonObject{{QStringLiteral("mode"), QStringLiteral("auto")},
                                  {QStringLiteral("models"), options.models.has_value()},
                                  {QStringLiteral("ocr_threads"), options.ocr_threads},
                                  {QStringLiteral("initial_pages"), qint64(options.initial_pages)},
                                  {QStringLiteral("batch_pages"), qint64(options.batch_pages)},
                                  {QStringLiteral("max_pages"), qint64(options.max_pages)},
                                  {QStringLiteral("ocr_budget"), qint64(options.ocr_budget)}})
            .toJson(QJsonDocument::Compact));

    const auto report = pdfbookmark::extract_metadata(toSdkPath(pdfPath), options, pdfbookmark::RunControl{&cancel});
    if (!report) {
        MetadataExtraction out;
        out.sdkVersion = QString::fromUtf8(pdfbookmark::version());
        out.optionsJson = optionsJson;
        out.status = report.error().code == pdfbookmark::ErrorCode::Cancelled ? MetadataExtraction::Status::Cancelled
                                                                               : MetadataExtraction::Status::Failed;
        out.error = QString::fromStdString(report.error().message);
        return out;
    }
    return metadataExtraction(report.value(), optionsJson);
}

} // namespace mbl::sdk
