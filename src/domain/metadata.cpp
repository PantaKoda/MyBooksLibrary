#include "domain/metadata.h"

#include <utility>

namespace mbl::domain {

MetadataOverride& MetadataOverrides::operator[](MetadataField field)
{
    return const_cast<MetadataOverride&>(std::as_const(*this)[field]);
}

const MetadataOverride& MetadataOverrides::operator[](MetadataField field) const
{
    switch (field) {
    case MetadataField::Title: return title;
    case MetadataField::Subtitle: return subtitle;
    case MetadataField::Contributors: return contributors;
    case MetadataField::Edition: return edition;
    case MetadataField::PublicationYear: return publicationYear;
    case MetadataField::CopyrightYear: return copyrightYear;
    }
    Q_UNREACHABLE_RETURN(title);
}

namespace {

// Applies one override to one extracted optional value. Only Resolved
// extraction is used automatically.
template <typename T, typename FromOverride>
void resolve(const MetadataOverride& override_, FieldStatus status, const std::optional<T>& extracted,
             FromOverride fromOverride, std::optional<T>& value, ValueSource& source)
{
    switch (override_.mode) {
    case OverrideMode::Cleared:
        value.reset();
        source = ValueSource::Cleared;
        return;
    case OverrideMode::Value:
        value = fromOverride(override_);
        source = value ? ValueSource::User : ValueSource::Cleared;
        return;
    case OverrideMode::Auto:
        if (status == FieldStatus::Resolved && extracted) {
            value = extracted;
            source = ValueSource::Extracted;
        } else {
            value.reset();
            source = ValueSource::None;
        }
        return;
    }
}

} // namespace

EffectiveMetadata effectiveMetadata(const std::optional<ExtractedMetadata>& extracted,
                                    const MetadataOverrides& overrides)
{
    const ExtractedMetadata none;
    const ExtractedMetadata& e = extracted ? *extracted : none;
    EffectiveMetadata out;

    const auto text = [](const MetadataOverride& o) { return o.text; };
    const auto year = [](const MetadataOverride& o) { return o.year; };

    resolve(overrides.title, e.titleStatus, e.title, text, out.title, out.titleSource);
    // The subtitle belongs to the title field in the SDK.
    resolve(overrides.subtitle, e.titleStatus, e.subtitle, text, out.subtitle, out.subtitleSource);
    resolve(overrides.edition, e.editionStatus, e.editionStatement, text, out.edition, out.editionSource);
    resolve(overrides.publicationYear, e.publicationYearStatus, e.publicationYear, year,
            out.publicationYear, out.publicationYearSource);
    resolve(overrides.copyrightYear, e.copyrightYearStatus, e.copyrightYear, year, out.copyrightYear,
            out.copyrightYearSource);

    switch (overrides.contributors.mode) {
    case OverrideMode::Cleared:
        out.contributorsSource = ValueSource::Cleared;
        break;
    case OverrideMode::Value:
        out.contributors = overrides.contributors.contributors;
        out.contributorsSource = out.contributors.isEmpty() ? ValueSource::Cleared : ValueSource::User;
        break;
    case OverrideMode::Auto:
        if (e.contributorsStatus == FieldStatus::Resolved && !e.contributors.isEmpty()) {
            out.contributors = e.contributors;
            out.contributorsSource = ValueSource::Extracted;
        }
        break;
    }
    return out;
}

} // namespace mbl::domain
