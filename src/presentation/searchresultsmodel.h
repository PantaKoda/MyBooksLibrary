// Presentation: search results, one row per book, best first, each with its
// best matching chapter titles. GUI-thread owned; filled with copied
// SearchResponse values. Search covers titles, authors and contents entries,
// not the full text of the books.
#pragma once

#include "domain/search.h"

#include <QAbstractListModel>
#include <QList>
#include <QQmlEngine>
#include <QVariantList>

#include <functional>

namespace mbl::presentation {

class SearchResultsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by SearchController.results.")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        BookIdRole = Qt::UserRole + 1,
        TitleRole,
        MatchTextRole,        // "Title or author" / "Contents".
        ProcessingStateRole,  // The book's metadata and contents state (pending, partial, unavailable...).
        // One map per chapter hit: title, pageText, page (index + 1, or -1),
        // sourcePage (index + 1, or -1), printedLabel, stateText, inPlan.
        ChaptersRole,
        MoreChaptersRole,     // "and n more matching entries", or empty.
    };

    using StateLookup = std::function<QString(const domain::BookId&)>;

    explicit SearchResultsModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setStateLookup(StateLookup lookup) { m_stateOf = std::move(lookup); }
    void setResults(QList<domain::BookHit> books);  // Replaces (model reset).
    void append(const QList<domain::BookHit>& books);
    void clear();
    void statesChanged();  // Books' processing states changed.

    static QVariantMap chapterMap(const domain::ChapterHit& hit);

signals:
    void countChanged();

private:
    QList<domain::BookHit> m_books;
    StateLookup m_stateOf;
};

} // namespace mbl::presentation
