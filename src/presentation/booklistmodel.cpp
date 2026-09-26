#include "presentation/booklistmodel.h"

#include <QCoreApplication>
#include <QStringList>

namespace mbl::presentation {

namespace {

QString stateText(const domain::BookSummary& book)
{
    // M03 has no processing yet; metadata and contents arrive with M04/M05.
    if (!book.hasMetadataRun && !book.hasTocRun)
        return QCoreApplication::translate("BookListModel", "Imported · not analyzed yet");
    if (!book.hasTocRun)
        return QCoreApplication::translate("BookListModel", "Metadata ready · contents not analyzed");
    return QCoreApplication::translate("BookListModel", "%n contents entries", nullptr, book.tocEntryCount);
}

} // namespace

BookListModel::BookListModel(QObject* parent) : QAbstractListModel(parent) {}

int BookListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_books.size());
}

QVariant BookListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_books.size())
        return {};
    const domain::BookSummary& book = m_books.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
        return book.displayTitle;
    case BookIdRole:
        return book.id.toString();
    case TitleFromFileNameRole:
        return book.displayTitleFromFileName;
    case ContributorsRole: {
        QStringList names;
        for (const auto& c : book.metadata.contributors)
            names << c.name;
        return names.join(QStringLiteral(", "));
    }
    case ProcessingStateRole:
        return stateText(book);
    }
    return {};
}

QHash<int, QByteArray> BookListModel::roleNames() const
{
    return {
        {BookIdRole, "bookId"},
        {TitleRole, "title"},
        {TitleFromFileNameRole, "titleFromFileName"},
        {ContributorsRole, "contributors"},
        {ProcessingStateRole, "processingState"},
    };
}

void BookListModel::setBooks(QList<domain::BookSummary> books)
{
    const bool countChanges = books.size() != m_books.size();
    beginResetModel();
    m_books = std::move(books);
    endResetModel();
    if (countChanges)
        emit countChanged();
}

int BookListModel::rowOfBook(const QString& bookId) const
{
    for (qsizetype i = 0; i < m_books.size(); ++i) {
        if (m_books.at(i).id.toString() == bookId)
            return int(i);
    }
    return -1;
}

QString BookListModel::bookIdAt(int row) const
{
    return row >= 0 && row < m_books.size() ? m_books.at(row).id.toString() : QString();
}

} // namespace mbl::presentation
