// Bibliographic metadata contracts: what an extraction run found, what the
// user overrode, and the effective values computed from both.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace mbl::domain {

// Mirrors the SDK's field states. Ambiguous is never auto-accepted.
enum class FieldStatus { Resolved, Ambiguous, NotFoundInSearch };

enum class ContributorRole { Author, Editor, Translator, Organization };

struct Contributor {
    QString name;
    ContributorRole role = ContributorRole::Author;

    friend bool operator==(const Contributor& a, const Contributor& b)
    {
        return a.name == b.name && a.role == b.role;
    }
};

// Normalised result of one metadata extraction run. Values are present only
// when the SDK resolved the field. Evidence and candidates stay in the raw
// report referenced by the run (normalised further in M04).
struct ExtractedMetadata {
    FieldStatus titleStatus = FieldStatus::NotFoundInSearch;
    std::optional<QString> title;
    std::optional<QString> subtitle;

    FieldStatus contributorsStatus = FieldStatus::NotFoundInSearch;
    QList<Contributor> contributors;  // Ordered as printed.

    FieldStatus editionStatus = FieldStatus::NotFoundInSearch;
    std::optional<QString> editionStatement;
    std::optional<int> editionOrdinal;

    FieldStatus publicationYearStatus = FieldStatus::NotFoundInSearch;
    std::optional<int> publicationYear;

    FieldStatus copyrightYearStatus = FieldStatus::NotFoundInSearch;
    std::optional<int> copyrightYear;  // Never used as a publication year.
};

enum class MetadataField { Title, Subtitle, Contributors, Edition, PublicationYear, CopyrightYear };

// Evidence and candidates behind one field of one extraction run, kept so
// the user can see why a value was (or was not) chosen. Scores rank
// candidates; they are not probabilities.
struct MetadataEvidence {
    int pageIndex = 0;   // Zero-based physical page.
    QString text;        // Supporting text as read.
    QString reason;
};

struct MetadataCandidate {
    QString value;       // Display text of the candidate value.
    double score = 0;
    QList<MetadataEvidence> evidence;
    QStringList reasons;
};

struct MetadataFieldDetail {
    MetadataField field = MetadataField::Title;  // Title covers title and subtitle.
    QList<MetadataEvidence> evidence;             // Support for the resolved value.
    QList<MetadataCandidate> alternatives;        // Other or competing candidates.
    QStringList reasons;                          // Why the field has its status.
};

// Auto: no override, use extraction. Value: user value. Cleared: deliberately
// empty; must not fall back to any extracted value.
enum class OverrideMode { Auto, Value, Cleared };

struct MetadataOverride {
    OverrideMode mode = OverrideMode::Auto;
    std::optional<QString> text;       // Title, Subtitle, Edition.
    std::optional<int> year;           // PublicationYear, CopyrightYear.
    QList<Contributor> contributors;   // Contributors.

    static MetadataOverride automatic() { return {}; }
    static MetadataOverride cleared() { return {OverrideMode::Cleared, {}, {}, {}}; }
    static MetadataOverride withText(QString value) { return {OverrideMode::Value, std::move(value), {}, {}}; }
    static MetadataOverride withYear(int value) { return {OverrideMode::Value, {}, value, {}}; }
    static MetadataOverride withContributors(QList<Contributor> value)
    {
        return {OverrideMode::Value, {}, {}, std::move(value)};
    }
};

struct MetadataOverrides {
    MetadataOverride title;
    MetadataOverride subtitle;
    MetadataOverride contributors;
    MetadataOverride edition;
    MetadataOverride publicationYear;
    MetadataOverride copyrightYear;

    MetadataOverride& operator[](MetadataField field);
    const MetadataOverride& operator[](MetadataField field) const;
};

// Where an effective value came from.
enum class ValueSource { None, Extracted, User, Cleared };

struct EffectiveMetadata {
    std::optional<QString> title;
    ValueSource titleSource = ValueSource::None;
    std::optional<QString> subtitle;
    ValueSource subtitleSource = ValueSource::None;
    QList<Contributor> contributors;
    ValueSource contributorsSource = ValueSource::None;
    std::optional<QString> edition;
    ValueSource editionSource = ValueSource::None;
    std::optional<int> publicationYear;
    ValueSource publicationYearSource = ValueSource::None;
    std::optional<int> copyrightYear;
    ValueSource copyrightYearSource = ValueSource::None;
};

// `extracted` is the active run, or nullopt when no run has been published.
EffectiveMetadata effectiveMetadata(const std::optional<ExtractedMetadata>& extracted,
                                    const MetadataOverrides& overrides);

// Stable storage codes.
QString toCode(FieldStatus status);
std::optional<FieldStatus> fieldStatusFromCode(const QString& code);
QString toCode(ContributorRole role);
std::optional<ContributorRole> contributorRoleFromCode(const QString& code);
QString toCode(MetadataField field);
std::optional<MetadataField> metadataFieldFromCode(const QString& code);
QString toCode(OverrideMode mode);
std::optional<OverrideMode> overrideModeFromCode(const QString& code);

} // namespace mbl::domain
