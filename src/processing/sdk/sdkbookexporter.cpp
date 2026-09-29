#include "processing/sdk/sdkbookexporter.h"

#include "processing/sdk/sdkcommon.h"

#include <pdfbookmark/pdfbookmark.hpp>

#include <QByteArray>

namespace mbl::sdk {

namespace pb = pdfbookmark;
using processing::ExportResult;

namespace {

std::string utf8(const QString& text)
{
    return text.toUtf8().toStdString();
}

QString hex(const std::array<std::uint8_t, 32>& bytes)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size())).toHex());
}

const char* codeName(pb::ErrorCode code)
{
    switch (code) {
    case pb::ErrorCode::InvalidArgument: return "InvalidArgument";
    case pb::ErrorCode::InputOpen: return "InputOpen";
    case pb::ErrorCode::InputChanged: return "InputChanged";
    case pb::ErrorCode::PdfBackend: return "PdfBackend";
    case pb::ErrorCode::OcrConfiguration: return "OcrConfiguration";
    case pb::ErrorCode::ResourceLimit: return "ResourceLimit";
    case pb::ErrorCode::Unsupported: return "Unsupported";
    case pb::ErrorCode::OutputExists: return "OutputExists";
    case pb::ErrorCode::OutputWrite: return "OutputWrite";
    case pb::ErrorCode::Cancelled: return "Cancelled";
    }
    return "Unknown";
}

// The application's words for the refusals a user can meet.
QString plainError(const pb::Error& error)
{
    switch (error.code) {
    case pb::ErrorCode::OutputExists:
        return QStringLiteral("A file with that name already exists.");
    case pb::ErrorCode::InputChanged:
        return QStringLiteral("The library's copy of the book no longer matches its record, so no copy was written.");
    case pb::ErrorCode::Unsupported:
        return QStringLiteral("This PDF is encrypted or signed, so bookmarks cannot be added to a copy.");
    case pb::ErrorCode::OutputWrite:
        return QStringLiteral("The copy could not be written or verified: %1").arg(QString::fromStdString(error.message));
    default:
        return QString::fromStdString(error.message);
    }
}

pb::BookmarkPlan sdkPlan(const domain::ExportPlan& plan)
{
    pb::BookmarkPlan out;
    const QByteArray digest = QByteArray::fromHex(plan.sourceSha256.toLatin1());
    for (qsizetype i = 0; i < digest.size() && i < qsizetype(out.input.sha256.size()); ++i)
        out.input.sha256[size_t(i)] = std::uint8_t(digest.at(i));
    out.input.page_count = pb::PageCount(plan.pageCount);
    for (const domain::ExportNode& n : plan.nodes) {
        pb::BookmarkNode node;
        node.id = utf8(n.id);
        if (n.parentId)
            node.parent_id = utf8(*n.parentId);
        node.title = utf8(n.title);
        node.destination.pdf_page_index = pb::PageIndex(n.page);
        out.nodes.push_back(std::move(node));
    }
    for (const domain::ExportOmission& o : plan.omitted)
        out.omitted_entries.push_back({utf8(o.entryId), utf8(o.reason)});
    for (const domain::ExportPromotion& p : plan.promotions) {
        auto& promotion = out.promotions.emplace_back();
        promotion.node_id = utf8(p.nodeId);
        if (p.originalParentId)
            promotion.original_parent_id = utf8(*p.originalParentId);
        if (p.newParentId)
            promotion.new_parent_id = utf8(*p.newParentId);
        promotion.reason = utf8(p.reason);
    }
    return out;
}

} // namespace

ExportResult SdkBookExporter::exportCopy(const QString& source, const QString& output, const domain::ExportPlan& plan,
                                         bool replaceExisting, const std::atomic_bool& cancel)
{
    ExportResult result;
    result.sdkVersion = QString::fromUtf8(pb::version());
    const pb::BookmarkPlan sdk = sdkPlan(plan);
    const pb::PlanValidation validation = pb::validate_plan(sdk);
    if (!validation.valid) {
        result.error = QStringLiteral("The bookmarks could not be prepared.");
        result.errorCode = QStringLiteral("InvalidPlan");
        for (const auto& issue : validation.issues) {
            result.planIssues << QStringLiteral("%1 %2: %3")
                                     .arg(QString::fromStdString(issue.node_id), QString::fromStdString(issue.field),
                                          QString::fromStdString(issue.message))
                                     .trimmed();
        }
        return result;
    }
    result.planJson = QString::fromStdString(pb::plan_to_json(sdk));

    pb::ApplyOptions options;
    options.replace_existing_output = replaceExisting;
    auto written = pb::apply(toSdkPath(source), toSdkPath(output), sdk, options, pb::RunControl{&cancel});
    if (!written) {
        const pb::Error& error = written.error();
        if (error.code == pb::ErrorCode::Cancelled) {
            result.status = ExportResult::Status::Cancelled;  // Only before the commit: nothing written.
            return result;
        }
        result.error = plainError(error);
        result.errorCode = QString::fromLatin1(codeName(error.code));
        return result;
    }
    const pb::WriteResult& w = written.value();
    if (!w.committed) {
        result.error = QStringLiteral("The copy was not committed.");
        result.errorCode = QStringLiteral("NotCommitted");
        return result;
    }
    // Committed: whatever the cancel flag says now, the file exists.
    result.status = ExportResult::Status::Committed;
    result.committed = true;
    result.output = output;
    result.outputSha256 = hex(w.output_sha256);
    result.outlineItems = int(w.verification.outline_items);
    result.pageCount = int(w.verification.page_count);
    result.structureMatches = w.verification.structure_matches;
    result.sourceUnchanged = w.verification.input_unchanged;
    result.sourceHadBookmarks = w.input_had_outline;
    for (const std::string& d : w.diagnostics)
        result.diagnostics << QString::fromStdString(d);
    return result;
}

} // namespace mbl::sdk
