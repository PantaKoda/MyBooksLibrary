#include "processing/sdk/sdkinfo.h"

#include <pdfbookmark/pdfbookmark.hpp>

#include <QByteArray>

#include <filesystem>

namespace mbl::sdk {

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

QString fromSdkPath(const std::filesystem::path& path)
{
#ifdef Q_OS_WIN
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.u8string());
#endif
}

} // namespace

SdkIdentity querySdkIdentity()
{
    SdkIdentity identity;
    identity.headerVersion = QStringLiteral(PDFBOOKMARK_VERSION_STRING);
    identity.loadedVersion = QString::fromUtf8(pdfbookmark::version());
    if (const auto models = pdfbookmark::find_models()) {
        identity.modelsFound = true;
        identity.detectorModel = fromSdkPath(models->detector);
    }
    return identity;
}

PdfProbe probePdf(const QString& localPath, const std::atomic_bool* cancel)
{
    PdfProbe probe;
    const std::filesystem::path input = toSdkPath(localPath);

    const auto identity = pdfbookmark::read_pdf_identity(input);
    if (!identity) {
        probe.error = QString::fromStdString(identity.error().message);
        return probe;
    }
    const auto& sha = identity.value().sha256;
    probe.sha256 = QString::fromLatin1(
        QByteArray(reinterpret_cast<const char*>(sha.data()), qsizetype(sha.size())).toHex());
    probe.pageCount = identity.value().page_count;

    pdfbookmark::MetadataRunOptions options;
    options.models = pdfbookmark::find_models();
    const auto report = pdfbookmark::extract_metadata(input, options,
                                                      pdfbookmark::RunControl{cancel});
    if (!report) {
        probe.error = QString::fromStdString(report.error().message);
        return probe;
    }
    const auto& title = report.value().result.title;
    probe.titleStatus = QString::fromUtf8(pdfbookmark::metadata::status_name(title.status));
    if (title.value)
        probe.title = QString::fromStdString(title.value->title);
    probe.pagesSearched = int(report.value().searched_pages.size());
    probe.metadataCancelled = report.value().cancelled;
    probe.metadataJsonBytes =
        qsizetype(pdfbookmark::metadata_report_json(report.value()).size());
    probe.ok = true;
    return probe;
}

AnalysisProbe probeAnalysis(const QString& localPath, const std::atomic_bool* cancel)
{
    AnalysisProbe probe;
    pdfbookmark::AnalysisOptions options;
    options.models = pdfbookmark::find_models();
    options.plan.allow_partial = false;
    options.plan.flat_outline_for_unknown_hierarchy = false;
    options.plan.title_style = pdfbookmark::PlanPolicy::TitleStyle::AsPrinted;

    int callbacks = 0;
    const auto report = pdfbookmark::analyze(toSdkPath(localPath), options, pdfbookmark::RunControl{cancel},
                                             [&callbacks](const pdfbookmark::AnalysisProgress&) { ++callbacks; });
    probe.progressCallbacks = callbacks;
    if (!report) {
        probe.error = QString::fromStdString(report.error().message);
        probe.cancelledError = report.error().code == pdfbookmark::ErrorCode::Cancelled;
        return probe;
    }
    const auto& r = report.value();
    probe.outcome = QString::fromUtf8(pdfbookmark::outcome_name(r.outcome));
    if (r.parsed)
        probe.parsedEntries = int(r.parsed->entries.size());
    if (r.mapping) {
        for (const auto& entry : r.mapping->entries) {
            if (entry.status == pdfbookmark::mapping::MappingStatus::Resolved)
                ++probe.resolvedEntries;
        }
    }
    probe.planReady = r.plan.ready;
    probe.pagesAcquired = int(r.pages.size());
    probe.ok = true;
    return probe;
}

} // namespace mbl::sdk
