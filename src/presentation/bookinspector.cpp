#include "presentation/bookinspector.h"

#include "catalog/catalog.h"
#include "catalog/jobs.h"
#include "catalog/library.h"

#include <QCoreApplication>
#include <QFuture>
#include <QVariantMap>

namespace mbl::presentation {

using namespace mbl::domain;

struct BookInspector::Loaded {
    quint64 generation = 0;
    Result<BookDetails> details = makeError(ErrorCode::NotFound, {});
    QList<MetadataFieldDetail> fieldDetails;
};

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("BookInspector", text);
}

QString trn(const char* text, int n)
{
    return QCoreApplication::translate("BookInspector", text, nullptr, n);
}

QString roleText(ContributorRole role)
{
    switch (role) {
    case ContributorRole::Author: return tr("author");
    case ContributorRole::Editor: return tr("editor");
    case ContributorRole::Translator: return tr("translator");
    case ContributorRole::Organization: return tr("organization");
    }
    return {};
}

// Where the shown value comes from, or why there is none.
QString sourceText(ValueSource source, const std::optional<FieldStatus>& status, bool extracted)
{
    switch (source) {
    case ValueSource::User: return tr("Your correction");
    case ValueSource::Cleared: return tr("Cleared by you");
    case ValueSource::Extracted: return tr("From the document");
    case ValueSource::None: break;
    }
    if (!extracted)
        return tr("Not read yet");
    if (status == FieldStatus::Ambiguous)
        return tr("Uncertain: several candidates, none chosen");
    return tr("Not found in the pages searched");
}

QStringList evidenceLines(const QList<MetadataEvidence>& evidence)
{
    QStringList lines;
    for (const MetadataEvidence& e : evidence) {
        QString line = tr("Page %1: “%2”").arg(e.pageIndex + 1).arg(e.text);
        if (!e.reason.isEmpty())
            line += QStringLiteral(" — ") + e.reason;
        lines << line;
    }
    return lines;
}

// What a field shows, and what an editor needs to correct it.
struct FieldView {
    MetadataField field = MetadataField::Title;
    QString label;
    QString value;          // Effective value as shown.
    ValueSource source = ValueSource::None;
    std::optional<FieldStatus> status;
    OverrideMode mode = OverrideMode::Auto;
    QString documentValue;  // The document's value, when a correction replaces it.
};

QString kindOf(MetadataField f)
{
    switch (f) {
    case MetadataField::PublicationYear:
    case MetadataField::CopyrightYear: return QStringLiteral("year");
    case MetadataField::Contributors: return QStringLiteral("contributors");
    default: return QStringLiteral("text");
    }
}

QString yearText(const std::optional<int>& year)
{
    return year ? QString::number(*year) : QString();
}

QString contributorsText(const QList<Contributor>& contributors)
{
    QStringList people;
    for (const Contributor& c : contributors)
        people << QStringLiteral("%1 (%2)").arg(c.name, roleText(c.role));
    return people.join(QStringLiteral("; "));
}

QVariantMap field(const FieldView& v, const EffectiveMetadata& m, bool extracted, const MetadataFieldDetail* detail)
{
    QStringList evidence;
    QStringList alternatives;
    if (detail) {
        evidence = evidenceLines(detail->evidence);
        for (const QString& reason : detail->reasons)
            evidence << reason;
        // The strongest few candidates (the SDK lists them best first); the
        // rest are only counted, so a noisy title page stays readable.
        constexpr int kShown = 5;
        for (qsizetype i = 0; i < detail->alternatives.size() && i < kShown; ++i) {
            const MetadataCandidate& c = detail->alternatives.at(i);
            QString line = QStringLiteral("“%1”").arg(c.value);
            if (!c.reasons.isEmpty())
                line += QStringLiteral(" — ") + c.reasons.join(QStringLiteral("; "));
            alternatives << line;
        }
        if (detail->alternatives.size() > kShown)
            alternatives << trn("and %n more", int(detail->alternatives.size() - kShown));
    }
    // The editor starts from the value shown (nothing when cleared or absent).
    QString editText;
    QVariantList editContributors;
    switch (v.field) {
    case MetadataField::Title: editText = m.title.value_or(QString()); break;
    case MetadataField::Subtitle: editText = m.subtitle.value_or(QString()); break;
    case MetadataField::Edition: editText = m.edition.value_or(QString()); break;
    case MetadataField::PublicationYear: editText = yearText(m.publicationYear); break;
    case MetadataField::CopyrightYear: editText = yearText(m.copyrightYear); break;
    case MetadataField::Contributors:
        for (const Contributor& c : m.contributors)
            editContributors << QVariantMap{{QStringLiteral("name"), c.name}, {QStringLiteral("role"), toCode(c.role)}};
        break;
    }
    return QVariantMap{{QStringLiteral("field"), toCode(v.field)},
                       {QStringLiteral("kind"), kindOf(v.field)},
                       {QStringLiteral("label"), v.label},
                       {QStringLiteral("value"), v.value.isEmpty() ? QStringLiteral("—") : v.value},
                       {QStringLiteral("mode"), toCode(v.mode)},
                       {QStringLiteral("sourceText"), sourceText(v.source, v.status, extracted)},
                       {QStringLiteral("documentValue"), v.documentValue},
                       {QStringLiteral("editText"), editText},
                       {QStringLiteral("editContributors"), editContributors},
                       {QStringLiteral("evidence"), evidence},
                       {QStringLiteral("alternatives"), alternatives}};
}

const MetadataFieldDetail* detailOf(const QList<MetadataFieldDetail>& details, MetadataField f)
{
    for (const MetadataFieldDetail& d : details) {
        if (d.field == f)
            return &d;
    }
    return nullptr;
}

QString contentsSummaryOf(const std::optional<TocAnalysis>& toc)
{
    if (!toc)
        return tr("Contents not analyzed yet.");
    const int n = int(toc->entries.size());
    if (toc->outcome == QLatin1String("no_toc_found_in_search")) {
        return toc->searchCoveredDocument.value_or(false) ? tr("No printed contents found in the document.")
                                                          : tr("No printed contents found in the pages searched.");
    }
    if (toc->outcome == QLatin1String("search_incomplete"))
        return tr("No printed contents found, but some pages could not be read.");
    if (n == 0)
        return tr("No contents entries.");
    int resolved = 0;
    for (const TocEntry& e : toc->entries)
        resolved += e.destinationState == DestinationState::Resolved ? 1 : 0;
    if (resolved == n)
        return trn("%n contents entries, every page confirmed.", n);
    return trn("%n contents entries", n) + QStringLiteral(", ")
           + tr("%1 with a confirmed page.").arg(resolved);
}

QStringList contentsNotesOf(const std::optional<TocAnalysis>& toc)
{
    QStringList notes;
    if (!toc || toc->entries.isEmpty())
        return notes;
    int ambiguous = 0;
    int unresolved = 0;
    int unknownLevel = 0;
    for (const TocEntry& e : toc->entries) {
        ambiguous += e.destinationState == DestinationState::Ambiguous ? 1 : 0;
        unresolved += e.destinationState == DestinationState::Unresolved ? 1 : 0;
        unknownLevel += e.hierarchy == HierarchyState::Unknown ? 1 : 0;
    }
    const QString of = QStringLiteral(" %1 of %2.").arg(QStringLiteral("%1"), QString::number(toc->entries.size()));
    if (ambiguous)
        notes << tr("More than one possible page:") + of.arg(ambiguous);
    if (unresolved)
        notes << tr("No page found:") + of.arg(unresolved);
    if (unknownLevel)
        notes << tr("Level uncertain:") + of.arg(unknownLevel);
    if (toc->parseComplete == false)
        notes << tr("The contents list may be incomplete.");
    notes << (toc->planReady ? tr("Ready for a bookmarked copy.") : tr("Not ready for a bookmarked copy."));
    for (const QString& blocker : toc->planBlockers)
        notes << tr("Why: %1").arg(blocker);
    return notes;
}

} // namespace

BookInspector::BookInspector(QObject* parent) : QObject(parent) {}

void BookInspector::setLibrary(std::shared_ptr<catalog::Library> library)
{
    m_library = std::move(library);
    if (m_book)
        load();
}

QString BookInspector::bookId() const
{
    return m_book ? m_book->toString() : QString();
}

void BookInspector::select(const QString& bookId)
{
    const BookId id = BookId::fromString(bookId);
    if (id.isNull()) {
        if (m_book) {
            m_book.reset();
            ++m_generation;  // Drops a load in flight.
            clear();
            setLoading(false);
            emit bookChanged();
        }
        return;
    }
    if (m_book == id)
        return;
    m_book = id;
    m_shownTocRun.reset();
    m_shownTocRevision.reset();
    m_contentsShown = false;
    setCorrectionError({});
    emit bookChanged();
    load();
}

void BookInspector::reload()
{
    if (m_book)
        load();
}

void BookInspector::load()
{
    if (!m_library || !m_book)
        return;
    const quint64 generation = ++m_generation;
    setLoading(true);
    m_library
        ->run([book = *m_book, generation](QSqlDatabase& db) {
            Loaded out;
            out.generation = generation;
            out.details = catalog::bookDetails(db, book);
            if (out.details && out.details.value().metadataRun) {
                if (auto d = catalog::metadataDetails(db, *out.details.value().metadataRun))
                    out.fieldDetails = d.value();
            }
            return out;
        })
        .then(this, [this](const Loaded& result) { apply(result); });
}

void BookInspector::apply(const Loaded& result)
{
    if (result.generation != m_generation)
        return;  // A newer selection or reload is pending.
    setLoading(false);
    if (!result.details) {
        clear();
        m_error = result.details.error().code == ErrorCode::NotFound ? tr("This book is no longer in the library.")
                                                                     : result.details.error().message;
        emit detailsChanged();
        emit loaded();
        return;
    }
    const BookDetails& d = result.details.value();
    const EffectiveMetadata& m = d.summary.metadata;
    const bool extracted = d.extracted.has_value();
    const auto status = [&](FieldStatus ExtractedMetadata::*member) -> std::optional<FieldStatus> {
        return extracted ? std::optional<FieldStatus>((*d.extracted).*member) : std::nullopt;
    };

    m_error.clear();
    m_title = d.summary.displayTitle;
    m_fileText = d.asset.pageCount ? tr("%1 · %2 pages").arg(d.originalFileName).arg(*d.asset.pageCount)
                                   : d.originalFileName;

    // The document's value, shown next to a correction that replaces it.
    const auto documentValue = [&](MetadataField f) -> QString {
        if (!extracted || d.overrides[f].mode == OverrideMode::Auto)
            return {};
        const ExtractedMetadata& x = *d.extracted;
        switch (f) {
        case MetadataField::Title: return x.title.value_or(QString());
        case MetadataField::Subtitle: return x.subtitle.value_or(QString());
        case MetadataField::Contributors: return contributorsText(x.contributors);
        case MetadataField::Edition: return x.editionStatement.value_or(QString());
        case MetadataField::PublicationYear: return yearText(x.publicationYear);
        case MetadataField::CopyrightYear: return yearText(x.copyrightYear);
        }
        return {};
    };
    const auto view = [&](MetadataField f, const QString& label, const QString& value, ValueSource source,
                          FieldStatus ExtractedMetadata::*statusMember) {
        return FieldView{f, label, value, source, status(statusMember), d.overrides[f].mode, documentValue(f)};
    };
    // Title and subtitle share one extracted status and one set of evidence.
    m_metadataFields = {
        field(view(MetadataField::Title, tr("Title"), m.title.value_or(QString()), m.titleSource,
                   &ExtractedMetadata::titleStatus),
              m, extracted, detailOf(result.fieldDetails, MetadataField::Title)),
        field(view(MetadataField::Subtitle, tr("Subtitle"), m.subtitle.value_or(QString()), m.subtitleSource,
                   &ExtractedMetadata::titleStatus),
              m, extracted, nullptr),
        field(view(MetadataField::Contributors, tr("Authors and contributors"), contributorsText(m.contributors),
                   m.contributorsSource, &ExtractedMetadata::contributorsStatus),
              m, extracted, detailOf(result.fieldDetails, MetadataField::Contributors)),
        field(view(MetadataField::Edition, tr("Edition"), m.edition.value_or(QString()), m.editionSource,
                   &ExtractedMetadata::editionStatus),
              m, extracted, detailOf(result.fieldDetails, MetadataField::Edition)),
        field(view(MetadataField::PublicationYear, tr("Publication year"), yearText(m.publicationYear),
                   m.publicationYearSource, &ExtractedMetadata::publicationYearStatus),
              m, extracted, detailOf(result.fieldDetails, MetadataField::PublicationYear)),
        field(view(MetadataField::CopyrightYear, tr("Copyright year"), yearText(m.copyrightYear),
                   m.copyrightYearSource, &ExtractedMetadata::copyrightYearStatus),
              m, extracted, detailOf(result.fieldDetails, MetadataField::CopyrightYear)),
    };
    m_contentsSummary = contentsSummaryOf(d.toc);
    m_contentsNotes = contentsNotesOf(d.toc);

    // Rebuild the tree only when the contents changed (a new run, or an
    // edited revision saved, kept or discarded), so expanded
    // branches and the current entry survive other updates.
    const std::optional<TocRevisionId> revision =
        d.tocRevision ? std::optional<TocRevisionId>(d.tocRevision->id) : std::nullopt;
    if (!m_contentsShown || m_shownTocRun != d.tocRun || m_shownTocRevision != revision) {
        m_contents.setEntries(d.toc ? d.toc->entries : QList<TocEntry>{});
        m_shownTocRun = d.tocRun;
        m_shownTocRevision = revision;
        m_contentsShown = true;
    }
    emit detailsChanged();
    emit loaded();
}

QVariantList BookInspector::contributorRoles() const
{
    QVariantList roles;
    for (ContributorRole r : {ContributorRole::Author, ContributorRole::Editor, ContributorRole::Translator,
                              ContributorRole::Organization})
        roles << QVariantMap{{QStringLiteral("code"), toCode(r)}, {QStringLiteral("text"), roleText(r)}};
    return roles;
}

void BookInspector::setText(const QString& bookId, const QString& field, const QString& value)
{
    const auto f = metadataFieldFromCode(field);
    if (!f || kindOf(*f) != QLatin1String("text")) {
        setCorrectionError(tr("This field cannot be corrected with text."));
        return;
    }
    const QString text = value.trimmed();
    if (text.isEmpty()) {
        setCorrectionError(tr("Enter a value, or use Clear to leave the field empty."));
        return;
    }
    save(bookId, field, MetadataOverride::withText(text));
}

void BookInspector::setYear(const QString& bookId, const QString& field, const QString& value)
{
    const auto f = metadataFieldFromCode(field);
    if (!f || kindOf(*f) != QLatin1String("year")) {
        setCorrectionError(tr("This field is not a year."));
        return;
    }
    bool ok = false;
    const int year = value.trimmed().toInt(&ok);
    if (!ok || year < 1 || year > 9999) {
        setCorrectionError(tr("Enter a year such as 2019, or use Clear to leave the field empty."));
        return;
    }
    save(bookId, field, MetadataOverride::withYear(year));
}

void BookInspector::setContributors(const QString& bookId, const QVariantList& contributors)
{
    QList<Contributor> list;
    for (const QVariant& item : contributors) {
        const QVariantMap map = item.toMap();
        const QString name = map.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty())
            continue;  // A row added but left empty.
        const auto role = contributorRoleFromCode(map.value(QStringLiteral("role")).toString());
        if (!role) {
            setCorrectionError(tr("Choose a role for %1.").arg(name));
            return;
        }
        list << Contributor{name, *role};
    }
    if (list.isEmpty()) {
        setCorrectionError(tr("Enter at least one name, or use Clear to leave the field empty."));
        return;
    }
    save(bookId, toCode(MetadataField::Contributors), MetadataOverride::withContributors(list));
}

void BookInspector::clearField(const QString& bookId, const QString& field)
{
    save(bookId, field, MetadataOverride::cleared());
}

void BookInspector::useDocumentValue(const QString& bookId, const QString& field)
{
    save(bookId, field, MetadataOverride::automatic());
}

void BookInspector::save(const QString& bookId, const QString& fieldCode, const MetadataOverride& value)
{
    const BookId book = BookId::fromString(bookId);
    const auto f = metadataFieldFromCode(fieldCode);
    if (!m_library || book.isNull() || !f) {
        setCorrectionError(tr("This field cannot be corrected."));
        return;
    }
    setCorrectionError({});
    if (m_saving++ == 0)
        emit savingChanged();
    m_library
        ->run([book, field = *f, value](QSqlDatabase& db) { return catalog::setOverride(db, book, field, value); })
        .then(this, [this, book](const Status& saved) {
            if (--m_saving == 0)
                emit savingChanged();
            if (!saved) {
                if (m_book == book) {
                    setCorrectionError(saved.error().code == ErrorCode::NotFound
                                           ? tr("This book is no longer in the library.")
                                           : tr("The correction was not saved: %1").arg(saved.error().message));
                }
                return;
            }
            if (m_book == book)
                load();
            emit corrected(book.toString());
        });
}

void BookInspector::setCorrectionError(const QString& error)
{
    if (m_correctionError == error)
        return;
    m_correctionError = error;
    emit correctionErrorChanged();
}

void BookInspector::clear()
{
    m_title.clear();
    m_fileText.clear();
    m_metadataFields.clear();
    m_contentsSummary.clear();
    m_contentsNotes.clear();
    m_error.clear();
    m_shownTocRun.reset();
    m_shownTocRevision.reset();
    m_contentsShown = false;
    m_contents.setEntries({});
    emit detailsChanged();
}

void BookInspector::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

} // namespace mbl::presentation
