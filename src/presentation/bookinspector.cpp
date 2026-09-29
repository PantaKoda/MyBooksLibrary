#include "presentation/bookinspector.h"

#include "catalog/catalog.h"
#include "catalog/jobs.h"
#include "catalog/library.h"
#include "catalog/tocedits.h"

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
    int resolved = 0;
    int shown = 0;
    for (const TocEntry& e : toc->entries) {
        if (e.removed)
            continue;
        ++shown;
        resolved += e.destinationState == DestinationState::Resolved ? 1 : 0;
    }
    if (shown == 0)
        return n == 0 ? tr("No contents entries.") : tr("Every contents entry was removed.");
    const int m = shown;
    if (resolved == m)
        return trn("%n contents entries, every page confirmed.", m);
    return trn("%n contents entries", m) + QStringLiteral(", ")
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
    int removed = 0;
    int edited = 0;
    for (const TocEntry& e : toc->entries) {
        if (e.removed) {
            ++removed;
            continue;
        }
        edited += e.edits.isEmpty() ? 0 : 1;
        ambiguous += e.destinationState == DestinationState::Ambiguous ? 1 : 0;
        unresolved += e.destinationState == DestinationState::Unresolved ? 1 : 0;
        unknownLevel += e.hierarchy == HierarchyState::Unknown ? 1 : 0;
    }
    const qsizetype shown = toc->entries.size() - removed;
    const QString of = QStringLiteral(" %1 of %2.").arg(QStringLiteral("%1"), QString::number(shown));
    if (edited)
        notes << trn("Changed or added by you: %n.", edited);
    if (removed)
        notes << trn("Removed by you: %n (kept, not searched).", removed);
    if (ambiguous)
        notes << tr("More than one possible page:") + of.arg(ambiguous);
    if (unresolved)
        notes << tr("No page found:") + of.arg(unresolved);
    if (unknownLevel)
        notes << tr("Level uncertain:") + of.arg(unknownLevel);
    if (toc->parseComplete == false)
        notes << tr("The contents list may be incomplete.");
    // Whether a bookmarked copy can be saved, and with what, is the Export
    // dialog's to say (it bookmarks every entry with a confirmed page). The
    // analysis's own reasons are contentsReasons, shown on request.
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
    setContentsError({});
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
    m_inTrash = d.summary.lifecycle == Lifecycle::Trashed;
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
    m_contentsReasons = d.toc ? d.toc->planBlockers : QStringList();

    // Rebuild the tree only when the contents changed (a new run, or an
    // edited revision saved, kept or discarded), so expanded
    // branches and the current entry survive other updates.
    const std::optional<TocRevisionId> revision =
        d.tocRevision ? std::optional<TocRevisionId>(d.tocRevision->id) : std::nullopt;
    if (!m_contentsShown || m_shownTocRun != d.tocRun || m_shownTocRevision != revision) {
        m_contents.setEntries(d.toc ? d.toc->entries : QList<TocEntry>{});
        m_shownTocRun = d.tocRun;
        m_shownTocRevision = revision;
        m_shownEntries = d.toc ? d.toc->entries : QList<TocEntry>{};
        m_contentsShown = true;
    }
    m_pageCount = d.asset.pageCount;
    m_contentsEdited = d.tocRevision.has_value();
    m_contentsNeedReconciliation = d.tocRevision && d.tocRevision->needsReconciliation;
    if (m_contentsNeedReconciliation) {
        int analyzed = 0;
        if (d.analyzedToc)
            analyzed = int(d.analyzedToc->entries.size());
        m_contentsEditText = trn("A newer analysis found different contents (%n entries). Your edited contents are "
                                 "still shown and searched until you choose.",
                                 analyzed);
    } else if (m_contentsEdited) {
        m_contentsEditText = tr("You edited these contents (version %1). Search uses your version.")
                                 .arg(d.tocRevision->number);
    } else {
        m_contentsEditText.clear();
    }
    emit detailsChanged();
    emit loaded();
}

void BookInspector::setContentsError(const QString& error)
{
    if (m_contentsError == error)
        return;
    m_contentsError = error;
    emit contentsErrorChanged();
}

void BookInspector::changeContents(const QString& bookId, const ContentsChange& change)
{
    const BookId book = BookId::fromString(bookId);
    if (!m_library || book.isNull() || m_book != book || !m_contentsShown) {
        setContentsError(tr("Select the book again to change its contents."));
        return;
    }
    setContentsError({});
    // What the tree shows: the catalog refuses the change if that is no longer current.
    const TocEditBase base{m_shownTocRun, m_shownTocRevision};
    if (m_saving++ == 0)
        emit savingChanged();
    m_library->run([book, base, change](QSqlDatabase& db) { return change(db, book, base); })
        .then(this, [this, book](const Status& saved) {
            if (--m_saving == 0)
                emit savingChanged();
            if (!saved) {
                if (m_book != book)
                    return;
                switch (saved.error().code) {
                case ErrorCode::StaleGeneration:
                    setContentsError(tr("The contents changed while you were editing (a new analysis or another "
                                        "change). They were reloaded; please try again."));
                    load();
                    break;
                case ErrorCode::Trashed:
                    setContentsError(tr("This book is in Trash."));
                    break;
                case ErrorCode::NotFound:
                    setContentsError(tr("This book is no longer in the library."));
                    break;
                default:
                    setContentsError(tr("The change was not saved: %1").arg(saved.error().message));
                    break;
                }
                return;
            }
            if (m_book == book)
                load();
            emit corrected(book.toString());
        });
}

void BookInspector::editContents(const QString& bookId, const QList<TocEdit>& edits)
{
    changeContents(bookId, [edits](QSqlDatabase& db, const BookId& book, const TocEditBase& base) -> Status {
        auto saved = catalog::editToc(db, book, base, edits);
        if (!saved)
            return saved.error();
        return Done{};
    });
}

std::optional<int> BookInspector::pageIndexOf(const QString& pageNumber)
{
    bool ok = false;
    const int page = pageNumber.trimmed().toInt(&ok);
    if (!ok || page < 1 || (m_pageCount && page > *m_pageCount)) {
        setContentsError(m_pageCount ? tr("Enter a page number from 1 to %1.").arg(*m_pageCount)
                                     : tr("Enter a page number, such as 12."));
        return std::nullopt;
    }
    return page - 1;
}

const TocEntry* BookInspector::shownEntry(const QString& key) const
{
    for (const TocEntry& e : m_shownEntries) {
        if (e.sdkEntryId == key)
            return &e;
    }
    return nullptr;
}

namespace {

// The entry's parent key, as the tree shows it (Unknown and Root: none).
std::optional<QString> shownParentOf(const TocEntry& e)
{
    return e.hierarchy == HierarchyState::KnownParent ? e.parentSdkEntryId : std::nullopt;
}

} // namespace

// The nearest entry above it with the same parent, not removed.
std::optional<QString> BookInspector::previousSiblingOf(const TocEntry& entry) const
{
    std::optional<QString> found;
    int foundOrder = -1;
    for (const TocEntry& e : m_shownEntries) {
        if (e.removed || e.sdkEntryId == entry.sdkEntryId || e.order >= entry.order || e.order <= foundOrder)
            continue;
        if (shownParentOf(e) == shownParentOf(entry)) {
            found = e.sdkEntryId;
            foundOrder = e.order;
        }
    }
    return found;
}

bool BookInspector::canIndent(const QString& key) const
{
    const TocEntry* e = shownEntry(key);
    return e && !e->removed && previousSiblingOf(*e).has_value();
}

bool BookInspector::canOutdent(const QString& key) const
{
    const TocEntry* e = shownEntry(key);
    return e && !e->removed && shownParentOf(*e).has_value();
}

void BookInspector::renameEntry(const QString& bookId, const QString& key, const QString& title)
{
    if (title.trimmed().isEmpty()) {
        setContentsError(tr("Enter a title for the entry."));
        return;
    }
    TocEdit edit;
    edit.kind = TocEdit::Kind::Rename;
    edit.entryKey = key;
    edit.title = title.trimmed();
    editContents(bookId, {edit});
}

void BookInspector::setEntryPage(const QString& bookId, const QString& key, const QString& pageNumber)
{
    const auto page = pageIndexOf(pageNumber);
    if (!page)
        return;
    TocEdit edit;
    edit.kind = TocEdit::Kind::SetPage;
    edit.entryKey = key;
    edit.page = page;
    editContents(bookId, {edit});
}

void BookInspector::clearEntryPage(const QString& bookId, const QString& key)
{
    TocEdit edit;
    edit.kind = TocEdit::Kind::ClearPage;
    edit.entryKey = key;
    editContents(bookId, {edit});
}

void BookInspector::indentEntry(const QString& bookId, const QString& key)
{
    const TocEntry* e = shownEntry(key);
    const auto parent = e ? previousSiblingOf(*e) : std::nullopt;
    if (!parent) {
        setContentsError(tr("There is no entry above it at the same level to place it under."));
        return;
    }
    TocEdit edit;
    edit.kind = TocEdit::Kind::SetParent;
    edit.entryKey = key;
    edit.parentKey = *parent;
    editContents(bookId, {edit});
}

void BookInspector::outdentEntry(const QString& bookId, const QString& key)
{
    const TocEntry* e = shownEntry(key);
    const auto parentKey = e ? shownParentOf(*e) : std::nullopt;
    if (!parentKey) {
        setContentsError(tr("It is already at the top level."));
        return;
    }
    const TocEntry* parent = shownEntry(*parentKey);
    const auto grandparent = parent ? shownParentOf(*parent) : std::nullopt;
    TocEdit edit;
    edit.entryKey = key;
    if (grandparent) {
        edit.kind = TocEdit::Kind::SetParent;
        edit.parentKey = *grandparent;
    } else {
        edit.kind = TocEdit::Kind::MakeRoot;
    }
    editContents(bookId, {edit});
}

void BookInspector::removeEntry(const QString& bookId, const QString& key)
{
    TocEdit edit;
    edit.kind = TocEdit::Kind::Remove;
    edit.entryKey = key;
    editContents(bookId, {edit});
}

void BookInspector::restoreEntry(const QString& bookId, const QString& key)
{
    TocEdit edit;
    edit.kind = TocEdit::Kind::Restore;
    edit.entryKey = key;
    editContents(bookId, {edit});
}

void BookInspector::addEntryAfter(const QString& bookId, const QString& key, const QString& title,
                                  const QString& pageNumber)
{
    if (title.trimmed().isEmpty()) {
        setContentsError(tr("Enter a title for the entry."));
        return;
    }
    TocEdit edit;
    edit.kind = TocEdit::Kind::Add;
    edit.entryKey = key;
    edit.title = title.trimmed();
    if (!pageNumber.trimmed().isEmpty()) {
        edit.page = pageIndexOf(pageNumber);
        if (!edit.page)
            return;
    }
    editContents(bookId, {edit});
}

void BookInspector::keepContentsEdits(const QString& bookId)
{
    changeContents(bookId, [](QSqlDatabase& db, const BookId& book, const TocEditBase& base) -> Status {
        auto kept = catalog::keepTocEdits(db, book, base);
        if (!kept)
            return kept.error();
        return Done{};
    });
}

void BookInspector::useAnalyzedContents(const QString& bookId)
{
    changeContents(bookId, [](QSqlDatabase& db, const BookId& book, const TocEditBase& base) {
        return catalog::useAnalyzedToc(db, book, base);
    });
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
    m_inTrash = false;
    m_metadataFields.clear();
    m_contentsSummary.clear();
    m_contentsNotes.clear();
    m_contentsReasons.clear();
    m_error.clear();
    m_shownTocRun.reset();
    m_shownTocRevision.reset();
    m_shownEntries.clear();
    m_pageCount.reset();
    m_contentsEdited = false;
    m_contentsNeedReconciliation = false;
    m_contentsEditText.clear();
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
