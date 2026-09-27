// Reader: the embedded reading session behind ReaderPane.qml (Qt PDF).
//
// Document lifetime is owned here, explicitly: the view (PdfMultiPageView)
// may exist only while `viewActive` is true, and the document source is
// changed or cleared only after the view has confirmed its destruction with
// viewReleased(). So the document never changes, reloads or closes under a
// live view (docs/READER.md, "Teardown").
//
// Pages here are zero-based physical page indices; the "number" methods take
// and give the page as shown to the user (index + 1). The reading position is
// saved per book (debounced while paging, at once when switching or closing)
// and reopening a book resumes there.
#pragma once

#include "domain/ids.h"

#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QUrl>

#include <memory>
#include <optional>

namespace mbl::catalog {
class Library;
}

namespace mbl::reader {

class ReaderController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by LibraryController.reader.")
    Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)
    Q_PROPERTY(QString bookId READ bookId NOTIFY bookChanged)
    Q_PROPERTY(QString title READ title NOTIFY bookChanged)
    Q_PROPERTY(QUrl documentUrl READ documentUrl NOTIFY documentChanged)
    Q_PROPERTY(bool viewActive READ viewActive NOTIFY viewActiveChanged)
    Q_PROPERTY(int requestedPage READ requestedPage NOTIFY requestedPageChanged)  // Index the view should show.
    Q_PROPERTY(int currentPage READ currentPage NOTIFY currentPageChanged)        // Index the view shows.
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    static constexpr int kSaveDelayMs = 1000;

    explicit ReaderController(QObject* parent = nullptr);
    ~ReaderController() override;

    void setLibrary(std::shared_ptr<catalog::Library> library);

    // Opens the book where it was last read (or at its first page).
    Q_INVOKABLE void openBook(const QString& bookId);
    // Opens the book at a page as shown to the user (1 = first page), e.g. a
    // chapter's destination or the page its contents entry is printed on.
    Q_INVOKABLE void openPageNumber(const QString& bookId, int pageNumber);
    // Within the open book (1-based).
    Q_INVOKABLE void goToPageNumber(int pageNumber);
    Q_INVOKABLE void close();

    // From the view: the page it shows (index), and that it was destroyed
    // after viewActive became false.
    Q_INVOKABLE void setCurrentPage(int pageIndex);
    Q_INVOKABLE void viewReleased();

    bool isOpen() const { return m_book.has_value() || m_pending.has_value(); }
    QString bookId() const { return m_book ? m_book->toString() : QString(); }
    QString title() const { return m_title; }
    QUrl documentUrl() const { return m_documentUrl; }
    bool viewActive() const { return m_viewActive; }
    int requestedPage() const { return m_requestedPage; }
    int currentPage() const { return m_currentPage; }
    QString error() const { return m_error; }

    // Saves the current position now (e.g. before the application closes).
    void flushPosition();

signals:
    void openChanged();
    void bookChanged();
    void documentChanged();
    void viewActiveChanged();
    void requestedPageChanged();
    void currentPageChanged();
    void errorChanged();
    void bookLoaded();  // A requested book is ready to show (tests).

private:
    struct Request {
        domain::BookId book;
        std::optional<int> pageIndex;  // Nullopt: resume where last read.
    };
    struct Loaded;
    void request(const Request& request);
    void proceed();  // Once no view exists: load the pending book, or finish closing.
    void load(const Request& request);
    void apply(const Loaded& loaded);
    void setViewActive(bool active);
    void setError(const QString& error);

    std::shared_ptr<catalog::Library> m_library;
    std::optional<domain::BookId> m_book;
    std::optional<Request> m_pending;
    bool m_closing = false;
    bool m_viewActive = false;
    bool m_viewExists = false;  // Between viewActive=true and viewReleased().
    quint64 m_generation = 0;
    QString m_title;
    QUrl m_documentUrl;
    std::optional<int> m_pageCount;
    int m_requestedPage = 0;
    int m_currentPage = -1;
    std::optional<int> m_savedPage;  // Last position written for m_book.
    QString m_error;
    QTimer m_saveTimer;
};

} // namespace mbl::reader
