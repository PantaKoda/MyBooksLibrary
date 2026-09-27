// Stable storage codes for domain enums. Never change an existing code.
#include "domain/book.h"
#include "domain/importing.h"
#include "domain/jobs.h"
#include "domain/metadata.h"
#include "domain/toc.h"

#include <utility>

namespace mbl::domain {

namespace {

template <typename Enum, size_t N>
std::optional<Enum> find(const QString& code, const std::pair<Enum, const char*> (&table)[N])
{
    for (const auto& [value, text] : table) {
        if (code == QLatin1StringView(text))
            return value;
    }
    return std::nullopt;
}

template <typename Enum, size_t N>
QString name(Enum value, const std::pair<Enum, const char*> (&table)[N])
{
    for (const auto& [v, text] : table) {
        if (v == value)
            return QString::fromLatin1(text);
    }
    Q_UNREACHABLE_RETURN(QString());
}

const std::pair<HierarchyState, const char*> kHierarchy[] = {
    {HierarchyState::Root, "root"},
    {HierarchyState::KnownParent, "known_parent"},
    {HierarchyState::Unknown, "unknown"},
};
const std::pair<DestinationState, const char*> kDestination[] = {
    {DestinationState::Resolved, "resolved"},
    {DestinationState::Ambiguous, "ambiguous"},
    {DestinationState::Unresolved, "unresolved"},
};
const std::pair<Lifecycle, const char*> kLifecycle[] = {
    {Lifecycle::Active, "active"},
    {Lifecycle::Trashed, "trashed"},
};

const std::pair<FieldStatus, const char*> kFieldStatus[] = {
    {FieldStatus::Resolved, "resolved"},
    {FieldStatus::Ambiguous, "ambiguous"},
    {FieldStatus::NotFoundInSearch, "not_found_in_search"},
};
const std::pair<ContributorRole, const char*> kRole[] = {
    {ContributorRole::Author, "author"},
    {ContributorRole::Editor, "editor"},
    {ContributorRole::Translator, "translator"},
    {ContributorRole::Organization, "organization"},
};
const std::pair<MetadataField, const char*> kField[] = {
    {MetadataField::Title, "title"},
    {MetadataField::Subtitle, "subtitle"},
    {MetadataField::Contributors, "contributors"},
    {MetadataField::Edition, "edition"},
    {MetadataField::PublicationYear, "publication_year"},
    {MetadataField::CopyrightYear, "copyright_year"},
};
const std::pair<OverrideMode, const char*> kMode[] = {
    {OverrideMode::Auto, "auto"},
    {OverrideMode::Value, "value"},
    {OverrideMode::Cleared, "cleared"},
};

const std::pair<ImportPhase, const char*> kImportPhase[] = {
    {ImportPhase::Copying, "copying"},
    {ImportPhase::Verified, "verified"},
    {ImportPhase::Registered, "registered"},
    {ImportPhase::Duplicate, "duplicate"},
    {ImportPhase::Failed, "failed"},
    {ImportPhase::Cancelled, "cancelled"},
    {ImportPhase::Abandoned, "abandoned"},
};

const std::pair<JobKind, const char*> kJobKind[] = {
    {JobKind::Metadata, "metadata"},
    {JobKind::Toc, "toc"},
};
const std::pair<JobState, const char*> kJobState[] = {
    {JobState::Queued, "queued"},
    {JobState::Running, "running"},
    {JobState::CancelRequested, "cancel_requested"},
    {JobState::Succeeded, "succeeded"},
    {JobState::Failed, "failed"},
    {JobState::Cancelled, "cancelled"},
    {JobState::Interrupted, "interrupted"},
};

} // namespace

QString toCode(JobKind v) { return name(v, kJobKind); }
std::optional<JobKind> jobKindFromCode(const QString& c) { return find(c, kJobKind); }
QString toCode(JobState v) { return name(v, kJobState); }
std::optional<JobState> jobStateFromCode(const QString& c) { return find(c, kJobState); }
bool isOpen(JobState s) { return s == JobState::Queued || s == JobState::Running || s == JobState::CancelRequested; }

QString toCode(ImportPhase v) { return name(v, kImportPhase); }
std::optional<ImportPhase> importPhaseFromCode(const QString& c) { return find(c, kImportPhase); }
bool isTerminal(ImportPhase phase) { return phase != ImportPhase::Copying && phase != ImportPhase::Verified; }

QString toCode(FieldStatus v) { return name(v, kFieldStatus); }
std::optional<FieldStatus> fieldStatusFromCode(const QString& c) { return find(c, kFieldStatus); }
QString toCode(ContributorRole v) { return name(v, kRole); }
std::optional<ContributorRole> contributorRoleFromCode(const QString& c) { return find(c, kRole); }
QString toCode(MetadataField v) { return name(v, kField); }
std::optional<MetadataField> metadataFieldFromCode(const QString& c) { return find(c, kField); }
QString toCode(OverrideMode v) { return name(v, kMode); }
std::optional<OverrideMode> overrideModeFromCode(const QString& c) { return find(c, kMode); }
QString toCode(HierarchyState v) { return name(v, kHierarchy); }
std::optional<HierarchyState> hierarchyStateFromCode(const QString& c) { return find(c, kHierarchy); }
QString toCode(DestinationState v) { return name(v, kDestination); }
std::optional<DestinationState> destinationStateFromCode(const QString& c) { return find(c, kDestination); }
QString toCode(Lifecycle v) { return name(v, kLifecycle); }
std::optional<Lifecycle> lifecycleFromCode(const QString& c) { return find(c, kLifecycle); }

} // namespace mbl::domain
