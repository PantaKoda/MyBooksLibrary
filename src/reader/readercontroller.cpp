#include "reader/readercontroller.h"

#include "catalog/catalog.h"
#include "catalog/library.h"
#include "catalog/reading.h"
#include "storage/librarylayout.h"

#include <QCoreApplication>
#include <QFuture>
#include <QMetaObject>

namespace mbl::reader {

using namespace mbl::domain;

struct ReaderController::Loaded {
    quint64 generation = 0;
    BookId book;
    std::optional<int> requestedPage;
    QString error;
    QString title;
    QString path;
    std::optional<int> pageCount;
    std::optional<int> savedPage;
};

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("ReaderController", text);
}

} // namespace

ReaderController::ReaderController(QObject* parent) : QObject(parent)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(kSaveDelayMs);
    connect(&m_saveTimer, &QTimer::timeout, this, &ReaderController::flushPosition);
}

ReaderController::~ReaderController()
{
    flushPosition();
}

void ReaderController::setLibrary(std::shared_ptr<catalog::Library> library)
{
    m_library = std::move(library);
}

void ReaderController::openBook(const QString& bookId)
{
    const BookId id = BookId::fromString(bookId);
    if (!id.isNull())
        request({id, std::nullopt});
}

void ReaderController::openPageNumber(const QString& bookId, int pageNumber)
{
    const BookId id = BookId::fromString(bookId);
    if (id.isNull() || pageNumber < 1)
        return;
    if (m_book == id && !m_pending && !m_closing) {
        goToPageNumber(pageNumber);  // Same book: just move.
        return;
    }
    request({id, pageNumber - 1});
}

void ReaderController::goToPageNumber(int pageNumber)
{
    if (!m_book || pageNumber < 1)
        return;
    int index = pageNumber - 1;
    if (m_pageCount)
        index = qMin(index, *m_pageCount - 1);
    // Re-emit even for the same page, so the view moves back to it.
    m_requestedPage = index;
    emit requestedPageChanged();
}

void ReaderController::close()
{
    if (!isOpen())
        return;
    flushPosition();
    ++m_generation;  // Drops a load in flight.
    m_pending.reset();
    m_closing = true;
    setViewActive(false);
    if (m_liveViews == 0)
        proceed();
}

void ReaderController::request(const Request& request)
{
    flushPosition();
    m_closing = false;
    m_pending = request;
    ++m_generation;
    emit openChanged();
    setError({});
    // The document must not change under a live view: release it first.
    setViewActive(false);
    if (m_liveViews == 0)
        proceed();  // No view was created (none yet, or the document failed).
}

void ReaderController::attachView(QObject* view)
{
    if (!view)
        return;
    ++m_liveViews;
    connect(view, &QObject::destroyed, this, &ReaderController::viewDestroyed);
}

void ReaderController::viewDestroyed()
{
    if (--m_liveViews > 0)
        return;
    // Continue after the view's destruction has finished, not inside it.
    QMetaObject::invokeMethod(this, &ReaderController::proceed, Qt::QueuedConnection);
}

void ReaderController::proceed()
{
    if (m_liveViews > 0 || m_viewActive)
        return;  // A view still exists, or one was requested meanwhile.
    if (m_pending) {
        load(*m_pending);
        return;
    }
    if (m_closing) {
        m_closing = false;
        m_documentUrl.clear();  // Closes the document; no view uses it.
        emit documentChanged();
        const bool hadBook = m_book.has_value();
        m_book.reset();
        m_title.clear();
        m_pageCount.reset();
        m_savedPage.reset();
        m_currentPage = -1;
        emit currentPageChanged();
        if (hadBook)
            emit bookChanged();
        emit openChanged();
    }
}

void ReaderController::load(const Request& request)
{
    if (!m_library)
        return;
    const quint64 generation = m_generation;
    m_library
        ->run([request, generation, root = m_library->rootDir()](QSqlDatabase& db) {
            Loaded out;
            out.generation = generation;
            out.book = request.book;
            out.requestedPage = request.pageIndex;
            auto details = catalog::bookDetails(db, request.book);
            if (!details) {
                out.error = details.error().code == ErrorCode::NotFound ? tr("This book is no longer in the library.")
                                                                        : details.error().message;
                return out;
            }
            out.title = details.value().summary.displayTitle;
            out.path = storage::LibraryLayout(root).absolute(details.value().asset.managedPath);
            out.pageCount = details.value().asset.pageCount;
            if (auto saved = catalog::readingPosition(db, request.book))
                out.savedPage = saved.value();
            return out;
        })
        .then(this, [this](const Loaded& loaded) { apply(loaded); });
}

void ReaderController::apply(const Loaded& loaded)
{
    if (loaded.generation != m_generation || !m_pending || !(m_pending->book == loaded.book))
        return;  // Superseded by a newer request or a close.
    m_pending.reset();
    if (!loaded.error.isEmpty()) {
        m_book.reset();
        m_documentUrl.clear();
        emit documentChanged();
        emit bookChanged();
        emit openChanged();
        setError(loaded.error);
        return;
    }
    const bool newBook = m_book != loaded.book;
    m_book = loaded.book;
    m_title = loaded.title;
    m_pageCount = loaded.pageCount;
    m_savedPage = loaded.savedPage;
    int page = loaded.requestedPage.value_or(loaded.savedPage.value_or(0));
    if (m_pageCount && *m_pageCount > 0)
        page = qBound(0, page, *m_pageCount - 1);
    m_requestedPage = qMax(0, page);
    m_currentPage = m_requestedPage;
    const QUrl url = QUrl::fromLocalFile(loaded.path);
    if (url != m_documentUrl) {
        m_documentUrl = url;  // No view exists now (proceed() checked).
        emit documentChanged();
    }
    if (newBook)
        emit bookChanged();
    emit requestedPageChanged();
    emit currentPageChanged();
    emit openChanged();
    setViewActive(true);
    emit bookLoaded();
}

void ReaderController::setCurrentPage(int pageIndex)
{
    if (!m_book || pageIndex < 0 || pageIndex == m_currentPage)
        return;
    m_currentPage = pageIndex;
    emit currentPageChanged();
    m_saveTimer.start();  // Saved once paging pauses.
}

void ReaderController::flushPosition()
{
    m_saveTimer.stop();
    if (!m_library || !m_book || m_currentPage < 0 || m_savedPage == m_currentPage)
        return;
    m_savedPage = m_currentPage;
    m_library->run([book = *m_book, page = m_currentPage](QSqlDatabase& db) {
        return catalog::setReadingPosition(db, book, page);
    });
}

void ReaderController::setViewActive(bool active)
{
    if (m_viewActive == active)
        return;
    m_viewActive = active;
    emit viewActiveChanged();
}

void ReaderController::setError(const QString& error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

} // namespace mbl::reader
