#include "processing/sdk/sdkcontentsanalyzer.h"

#include "processing/sdk/contentsnormalize.h"
#include "processing/sdk/sdkcommon.h"

#include <pdfbookmark/pdfbookmark.hpp>

#include <QJsonDocument>
#include <QJsonObject>

namespace mbl::sdk {

namespace pb = pdfbookmark;
using processing::ContentsAnalysis;
using processing::MetadataExtraction;

namespace {

pb::AnalysisOptions analysisOptions(const SdkContentsOptions& o)
{
    pb::AnalysisOptions options;  // Finite default limits: 40 pages, then 20 more, up to 200; 64 OCR attempts.
    options.models = o.useModels ? pb::find_models() : std::nullopt;
    options.ocr_threads = o.ocrThreads;
    options.plan.allow_partial = false;
    options.plan.flat_outline_for_unknown_hierarchy = false;
    options.plan.title_style = pb::PlanPolicy::TitleStyle::AsPrinted;
    return options;
}

QString analysisOptionsJson(const pb::AnalysisOptions& o, bool withMetadata)
{
    QJsonObject json{{QStringLiteral("mode"), QStringLiteral("auto")},
                     {QStringLiteral("models"), o.models.has_value()},
                     {QStringLiteral("ocr_threads"), o.ocr_threads},
                     {QStringLiteral("initial_pages"), qint64(o.limits.initial_pages)},
                     {QStringLiteral("batch_pages"), qint64(o.limits.batch_pages)},
                     {QStringLiteral("max_search_pages"), qint64(o.limits.max_search_pages)},
                     {QStringLiteral("max_evidence_pages"), qint64(o.limits.max_evidence_pages)},
                     {QStringLiteral("ocr_budget"), qint64(o.limits.ocr_budget)},
                     {QStringLiteral("allow_partial"), o.plan.allow_partial},
                     {QStringLiteral("flat_outline_for_unknown_hierarchy"), o.plan.flat_outline_for_unknown_hierarchy},
                     {QStringLiteral("title_style"), QStringLiteral("as_printed")}};
    if (withMetadata)
        json.insert(QStringLiteral("run"), QStringLiteral("analyze_book"));
    return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
}

QString metadataOptionsJson(const pb::AnalysisOptions& session, const pb::MetadataRunOptions& m)
{
    // In analyze_book the session settings (models, threads) come from the
    // analysis options; the metadata budget is part of the run-wide one.
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{QStringLiteral("mode"), QStringLiteral("auto")},
                                  {QStringLiteral("models"), session.models.has_value()},
                                  {QStringLiteral("ocr_threads"), session.ocr_threads},
                                  {QStringLiteral("initial_pages"), qint64(m.initial_pages)},
                                  {QStringLiteral("batch_pages"), qint64(m.batch_pages)},
                                  {QStringLiteral("max_pages"), qint64(m.max_pages)},
                                  {QStringLiteral("ocr_budget"), qint64(m.ocr_budget)},
                                  {QStringLiteral("run"), QStringLiteral("analyze_book")}})
            .toJson(QJsonDocument::Compact));
}

pb::AnalysisProgressCallback progressCallback(const processing::ContentsAnalyzer::Progress& progress)
{
    if (!progress)
        return {};
    return [progress](const pb::AnalysisProgress& p) {
        progress(QString::fromStdString(p.stage), int(p.pages_acquired));
    };
}

ContentsAnalysis failure(const pb::Error& error, const QString& optionsJson)
{
    ContentsAnalysis out;
    out.sdkVersion = QString::fromUtf8(pb::version());
    out.optionsJson = optionsJson;
    out.status = error.code == pb::ErrorCode::Cancelled ? ContentsAnalysis::Status::Cancelled
                                                        : ContentsAnalysis::Status::Failed;
    out.error = QString::fromStdString(error.message);
    return out;
}

ContentsAnalysis fromReport(const pb::AnalysisReport& report, const pb::AnalysisOptions& options,
                            const QString& optionsJson)
{
    ContentsAnalysis out;
    out.sdkVersion = QString::fromUtf8(pb::version());
    out.optionsJson = optionsJson;
    if (report.outcome == pb::AnalysisOutcome::Cancelled) {
        out.status = ContentsAnalysis::Status::Cancelled;
        return out;
    }
    out.sourceSha256 = sha256Hex(report.input);
    out.pageCount = int(report.input.page_count);
    out.toc = normalizeContents(report);
    out.outcome = out.toc.outcome;
    out.reportJson = QByteArray::fromStdString(pb::analysis_report_json(report, options));
    out.modelIdentity = QString::fromStdString(report.model_identity);
    out.status = ContentsAnalysis::Status::Completed;
    return out;
}

MetadataExtraction metadataFailure(const pb::Error& error, const QString& optionsJson)
{
    MetadataExtraction out;
    out.sdkVersion = QString::fromUtf8(pb::version());
    out.optionsJson = optionsJson;
    out.status = error.code == pb::ErrorCode::Cancelled ? MetadataExtraction::Status::Cancelled
                                                        : MetadataExtraction::Status::Failed;
    out.error = QString::fromStdString(error.message);
    return out;
}

} // namespace

ContentsAnalysis SdkContentsAnalyzer::analyze(const QString& pdfPath, const std::atomic_bool& cancel,
                                              const Progress& progress)
{
    const pb::AnalysisOptions options = analysisOptions(m_options);
    const QString optionsJson = analysisOptionsJson(options, false);
    const auto report = pb::analyze(toSdkPath(pdfPath), options, pb::RunControl{&cancel}, progressCallback(progress));
    if (!report)
        return failure(report.error(), optionsJson);
    return fromReport(report.value(), options, optionsJson);
}

ContentsAnalysis SdkContentsAnalyzer::analyzeBook(const QString& pdfPath, const std::atomic_bool& cancel,
                                                  const Progress& progress, const MetadataReady& onMetadata)
{
    const pb::AnalysisOptions options = analysisOptions(m_options);
    const pb::MetadataRunOptions metadata;  // Defaults; models and threads come from `options`.
    const QString optionsJson = analysisOptionsJson(options, true);
    const QString metadataJson = metadataOptionsJson(options, metadata);

    bool delivered = false;
    const auto deliver = [&](const MetadataExtraction& extraction) {
        if (delivered)
            return;
        delivered = true;
        if (onMetadata)
            onMetadata(extraction);
    };
    const auto book = pb::analyze_book(toSdkPath(pdfPath), options, metadata, pb::RunControl{&cancel},
                                       progressCallback(progress),
                                       [&](const pb::MetadataReport& report) {
                                           deliver(metadataExtraction(report, metadataJson));
                                       });
    if (!book) {
        // Opening the input or the metadata stage failed: both results fail.
        deliver(metadataFailure(book.error(), metadataJson));
        return failure(book.error(), optionsJson);
    }
    const pb::BookReport& report = book.value();
    deliver(metadataExtraction(report.metadata, metadataJson));  // Only if the callback did not run.
    if (!report.analysis) {
        if (report.analysis_error)
            return failure(*report.analysis_error, optionsJson);
        ContentsAnalysis out;  // Not expected: no analysis and no reason.
        out.sdkVersion = QString::fromUtf8(pb::version());
        out.optionsJson = optionsJson;
        out.error = QStringLiteral("The contents analysis did not run.");
        return out;
    }
    return fromReport(*report.analysis, options, optionsJson);
}

} // namespace mbl::sdk
