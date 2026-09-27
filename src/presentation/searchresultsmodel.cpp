#include "presentation/searchresultsmodel.h"

#include <QCoreApplication>
#include <QVariantMap>

namespace mbl::presentation {

using namespace mbl::domain;

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("SearchResultsModel", text);
}

QString trn(const char* text, int n)
{
    return QCoreApplication::translate("SearchResultsModel", text, nullptr, n);
}

} // namespace

SearchResultsModel::SearchResultsModel(QObject* parent) : QAbstractListModel(parent) {}

int SearchResultsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_books.size());
}

QVariantMap SearchResultsModel::chapterMap(const ChapterHit& hit)
{
    // Only a resolved hit has a page to open; otherwise say so and offer the
    // page where the entry is listed, if known. No page is guessed.
    const bool resolved = hit.destinationState == DestinationState::Resolved && hit.destinationPage;
    QString pageText;
    QString stateText;
    if (resolved) {
        pageText = tr("Page %1").arg(*hit.destinationPage + 1);
    } else {
        pageText = hit.destinationState == DestinationState::Ambiguous ? tr("Page uncertain") : tr("Page not found");
        stateText = hit.sourceTocPage ? tr("Listed on page %1 of the PDF").arg(*hit.sourceTocPage + 1)
                                      : tr("Where it is listed is not known");
    }
    return QVariantMap{{QStringLiteral("title"), hit.title},
                       {QStringLiteral("pageText"), pageText},
                       {QStringLiteral("page"), resolved ? *hit.destinationPage + 1 : -1},
                       {QStringLiteral("sourcePage"), hit.sourceTocPage ? *hit.sourceTocPage + 1 : -1},
                       {QStringLiteral("printedLabel"), hit.printedLabel.value_or(QString())},
                       {QStringLiteral("stateText"), stateText},
                       {QStringLiteral("inPlan"), hit.inExportPlan}};
}

QVariant SearchResultsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_books.size())
        return {};
    const BookHit& book = m_books.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
        return book.displayTitle;
    case BookIdRole:
        return book.book.toString();
    case MatchTextRole:
        return book.metadataMatch ? (book.totalChapterMatches > 0 ? tr("Title or author, and contents") : tr("Title or author"))
                                  : tr("Contents");
    case ProcessingStateRole:
        return m_stateOf ? m_stateOf(book.book) : QString();
    case ChaptersRole: {
        QVariantList chapters;
        for (const ChapterHit& hit : book.chapters)
            chapters << chapterMap(hit);
        return chapters;
    }
    case MoreChaptersRole: {
        const int more = book.totalChapterMatches - int(book.chapters.size());
        return more > 0 ? trn("and %n more matching entries", more) : QString();
    }
    }
    return {};
}

QHash<int, QByteArray> SearchResultsModel::roleNames() const
{
    return {
        {BookIdRole, "bookId"},
        {TitleRole, "title"},
        {MatchTextRole, "matchText"},
        {ProcessingStateRole, "processingState"},
        {ChaptersRole, "chapters"},
        {MoreChaptersRole, "moreChapters"},
    };
}

int SearchResultsModel::rowOfBook(const QString& bookId) const
{
    for (qsizetype row = 0; row < m_books.size(); ++row) {
        if (m_books.at(row).book.toString() == bookId)
            return int(row);
    }
    return -1;
}

QString SearchResultsModel::bookIdAt(int row) const
{
    return row >= 0 && row < m_books.size() ? m_books.at(row).book.toString() : QString();
}

void SearchResultsModel::setResults(QList<BookHit> books)
{
    const qsizetype before = m_books.size();
    beginResetModel();
    m_books = std::move(books);
    endResetModel();
    if (m_books.size() != before)
        emit countChanged();
}

void SearchResultsModel::append(const QList<BookHit>& books)
{
    if (books.isEmpty())
        return;
    beginInsertRows({}, int(m_books.size()), int(m_books.size() + books.size() - 1));
    m_books += books;
    endInsertRows();
    emit countChanged();
}

void SearchResultsModel::clear()
{
    setResults({});
}

void SearchResultsModel::statesChanged()
{
    if (!m_books.isEmpty())
        emit dataChanged(index(0), index(int(m_books.size() - 1)), {ProcessingStateRole});
}

} // namespace mbl::presentation
