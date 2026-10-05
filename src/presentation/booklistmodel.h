// Presentation: the book list shown in the library view. GUI-thread owned;
// filled with copied BookSummary values delivered from the database thread.
// Rows are not identifiers: views keep selection by book ID (rowOfBook).
// The rows are the current view (the library, a collection or Trash); title
// and state lookups for other views (activity, search) use every known book.
#pragma once

#include "domain/book.h"
#include "domain/jobs.h"

#include <QAbstractListModel>
#include <QHash>
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
        ActivityRole,          // "running" (a job of this book runs), "waiting" (queued) or "".
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
    // Every book the lookups below should know, in any view (active and
    // trashed). Rows are unchanged.
    void setKnownBooks(const QList<domain::BookSummary>& books);

    // The latest metadata and contents jobs of each book, shown in the
    // processing state.
    // Both merge: a job older than the one shown for its book is ignored, so
    // a late snapshot cannot undo a newer state.
    void setLatestJobs(const QList<domain::JobRecord>& jobs);
    void updateJob(const domain::JobRecord& job);

    QString titleOf(const domain::BookId& id) const;  // Empty when unknown.
    QString processingStateOf(const domain::BookId& id) const;  // Empty when unknown.
    QString activityOf(const domain::BookId& id) const;         // As ActivityRole.
    Q_INVOKABLE int rowOfBook(const QString& bookId) const;  // -1 when absent.
    Q_INVOKABLE QString bookIdAt(int row) const;

signals:
    void countChanged();

private:
    bool acceptJob(const domain::JobRecord& job);  // True if it became the book's shown job.
    void emitStateChanged(const domain::BookId& id);

    QString stateOf(const domain::BookSummary& book) const;
    const domain::BookSummary* find(const domain::BookId& id) const;

    QList<domain::BookSummary> m_books;               // The rows: the current view.
    QHash<domain::BookId, domain::BookSummary> m_known;  // Lookups: every known book.
    QHash<domain::BookId, domain::JobRecord> m_metadataJobs;
    QHash<domain::BookId, domain::JobRecord> m_contentsJobs;
};

} // namespace mbl::presentation
