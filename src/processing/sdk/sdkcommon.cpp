#include "processing/sdk/sdkcommon.h"

#include "processing/sdk/metadatanormalize.h"

#include <QByteArray>

namespace mbl::sdk {

using processing::MetadataExtraction;

std::filesystem::path toSdkPath(const QString& localPath)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(localPath.toStdWString());
#else
    const QByteArray utf8 = localPath.toUtf8();
    return std::filesystem::u8path(utf8.constData(), utf8.constData() + utf8.size());
#endif
}

QString sha256Hex(const pdfbookmark::InputIdentity& input)
{
    const auto& sha = input.sha256;
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(sha.data()), qsizetype(sha.size())).toHex());
}

MetadataExtraction metadataExtraction(const pdfbookmark::MetadataReport& report, const QString& optionsJson)
{
    MetadataExtraction out;
    out.sdkVersion = QString::fromUtf8(pdfbookmark::version());
    out.optionsJson = optionsJson;
    if (report.cancelled) {
        out.status = MetadataExtraction::Status::Cancelled;
        return out;
    }
    out.sourceSha256 = sha256Hex(report.input);
    out.pageCount = int(report.input.page_count);
    out.metadata = normalizeMetadata(report.result, &out.details);
    out.reportJson = QByteArray::fromStdString(pdfbookmark::metadata_report_json(report));
    out.modelIdentity = QString::fromStdString(report.model_identity);
    out.outcome = report.search_covered_document ? QStringLiteral("completed_whole_document")
                                                 : QStringLiteral("completed");
    out.status = MetadataExtraction::Status::Completed;
    return out;
}

} // namespace mbl::sdk
