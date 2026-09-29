#include "processing/sdk/pagelabels.h"

#include <QMap>
#include <QPdfDocument>

#include <set>

namespace mbl::sdk {

namespace pb = pdfbookmark;

namespace {

using Style = PageLabelRun::Style;

struct Number {
    Style style = Style::Decimal;
    QString prefix;
    quint32 value = 0;
};

bool asciiLetter(QChar c)
{
    return (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z');
}

std::optional<quint32> decimal(QStringView text)
{
    if (text.isEmpty() || text.size() > 8)
        return std::nullopt;
    quint32 value = 0;
    for (const QChar c : text) {
        if (c < u'0' || c > u'9')
            return std::nullopt;
        value = value * 10 + quint32(c.unicode() - u'0');
    }
    return value;
}

QString romanText(quint32 value)
{
    static const struct {
        quint32 value;
        const char* text;
    } parts[] = {{1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"}, {90, "XC"}, {50, "L"},
                 {40, "XL"},  {10, "X"},   {9, "IX"},   {5, "V"},    {4, "IV"},  {1, "I"}};
    QString out;
    for (const auto& part : parts) {
        for (; value >= part.value; value -= part.value)
            out += QLatin1StringView(part.text);
    }
    return out;
}

// Only the canonical form ("xiv", not "xiiii"), up to 3999, in either case.
std::optional<quint32> roman(QStringView text)
{
    if (text.isEmpty() || text.size() > 15)
        return std::nullopt;
    const QString upper = text.toString().toUpper();
    int value = 0;
    for (qsizetype i = 0; i < upper.size(); ++i) {
        static const QString digits = QStringLiteral("IVXLCDM");
        static const int values[] = {1, 5, 10, 50, 100, 500, 1000};
        const qsizetype at = digits.indexOf(upper.at(i));
        if (at < 0)
            return std::nullopt;
        const qsizetype next = i + 1 < upper.size() ? digits.indexOf(upper.at(i + 1)) : -1;
        value += next > at ? -values[at] : values[at];
    }
    if (value <= 0 || value > 3999 || romanText(quint32(value)) != upper)
        return std::nullopt;
    return quint32(value);
}

// The same forms the SDK reads as printed page numbers.
std::optional<Number> number(const QString& label)
{
    const QString text = label.trimmed();
    if (const auto value = decimal(text))
        return Number{Style::Decimal, {}, *value};
    if (const auto value = roman(text))
        return Number{Style::Roman, {}, *value};
    const qsizetype hyphen = text.indexOf(u'-');
    if (hyphen < 1 || hyphen > 3 || text.indexOf(u'-', hyphen + 1) >= 0)
        return std::nullopt;
    for (qsizetype i = 0; i < hyphen; ++i) {
        if (!asciiLetter(text.at(i)))
            return std::nullopt;
    }
    const auto value = decimal(QStringView(text).mid(hyphen + 1));
    if (!value)
        return std::nullopt;
    return Number{Style::Prefixed, text.left(hyphen).toUpper(), *value};
}

std::optional<Style> labelStyle(pb::parsing::NumberingStyle style)
{
    switch (style) {
    case pb::parsing::NumberingStyle::Decimal:
        return Style::Decimal;
    case pb::parsing::NumberingStyle::Roman:
        return Style::Roman;
    case pb::parsing::NumberingStyle::PrefixedDecimal:
        return Style::Prefixed;
    case pb::parsing::NumberingStyle::Unknown:
        break;
    }
    return std::nullopt;
}

pb::parsing::NumberingStyle sdkStyle(Style style)
{
    switch (style) {
    case Style::Decimal:
        return pb::parsing::NumberingStyle::Decimal;
    case Style::Roman:
        return pb::parsing::NumberingStyle::Roman;
    case Style::Prefixed:
        return pb::parsing::NumberingStyle::PrefixedDecimal;
    }
    return pb::parsing::NumberingStyle::Unknown;
}

int placedEntries(const pb::AnalysisReport& report)
{
    int n = 0;
    if (report.mapping) {
        for (const auto& m : report.mapping->entries)
            n += m.status == pb::mapping::MappingStatus::Resolved ? 1 : 0;
    }
    return n;
}

} // namespace

QList<PageLabelRun> pageLabelRuns(const QStringList& labels)
{
    QList<PageLabelRun> runs;
    for (int page = 0; page < labels.size(); ++page) {
        const auto n = number(labels.at(page));
        if (!n)
            continue;
        if (!runs.isEmpty()) {
            PageLabelRun& last = runs.last();
            if (last.end == page && last.style == n->style && last.prefix == n->prefix
                && n->value == last.firstNumber + quint32(page - last.first)) {
                last.end = page + 1;
                continue;
            }
        }
        runs.append(PageLabelRun{n->style, n->prefix, page, page + 1, n->value});
    }
    return runs;
}

bool numberingBreaks(const QList<PageLabelRun>& runs)
{
    QMap<QPair<int, QString>, int> perStyle;
    for (const PageLabelRun& run : runs) {
        if (++perStyle[qMakePair(int(run.style), run.prefix)] > 1)
            return true;
    }
    return false;
}

std::optional<qsizetype> runHolding(const QList<PageLabelRun>& runs, PageLabelRun::Style style,
                                    const QString& prefix, quint32 number)
{
    std::optional<qsizetype> found;
    for (qsizetype i = 0; i < runs.size(); ++i) {
        const PageLabelRun& run = runs.at(i);
        if (run.style != style || run.prefix.compare(prefix, Qt::CaseInsensitive) != 0 || number < run.firstNumber
            || number - run.firstNumber >= quint32(run.end - run.first))
            continue;
        if (found)
            return std::nullopt;  // Printed twice, e.g. two sections numbered from 1.
        found = i;
    }
    return found;
}

QStringList readPageLabels(const QString& pdfPath)
{
    QPdfDocument document;
    if (document.load(pdfPath) != QPdfDocument::Error::None)
        return {};
    QStringList labels;
    labels.reserve(document.pageCount());
    for (int page = 0; page < document.pageCount(); ++page)
        labels.append(document.pageLabel(page));
    return labels;
}

std::optional<pb::AnalysisOptions> pageLabelAnalysisOptions(const QStringList& labels, const pb::AnalysisReport& first,
                                                            const pb::AnalysisOptions& options)
{
    if (first.outcome == pb::AnalysisOutcome::Cancelled || !first.parsed || !first.mapping
        || labels.size() != qsizetype(first.input.page_count))
        return std::nullopt;
    std::set<std::string> unplaced;
    for (const auto& m : first.mapping->entries) {
        if (m.status != pb::mapping::MappingStatus::Resolved)
            unplaced.insert(m.entry_id);
    }
    const QList<PageLabelRun> runs = pageLabelRuns(labels);
    if (unplaced.empty() || !numberingBreaks(runs))
        return std::nullopt;

    const std::string origin = "MyBooksLibrary: the PDF's page labels";
    std::vector<pb::mapping::NumberingSection> sections;
    for (qsizetype i = 0; i < runs.size(); ++i) {
        const PageLabelRun& run = runs.at(i);
        sections.push_back({"page-labels-" + std::to_string(i + 1), pb::PageIndex(run.first), pb::PageIndex(run.end),
                            sdkStyle(run.style), run.prefix.toStdString(), origin, false});
    }
    // With several sections of its style, an entry fits all of them: the SDK
    // places it only in the one it is associated with.
    pb::AnalysisOptions out = options;
    bool placesMore = false;
    for (const auto& entry : first.parsed->entries) {
        const auto& ref = entry.printed_reference;
        if (!ref || !ref->ordinal || ref->uncertain)
            continue;
        const auto style = labelStyle(ref->numbering);
        if (!style)
            continue;
        const auto run = runHolding(runs, *style, QString::fromStdString(ref->prefix), *ref->ordinal);
        if (!run)
            continue;
        out.entry_sections.push_back({entry.id, sections[std::size_t(*run)].id, origin});
        placesMore = placesMore || unplaced.count(entry.id) > 0;
    }
    if (!placesMore)
        return std::nullopt;
    out.sections = std::move(sections);
    return out;
}

bool placesMoreEntries(const pb::AnalysisReport& second, const pb::AnalysisReport& first)
{
    if (!first.parsed || !second.parsed || first.parsed->entries.size() != second.parsed->entries.size())
        return false;
    for (std::size_t i = 0; i < first.parsed->entries.size(); ++i) {
        const auto& a = first.parsed->entries[i];
        const auto& b = second.parsed->entries[i];
        const std::string aPage = a.printed_reference ? a.printed_reference->literal : std::string();
        const std::string bPage = b.printed_reference ? b.printed_reference->literal : std::string();
        if (a.id != b.id || a.title != b.title || aPage != bPage)
            return false;
    }
    return placedEntries(second) > placedEntries(first);
}

} // namespace mbl::sdk
