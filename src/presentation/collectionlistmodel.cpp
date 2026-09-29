#include "presentation/collectionlistmodel.h"

namespace mbl::presentation {

using namespace mbl::domain;

CollectionListModel::CollectionListModel(QObject* parent) : QAbstractListModel(parent) {}

int CollectionListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_collections.size());
}

QVariant CollectionListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_collections.size())
        return {};
    const CollectionSummary& c = m_collections.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return c.name;
    case CollectionIdRole:
        return c.id.toString();
    case BookCountRole:
        return c.bookCount;
    }
    return {};
}

QHash<int, QByteArray> CollectionListModel::roleNames() const
{
    return {{CollectionIdRole, "collectionId"}, {NameRole, "name"}, {BookCountRole, "bookCount"}};
}

void CollectionListModel::setCollections(const QList<CollectionSummary>& collections)
{
    bool sameRows = collections.size() == m_collections.size();
    for (qsizetype i = 0; sameRows && i < collections.size(); ++i)
        sameRows = collections.at(i).id == m_collections.at(i).id;
    if (!sameRows) {
        const bool countChanges = collections.size() != m_collections.size();
        beginResetModel();
        m_collections = collections;
        endResetModel();
        if (countChanges)
            emit countChanged();
        return;
    }
    for (qsizetype i = 0; i < collections.size(); ++i) {
        const CollectionSummary& now = collections.at(i);
        const CollectionSummary& was = m_collections.at(i);
        if (now.name == was.name && now.bookCount == was.bookCount)
            continue;
        m_collections[i] = now;
        emit dataChanged(index(int(i)), index(int(i)));
    }
}

QString CollectionListModel::nameOf(const CollectionId& id) const
{
    for (const CollectionSummary& c : m_collections) {
        if (c.id == id)
            return c.name;
    }
    return {};
}

int CollectionListModel::rowOfCollection(const QString& collectionId) const
{
    for (qsizetype i = 0; i < m_collections.size(); ++i) {
        if (m_collections.at(i).id.toString() == collectionId)
            return int(i);
    }
    return -1;
}

} // namespace mbl::presentation
