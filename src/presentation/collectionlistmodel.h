// Presentation: the library's collections for the sidebar, by name, with
// their active book counts. GUI-thread owned; filled with copied values.
// Rows are not identifiers: views select by collection ID.
#pragma once

#include "domain/collection.h"

#include <QAbstractListModel>
#include <QList>
#include <QQmlEngine>

namespace mbl::presentation {

class CollectionListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by LibraryController.collections.")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        CollectionIdRole = Qt::UserRole + 1,
        NameRole,
        BookCountRole,
    };

    explicit CollectionListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Same collections in the same order: only changed rows are updated, so
    // the sidebar keeps its state; otherwise the model is reset.
    void setCollections(const QList<domain::CollectionSummary>& collections);
    // Name of the collection, or empty.
    QString nameOf(const domain::CollectionId& id) const;
    Q_INVOKABLE int rowOfCollection(const QString& collectionId) const;  // -1 when absent.

signals:
    void countChanged();

private:
    QList<domain::CollectionSummary> m_collections;
};

} // namespace mbl::presentation
