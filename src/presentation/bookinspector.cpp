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

QVariantMap field(const QString& label, const QString& value, ValueSource source,
                  const std::optional<FieldStatus>& status, bool extracted, const MetadataFieldDetail* detail)
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
    return QVariantMap{{QStringLiteral("label"), label},
                       {QStringLiteral("value"), value.isEmpty() ? QStringLiteral("—") : value},
                       {QStringLiteral("sourceText"), sourceText(source, status, extracted)},
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
    m_contentsShown = false;
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

    QString title = m.title.value_or(QString());
    if (m.subtitle)
        title += QStringLiteral(": ") + *m.subtitle;
    QStringList people;
    for (const Contributor& c : m.contributors)
        people << QStringLiteral("%1 (%2)").arg(c.name, roleText(c.role));
    const auto year = [](const std::optional<int>& y) { return y ? QString::number(*y) : QString(); };
    m_metadataFields = {
        field(tr("Title"), title, m.titleSource, status(&ExtractedMetadata::titleStatus), extracted,
              detailOf(result.fieldDetails, MetadataField::Title)),
        field(tr("Authors and contributors"), people.join(QStringLiteral("; ")), m.contributorsSource,
              status(&ExtractedMetadata::contributorsStatus), extracted,
              detailOf(result.fieldDetails, MetadataField::Contributors)),
        field(tr("Edition"), m.edition.value_or(QString()), m.editionSource, status(&ExtractedMetadata::editionStatus),
              extracted, detailOf(result.fieldDetails, MetadataField::Edition)),
        field(tr("Publication year"), year(m.publicationYear), m.publicationYearSource,
              status(&ExtractedMetadata::publicationYearStatus), extracted,
              detailOf(result.fieldDetails, MetadataField::PublicationYear)),
        field(tr("Copyright year"), year(m.copyrightYear), m.copyrightYearSource,
              status(&ExtractedMetadata::copyrightYearStatus), extracted,
              detailOf(result.fieldDetails, MetadataField::CopyrightYear)),
    };
    m_contentsSummary = contentsSummaryOf(d.toc);
    m_contentsNotes = contentsNotesOf(d.toc);

    // Rebuild the tree only when the contents run changed, so expanded
    // branches and the current entry survive other updates.
    if (!m_contentsShown || m_shownTocRun != d.tocRun) {
        m_contents.setEntries(d.toc ? d.toc->entries : QList<TocEntry>{});
        m_shownTocRun = d.tocRun;
        m_contentsShown = true;
    }
    emit detailsChanged();
    emit loaded();
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
