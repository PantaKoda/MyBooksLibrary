// Presentation: the book list shown in the library view. GUI-thread owned;
// filled with copied BookSummary values delivered from the database thread.
// Rows are not identifiers: views keep selection by book ID (rowOfBook).
#pragma once

#include "domain/book.h"

#include <QAbstractListModel>
#include <QList>
#include <QQmlEngine>

namespace mbl::presentation {

class BookListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by LibraryController.books.")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        BookIdRole = Qt::UserRole + 1,
        TitleRole,             // Effective title, or the file name without extension.
        TitleFromFileNameRole, // True when no title is known yet.
        ContributorsRole,      // Effective contributor names joined for display.
        ProcessingStateRole,   // Plain-language processing state.
    };

    explicit BookListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Makes the rows equal to `books` (in that order) with row-level changes
    // keyed by book ID -- never a model reset -- so views keep their scroll
    // position and current item. A row whose revision changed gets
    // dataChanged. Must be called on the model's thread.
    void setBooks(QList<domain::BookSummary> books);

    Q_INVOKABLE int rowOfBook(const QString& bookId) const;  // -1 when absent.
    Q_INVOKABLE QString bookIdAt(int row) const;

signals:
    void countChanged();

private:
    QList<domain::BookSummary> m_books;
};

} // namespace mbl::presentation
