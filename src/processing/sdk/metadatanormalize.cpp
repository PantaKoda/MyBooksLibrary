#include "processing/sdk/metadatanormalize.h"

#include <QStringList>

namespace mbl::sdk {

namespace pm = pdfbookmark::metadata;
using namespace mbl::domain;

namespace {

QString text(const std::string& s)
{
    return QString::fromStdString(s);
}

FieldStatus status(pm::FieldStatus s)
{
    switch (s) {
    case pm::FieldStatus::Resolved: return FieldStatus::Resolved;
    case pm::FieldStatus::Ambiguous: return FieldStatus::Ambiguous;
    case pm::FieldStatus::NotFoundInSearch: return FieldStatus::NotFoundInSearch;
    }
    return FieldStatus::NotFoundInSearch;
}

ContributorRole role(pm::ContributorRole r)
{
    switch (r) {
    case pm::ContributorRole::Author: return ContributorRole::Author;
    case pm::ContributorRole::Editor: return ContributorRole::Editor;
    case pm::ContributorRole::Translator: return ContributorRole::Translator;
    case pm::ContributorRole::Organization: return ContributorRole::Organization;
    }
    return ContributorRole::Author;
}

QList<MetadataEvidence> evidence(const std::vector<pm::Evidence>& in)
{
    QList<MetadataEvidence> out;
    for (const pm::Evidence& e : in)
        out << MetadataEvidence{int(e.source.page_index), text(e.text), text(e.reason)};
    return out;
}

QStringList strings(const std::vector<std::string>& in)
{
    QStringList out;
    for (const std::string& s : in)
        out << text(s);
    return out;
}

// Display text of candidate values.
QString display(const pm::TitleValue& v)
{
    return v.subtitle ? text(v.title) + QStringLiteral(": ") + text(*v.subtitle) : text(v.title);
}
QString display(const std::vector<pm::Contributor>& v)
{
    QStringList out;
    for (const pm::Contributor& c : v)
        out << QStringLiteral("%1 (%2)").arg(text(c.name), QString::fromUtf8(pm::role_name(c.role)));
    return out.join(QStringLiteral("; "));
}
QString display(const pm::EditionValue& v)
{
    return text(v.statement);
}
QString display(const pm::YearValue& v)
{
    return QStringLiteral("%1 (%2): %3").arg(v.year).arg(QString::fromUtf8(pm::kind_name(v.kind)), text(v.statement));
}

template <typename T>
MetadataFieldDetail detail(MetadataField field, const pm::Field<T>& f)
{
    MetadataFieldDetail d;
    d.field = field;
    d.evidence = evidence(f.evidence);
    d.reasons = strings(f.reasons);
    for (const auto& c : f.alternatives)
        d.alternatives << MetadataCandidate{display(c.value), c.score, evidence(c.evidence), strings(c.reasons)};
    return d;
}

} // namespace

ExtractedMetadata normalizeMetadata(const pm::MetadataResult& r, QList<MetadataFieldDetail>* details)
{
    ExtractedMetadata m;
    m.titleStatus = status(r.title.status);
    if (m.titleStatus == FieldStatus::Resolved && r.title.value) {
        m.title = text(r.title.value->title);
        if (r.title.value->subtitle)
            m.subtitle = text(*r.title.value->subtitle);
    }

    m.contributorsStatus = status(r.contributors.status);
    if (m.contributorsStatus == FieldStatus::Resolved && r.contributors.value) {
        for (const pm::Contributor& c : *r.contributors.value)
            m.contributors << Contributor{text(c.name), role(c.role)};
    }

    m.editionStatus = status(r.edition.status);
    if (m.editionStatus == FieldStatus::Resolved && r.edition.value) {
        m.editionStatement = text(r.edition.value->statement);
        if (r.edition.value->ordinal)
            m.editionOrdinal = int(*r.edition.value->ordinal);
    }

    // The SDK already separates the two kinds of year; never mix them.
    m.publicationYearStatus = status(r.publication_year.status);
    if (m.publicationYearStatus == FieldStatus::Resolved && r.publication_year.value)
        m.publicationYear = r.publication_year.value->year;
    m.copyrightYearStatus = status(r.copyright_year.status);
    if (m.copyrightYearStatus == FieldStatus::Resolved && r.copyright_year.value)
        m.copyrightYear = r.copyright_year.value->year;

    if (details) {
        details->clear();
        MetadataFieldDetail title = detail(MetadataField::Title, r.title);
        // Title candidates keep their title and subtitle apart, so one can be
        // chosen as a correction as it was read.
        for (qsizetype i = 0; i < title.alternatives.size() && i < qsizetype(r.title.alternatives.size()); ++i) {
            const pm::TitleValue& v = r.title.alternatives.at(std::size_t(i)).value;
            title.alternatives[i].title = text(v.title);
            if (v.subtitle)
                title.alternatives[i].subtitle = text(*v.subtitle);
        }
        *details << title << detail(MetadataField::Contributors, r.contributors)
                 << detail(MetadataField::Edition, r.edition)
                 << detail(MetadataField::PublicationYear, r.publication_year)
                 << detail(MetadataField::CopyrightYear, r.copyright_year);
    }
    return m;
}

} // namespace mbl::sdk
