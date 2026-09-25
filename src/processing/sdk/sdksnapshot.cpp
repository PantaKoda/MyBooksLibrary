#include "processing/sdk/sdksnapshot.h"

#include <QCryptographicHash>
#include <QStringList>

namespace mbl::sdk {

namespace {

QString text(const std::string& value)
{
    return QString::fromStdString(value);
}

template <typename T>
QString opt(const std::optional<T>& value)
{
    if (!value)
        return QStringLiteral("-");
    if constexpr (std::is_same_v<T, std::string>)
        return text(*value);
    else
        return QString::number(*value);
}

QString number(double value)
{
    return QString::number(value, 'g', 12);
}

QString pages(const std::vector<pdfbookmark::PageIndex>& indices)
{
    QStringList out;
    for (const auto index : indices)
        out << QString::number(index);
    return out.join(u',');
}

QString titleValue(const pdfbookmark::metadata::TitleValue& v)
{
    return text(v.title) + u'|' + opt(v.subtitle);
}

QString contributorsValue(const std::vector<pdfbookmark::metadata::Contributor>& v)
{
    QStringList out;
    for (const auto& c : v)
        out << text(c.name) + u'(' + QString::fromUtf8(pdfbookmark::metadata::role_name(c.role)) + u')';
    return out.join(u';');
}

QString editionValue(const pdfbookmark::metadata::EditionValue& v)
{
    return text(v.statement) + u'|' + opt(v.ordinal);
}

QString yearValue(const pdfbookmark::metadata::YearValue& v)
{
    return QString::number(v.year) + u'|' + QString::fromUtf8(pdfbookmark::metadata::kind_name(v.kind)) + u'|'
           + text(v.statement);
}

template <typename T, typename Format>
void field(QStringList& lines, const char* name, const pdfbookmark::metadata::Field<T>& f, Format format)
{
    lines << QStringLiteral("metadata.%1 status=%2 value=%3")
                 .arg(QLatin1StringView(name), QString::fromUtf8(pdfbookmark::metadata::status_name(f.status)),
                      f.value ? format(*f.value) : QStringLiteral("-"));
    for (const auto& alternative : f.alternatives) {
        QStringList evidencePages;
        for (const auto& e : alternative.evidence)
            evidencePages << QString::number(e.source.page_index);
        lines << QStringLiteral("metadata.%1 alternative=%2 score=%3 evidence_pages=%4")
                     .arg(QLatin1StringView(name), format(alternative.value), number(alternative.score),
                          evidencePages.join(u','));
    }
}

} // namespace

QString metadataSnapshot(const pdfbookmark::MetadataReport& report)
{
    const auto& r = report.result;
    QStringList lines;
    field(lines, "title", r.title, titleValue);
    field(lines, "contributors", r.contributors, contributorsValue);
    field(lines, "edition", r.edition, editionValue);
    field(lines, "publication_year", r.publication_year, yearValue);
    field(lines, "copyright_year", r.copyright_year, yearValue);
    lines << QStringLiteral("metadata.searched_pages=%1").arg(pages(report.searched_pages));
    lines << QStringLiteral("metadata.covered_document=%1").arg(report.search_covered_document);
    lines << QStringLiteral("metadata.cancelled=%1").arg(report.cancelled);
    return lines.join(u'\n');
}

QString analysisSnapshot(const pdfbookmark::AnalysisReport& report)
{
    QStringList lines;
    lines << QStringLiteral("analysis.outcome=%1").arg(QString::fromUtf8(pdfbookmark::outcome_name(report.outcome)));
    lines << QStringLiteral("analysis.search_pages=%1").arg(pages(report.search_pages));
    lines << QStringLiteral("analysis.evidence_pages=%1").arg(pages(report.evidence_pages));
    lines << QStringLiteral("analysis.candidate=%1 explicit=%2")
                 .arg(opt(report.candidate.chosen_id))
                 .arg(report.candidate.explicit_selection);
    if (report.parsed) {
        lines << QStringLiteral("parsed.candidate=%1 completeness=%2 unparsed=%3")
                     .arg(text(report.parsed->candidate_id))
                     .arg(int(report.parsed->completeness))
                     .arg(report.parsed->unparsed.size());
        for (const auto& e : report.parsed->entries) {
            const auto& ref = e.printed_reference;
            lines << QStringLiteral("entry id=%1 order=%2 title=%3 printed=%4 hierarchy=%5 parent=%6")
                         .arg(text(e.id))
                         .arg(e.order)
                         .arg(text(e.title), ref ? text(ref->literal) : QStringLiteral("-"))
                         .arg(int(e.hierarchy.kind))
                         .arg(opt(e.hierarchy.parent_id));
        }
    } else {
        lines << QStringLiteral("parsed=none");
    }
    if (report.mapping) {
        for (const auto& m : report.mapping->entries) {
            QStringList alternatives;
            for (const auto& a : m.alternatives)
                alternatives << QString::number(a.pdf_page_index);
            lines << QStringLiteral("map entry=%1 status=%2 page=%3 method=%4 alternatives=%5")
                         .arg(text(m.entry_id))
                         .arg(int(m.status))
                         .arg(opt(m.pdf_page_index))
                         .arg(m.method ? QString::number(int(*m.method)) : QStringLiteral("-"))
                         .arg(alternatives.join(u','));
        }
    } else {
        lines << QStringLiteral("mapping=none");
    }
    lines << QStringLiteral("plan.ready=%1").arg(report.plan.ready);
    for (const auto& blocker : report.plan.blockers)
        lines << QStringLiteral("plan.blocker=%1").arg(text(blocker));
    if (report.plan.plan) {
        for (const auto& node : report.plan.plan->nodes) {
            lines << QStringLiteral("node id=%1 parent=%2 title=%3 page=%4")
                         .arg(text(node.id), opt(node.parent_id), text(node.title))
                         .arg(node.destination.pdf_page_index);
        }
        for (const auto& omitted : report.plan.plan->omitted_entries)
            lines << QStringLiteral("omitted entry=%1").arg(text(omitted.entry_id));
        for (const auto& promotion : report.plan.plan->promotions) {
            lines << QStringLiteral("promoted node=%1 from=%2 to=%3")
                         .arg(text(promotion.node_id), opt(promotion.original_parent_id), opt(promotion.new_parent_id));
        }
    }
    return lines.join(u'\n');
}

QString snapshotDigest(const QString& snapshot)
{
    return QString::fromLatin1(QCryptographicHash::hash(snapshot.toUtf8(), QCryptographicHash::Sha256).toHex());
}

} // namespace mbl::sdk
