#include "presentation/searchcontroller.h"

#include "catalog/library.h"
#include "search/searchindex.h"

#include <QCoreApplication>
#include <QFuture>

namespace mbl::presentation {

using namespace mbl::domain;

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("SearchController", text);
}

QString trn(const char* text, int n)
{
    return QCoreApplication::translate("SearchController", text, nullptr, n);
}

struct Outcome {
    SearchResponse response;
    QString error;
};

} // namespace

SearchController::SearchController(QObject* parent) : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kDebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, [this] { run(0); });
}

void SearchController::setLibrary(std::shared_ptr<catalog::Library> library)
{
    m_library = std::move(library);
    if (active())
        run(0);
}

void SearchController::setText(const QString& text)
{
    if (text == m_text)
        return;
    m_text = text;
    emit textChanged();
    if (!active()) {
        clear();
        return;
    }
    m_debounce.start();  // Restarts: runs once typing pauses.
}

void SearchController::setScope(int scope)
{
    const auto next = SearchScope(qBound(0, scope, int(SearchScope::Contents)));
    if (next == m_scope)
        return;
    m_scope = next;
    emit scopeChanged();
    if (active())
        run(0);
}

void SearchController::clear()
{
    m_debounce.stop();
    ++m_generation;  // Drops a response in flight.
    if (!m_text.isEmpty()) {
        m_text.clear();
        emit textChanged();
    }
    m_results.clear();
    m_totalBooks = 0;
    setSearching(false);
    setStatus({});
}

void SearchController::loadMore()
{
    if (active() && canLoadMore() && !m_searching)
        run(m_results.rowCount());
}

void SearchController::refresh()
{
    if (active()) {
        m_debounce.stop();
        run(0);
    }
}

void SearchController::run(int offset)
{
    if (!m_library || !active())
        return;
    SearchRequest request;
    request.text = m_text;
    request.scope = m_scope;
    request.offset = offset;
    request.limit = kPageSize;
    request.generation = ++m_generation;
    setSearching(true);
    m_library
        ->run([request](QSqlDatabase& db) {
            Outcome out;
            auto response = search::search(db, request);
            if (response)
                out.response = response.value();
            else
                out.error = response.error().message;
            out.response.generation = request.generation;
            return out;
        })
        .then(this, [this, offset](const Outcome& out) { apply(out.response, offset, out.error); });
}

void SearchController::apply(const SearchResponse& response, int offset, const QString& error)
{
    if (response.generation != m_generation)
        return;  // A newer query (or a clear) came after this one.
    setSearching(false);
    if (!error.isEmpty()) {
        setStatus(tr("Search failed: %1").arg(error));
        emit responseApplied();
        return;
    }
    if (offset == 0)
        m_results.setResults(response.books);
    else
        m_results.append(response.books);
    m_totalBooks = response.totalBooks;
    if (response.queryEmpty)
        setStatus(tr("Type letters or numbers to search titles, authors and contents."));
    else if (m_totalBooks == 0)
        setStatus(tr("No matches in indexed titles and contents."));
    else
        setStatus(trn("%n book(s) found in titles, authors and contents.", m_totalBooks));
    emit statusChanged();  // canLoadMore may have changed with the same text.
    emit responseApplied();
}

void SearchController::setSearching(bool searching)
{
    if (m_searching == searching)
        return;
    m_searching = searching;
    emit searchingChanged();
}

void SearchController::setStatus(const QString& text)
{
    if (m_statusText == text)
        return;
    m_statusText = text;
    emit statusChanged();
}

} // namespace mbl::presentation
