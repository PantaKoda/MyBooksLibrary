#include "processing/sdk/sdksnapshot.h"

#include <QCryptographicHash>
#include <QStringList>

namespace mbl::sdk {

namespace {

// Encoding rules, so that distinct results never share a snapshot:
// - every string is a JSON-style quoted string: '"' and '\' are escaped and
//   control characters (including line breaks) become \uXXXX, so a value can
//   neither contain an unescaped separator nor start a new snapshot line;
// - an absent optional is the bare word null, which no quoted string equals;
// - composite values are bracketed: {...} for records, [...] for lists.

QString str(const std::string& value)
{
    const QString s = QString::fromStdString(value);
    QString out;
    out.reserve(s.size() + 2);
    out += u'"';
    for (const QChar c : s) {  // UTF-16 code units; surrogates pass through unchanged.
        if (c == u'"' || c == u'\\') {
            out += u'\\';
            out += c;
        } else if (c.unicode() < 0x20 || c.unicode() == 0x7f || c == QChar::LineSeparator
                   || c == QChar::ParagraphSeparator) {
            out += QStringLiteral("\\u%1").arg(uint(c.unicode()), 4, 16, QLatin1Char('0'));
        } else {
            out += c;
        }
    }
    out += u'"';
    return out;
}

QString optStr(const std::optional<std::string>& value)
{
    return value ? str(*value) : QStringLiteral("null");
}

template <typename T>
QString optNum(const std::optional<T>& value)
{
    return value ? QString::number(*value) : QStringLiteral("null");
}

QString number(double value)
{
    return QString::number(value, 'g', 17);
}

QString list(const QStringList& items)
{
    return u'[' + items.join(u',') + u']';
}

QString pages(const std::vector<pdfbookmark::PageIndex>& indices)
{
    QStringList out;
    for (const auto index : indices)
        out << QString::number(index);
    return list(out);
}

QString name(const char* stableName)
{
    return QString::fromUtf8(stableName);  // SDK enum names: fixed identifiers, never document text.
}

// Joins `key=value` pairs with spaces. Values are already encoded; nothing is
// ever used as a format string (QString::arg would re-scan substituted text,
// so a title containing "%2" could be rewritten).
QString line(std::initializer_list<std::pair<const char*, QString>> fields)
{
    QStringList parts;
    for (const auto& [key, value] : fields)
        parts << QLatin1StringView(key) + u'=' + value;
    return parts.join(u' ');
}

QString flag(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

QString titleValue(const pdfbookmark::metadata::TitleValue& v)
{
    return QStringLiteral("{title:") + str(v.title) + QStringLiteral(",subtitle:") + optStr(v.subtitle) + u'}';
}

QString contributorsValue(const std::vector<pdfbookmark::metadata::Contributor>& v)
{
    QStringList out;
    for (const auto& c : v) {
        out << QStringLiteral("{name:") + str(c.name) + QStringLiteral(",role:")
                   + name(pdfbookmark::metadata::role_name(c.role)) + u'}';
    }
    return list(out);
}

QString editionValue(const pdfbookmark::metadata::EditionValue& v)
{
    return QStringLiteral("{statement:") + str(v.statement) + QStringLiteral(",ordinal:") + optNum(v.ordinal) + u'}';
}

QString yearValue(const pdfbookmark::metadata::YearValue& v)
{
    return QStringLiteral("{year:") + QString::number(v.year) + QStringLiteral(",kind:")
           + name(pdfbookmark::metadata::kind_name(v.kind)) + QStringLiteral(",statement:") + str(v.statement) + u'}';
}

template <typename T, typename Format>
void field(QStringList& lines, const char* fieldName, const pdfbookmark::metadata::Field<T>& f, Format format)
{
    lines << line({{"metadata", QString::fromLatin1(fieldName)},
                   {"status", name(pdfbookmark::metadata::status_name(f.status))},
                   {"value", f.value ? format(*f.value) : QStringLiteral("null")}});
    for (const auto& alternative : f.alternatives) {
        QStringList evidencePages;
        for (const auto& e : alternative.evidence)
            evidencePages << QString::number(e.source.page_index);
        lines << line({{"metadata", QString::fromLatin1(fieldName)},
                       {"alternative", format(alternative.value)},
                       {"score", number(alternative.score)},
                       {"evidence_pages", list(evidencePages)}});
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
    lines << line({{"metadata.searched_pages", pages(report.searched_pages)}});
    lines << line({{"metadata.covered_document", flag(report.search_covered_document)}});
    lines << line({{"metadata.cancelled", flag(report.cancelled)}});
    return lines.join(u'\n');
}

QString analysisSnapshot(const pdfbookmark::AnalysisReport& report)
{
    QStringList lines;
    lines << line({{"analysis.outcome", name(pdfbookmark::outcome_name(report.outcome))}});
    lines << line({{"analysis.search_pages", pages(report.search_pages)}});
    lines << line({{"analysis.evidence_pages", pages(report.evidence_pages)}});
    lines << line({{"analysis.candidate", optStr(report.candidate.chosen_id)},
                   {"explicit", flag(report.candidate.explicit_selection)}});
    if (report.parsed) {
        lines << line({{"parsed.candidate", str(report.parsed->candidate_id)},
                       {"completeness", QString::number(int(report.parsed->completeness))},
                       {"unparsed", QString::number(report.parsed->unparsed.size())}});
        for (const auto& e : report.parsed->entries) {
            const auto& ref = e.printed_reference;
            lines << line({{"entry", str(e.id)},
                           {"order", QString::number(e.order)},
                           {"title", str(e.title)},
                           {"printed", ref ? str(ref->literal) : QStringLiteral("null")},
                           {"hierarchy", QString::number(int(e.hierarchy.kind))},
                           {"parent", optStr(e.hierarchy.parent_id)}});
        }
    } else {
        lines << line({{"parsed", QStringLiteral("null")}});
    }
    if (report.mapping) {
        for (const auto& m : report.mapping->entries) {
            QStringList alternatives;
            for (const auto& a : m.alternatives)
                alternatives << QString::number(a.pdf_page_index);
            lines << line({{"map", str(m.entry_id)},
                           {"status", QString::number(int(m.status))},
                           {"page", optNum(m.pdf_page_index)},
                           {"method", m.method ? QString::number(int(*m.method)) : QStringLiteral("null")},
                           {"alternatives", list(alternatives)}});
        }
    } else {
        lines << line({{"mapping", QStringLiteral("null")}});
    }
    lines << line({{"plan.ready", flag(report.plan.ready)}});
    for (const auto& blocker : report.plan.blockers)
        lines << line({{"plan.blocker", str(blocker)}});
    if (report.plan.plan) {
        for (const auto& node : report.plan.plan->nodes) {
            lines << line({{"node", str(node.id)},
                           {"parent", optStr(node.parent_id)},
                           {"title", str(node.title)},
                           {"page", QString::number(node.destination.pdf_page_index)}});
        }
        for (const auto& omitted : report.plan.plan->omitted_entries)
            lines << line({{"omitted", str(omitted.entry_id)}});
        for (const auto& promotion : report.plan.plan->promotions) {
            lines << line({{"promoted", str(promotion.node_id)},
                           {"from", optStr(promotion.original_parent_id)},
                           {"to", optStr(promotion.new_parent_id)}});
        }
    } else {
        lines << line({{"plan.plan", QStringLiteral("null")}});
    }
    return lines.join(u'\n');
}

QString snapshotDigest(const QString& snapshot)
{
    return QString::fromLatin1(QCryptographicHash::hash(snapshot.toUtf8(), QCryptographicHash::Sha256).toHex());
}

} // namespace mbl::sdk
