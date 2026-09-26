#include "processing/sdk/sdkmetadataextractor.h"

#include "processing/sdk/metadatanormalize.h"

#include <pdfbookmark/pdfbookmark.hpp>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <filesystem>

namespace mbl::sdk {

using processing::MetadataExtraction;

namespace {

std::filesystem::path toSdkPath(const QString& localPath)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(localPath.toStdWString());
#else
    const QByteArray utf8 = localPath.toUtf8();
    return std::filesystem::u8path(utf8.constData(), utf8.constData() + utf8.size());
#endif
}

} // namespace

MetadataExtraction SdkMetadataExtractor::extract(const QString& pdfPath, const std::atomic_bool& cancel)
{
    MetadataExtraction out;
    out.sdkVersion = QString::fromUtf8(pdfbookmark::version());

    pdfbookmark::MetadataRunOptions options;  // Finite defaults: 10 pages, then up to 30; 16 OCR attempts.
    options.models = m_options.useModels ? pdfbookmark::find_models() : std::nullopt;
    options.ocr_threads = m_options.ocrThreads;
    out.optionsJson = QString::fromUtf8(
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
        out.status = report.error().code == pdfbookmark::ErrorCode::Cancelled ? MetadataExtraction::Status::Cancelled
                                                                               : MetadataExtraction::Status::Failed;
        out.error = QString::fromStdString(report.error().message);
        return out;
    }
    const pdfbookmark::MetadataReport& r = report.value();
    if (r.cancelled) {
        out.status = MetadataExtraction::Status::Cancelled;
        return out;
    }
    const auto& sha = r.input.sha256;
    out.sourceSha256 = QString::fromLatin1(
        QByteArray(reinterpret_cast<const char*>(sha.data()), qsizetype(sha.size())).toHex());
    out.pageCount = int(r.input.page_count);
    out.metadata = normalizeMetadata(r.result, &out.details);
    out.reportJson = QByteArray::fromStdString(pdfbookmark::metadata_report_json(r));
    out.modelIdentity = QString::fromStdString(r.model_identity);
    out.outcome = r.search_covered_document ? QStringLiteral("completed_whole_document")
                                            : QStringLiteral("completed");
    out.status = MetadataExtraction::Status::Completed;
    return out;
}

} // namespace mbl::sdk
