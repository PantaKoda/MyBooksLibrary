// Presentation: the library's search field. Debounces typing, runs the query
// on the database thread, and applies only the newest request's response,
// so a slow earlier query can never replace a newer one. Search covers
// titles, authors and contents entries, not the full text of the books.
#pragma once

#include "domain/search.h"
#include "presentation/searchresultsmodel.h"

#include <QObject>
#include <QQmlEngine>
#include <QTimer>

#include <memory>

namespace mbl::catalog {
class Library;
}

namespace mbl::presentation {

class SearchController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by LibraryController.search.")
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(int scope READ scope WRITE setScope NOTIFY scopeChanged)  // SearchScope as int.
    Q_PROPERTY(bool active READ active NOTIFY textChanged)               // A query is entered.
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool canLoadMore READ canLoadMore NOTIFY statusChanged)
    Q_PROPERTY(mbl::presentation::SearchResultsModel* results READ results CONSTANT)

public:
    static constexpr int kDebounceMs = 250;
    static constexpr int kPageSize = 50;  // Books per page.

    explicit SearchController(QObject* parent = nullptr);

    void setLibrary(std::shared_ptr<catalog::Library> library);

    QString text() const { return m_text; }
    void setText(const QString& text);  // Debounced.
    int scope() const { return int(m_scope); }
    void setScope(int scope);           // Runs at once.
    bool active() const { return !m_text.trimmed().isEmpty(); }
    bool searching() const { return m_searching; }
    QString statusText() const { return m_statusText; }
    bool canLoadMore() const { return int(m_results.rowCount()) < m_totalBooks; }
    SearchResultsModel* results() { return &m_results; }

    Q_INVOKABLE void clear();
    Q_INVOKABLE void loadMore();
    // Runs the current query now (e.g. after processing published new titles).
    Q_INVOKABLE void refresh();

signals:
    void textChanged();
    void scopeChanged();
    void searchingChanged();
    void statusChanged();
    void responseApplied();  // The newest response was applied (tests, views).

private:
    void run(int offset);
    void apply(const domain::SearchResponse& response, int offset, const QString& error);
    void setSearching(bool searching);
    void setStatus(const QString& text);

    std::shared_ptr<catalog::Library> m_library;
    QTimer m_debounce;
    QString m_text;
    domain::SearchScope m_scope = domain::SearchScope::All;
    quint64 m_generation = 0;  // Newest request; older responses are dropped.
    bool m_searching = false;
    int m_totalBooks = 0;
    QString m_statusText;
    SearchResultsModel m_results;
};

} // namespace mbl::presentation
