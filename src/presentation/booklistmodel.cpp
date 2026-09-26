#include "presentation/booklistmodel.h"

#include <QCoreApplication>
#include <QSet>
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
    // Apply the new list as row-level changes keyed by book ID (removals,
    // moves, insertions, dataChanged) rather than a model reset, so views
    // keep their scroll position, current item and delegates.
    const qsizetype oldCount = m_books.size();

    QSet<domain::BookId> wanted;
    for (const domain::BookSummary& book : books)
        wanted.insert(book.id);
    for (qsizetype row = m_books.size() - 1; row >= 0; --row) {
        if (!wanted.contains(m_books.at(row).id)) {
            beginRemoveRows({}, int(row), int(row));
            m_books.removeAt(row);
            endRemoveRows();
        }
    }

    for (qsizetype target = 0; target < books.size(); ++target) {
        domain::BookSummary& incoming = books[target];
        if (target < m_books.size() && m_books.at(target).id == incoming.id) {
            // Same book in place: replace it, and notify if it changed.
            const bool changed = m_books.at(target).revision != incoming.revision;
            m_books[target] = std::move(incoming);
            if (changed)
                emit dataChanged(index(int(target)), index(int(target)));
            continue;
        }
        qsizetype from = -1;
        for (qsizetype row = target + 1; row < m_books.size(); ++row) {
            if (m_books.at(row).id == incoming.id) {
                from = row;
                break;
            }
        }
        if (from >= 0) {
            // Later in the list: move it up to its new position.
            const bool changed = m_books.at(from).revision != incoming.revision;
            beginMoveRows({}, int(from), int(from), {}, int(target));
            m_books.move(from, target);
            endMoveRows();
            m_books[target] = std::move(incoming);
            if (changed)
                emit dataChanged(index(int(target)), index(int(target)));
        } else {
            beginInsertRows({}, int(target), int(target));
            m_books.insert(target, std::move(incoming));
            endInsertRows();
        }
    }

    if (m_books.size() != oldCount)
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
