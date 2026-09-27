// Reader: the reading session without a view. The test plays the view's
// part: it confirms the view's release with viewReleased(), and checks that
// the document never changes before that.
#include "catalog/catalog.h"
#include "catalog/library.h"
#include "catalog/reading.h"
#include "reader/readercontroller.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace mbl::domain;
using mbl::catalog::Library;
using mbl::reader::ReaderController;

namespace {

BookId addBook(QSqlDatabase& db, int n, int pages)
{
    NewBook book;
    book.asset.id = AssetId::create();
    book.asset.sha256 = QStringLiteral("%1").arg(n, 64, 10, QLatin1Char('0'));
    book.asset.byteSize = 1;
    book.asset.pageCount = pages;
    book.asset.managedPath = QStringLiteral("files/%1/source.pdf").arg(book.asset.id.toString());
    book.originalFileName = QStringLiteral("book %1.pdf").arg(n);
    book.originalPath = book.originalFileName;
    return mbl::catalog::registerBook(db, book).value();
}

} // namespace

class TestReaderController : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void opensWhereLastReadAndSavesPosition();
    void documentNeverChangesUnderALiveView();
    void closeWaitsForTheViewThenClosesTheDocument();
    void newestOpenWins();
    void pagesAreClampedAndUnknownBooksReported();

private:
    template <typename Task>
    auto db(Task task) { return m_library->run(std::move(task)).result(); }
    std::optional<int> positionOf(const BookId& book)
    {
        return db([book](QSqlDatabase& d) { return mbl::catalog::readingPosition(d, book); }).value();
    }
    void waitLoaded(QSignalSpy& spy, qsizetype count) { QTRY_COMPARE_WITH_TIMEOUT(spy.size(), count, 5000); }

    std::unique_ptr<QTemporaryDir> m_dir;
    std::shared_ptr<Library> m_library;
    BookId m_a;
    BookId m_b;
};

void TestReaderController::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    auto opened = Library::open(m_dir->path());
    QVERIFY(opened);
    m_library = std::shared_ptr<Library>(std::move(opened.value()));
    m_a = db([](QSqlDatabase& d) { return addBook(d, 1, 10); });
    m_b = db([](QSqlDatabase& d) { return addBook(d, 2, 5); });
}

void TestReaderController::cleanup()
{
    m_library.reset();
    m_dir.reset();
}

void TestReaderController::opensWhereLastReadAndSavesPosition()
{
    ReaderController r;
    r.setLibrary(m_library);
    QSignalSpy loaded(&r, &ReaderController::bookLoaded);
    r.openBook(m_a.toString());
    QVERIFY(r.isOpen());
    waitLoaded(loaded, 1);
    QCOMPARE(r.bookId(), m_a.toString());
    QCOMPARE(r.requestedPage(), 0);  // Never read: first page.
    QVERIFY(r.viewActive());
    QVERIFY(r.documentUrl().toLocalFile().endsWith(QStringLiteral("/source.pdf")));

    r.setCurrentPage(3);
    r.setCurrentPage(4);  // Paging: saved once it pauses.
    QVERIFY(!positionOf(m_a));
    QTRY_COMPARE_WITH_TIMEOUT(positionOf(m_a), std::optional<int>(4), ReaderController::kSaveDelayMs * 3);

    // Another book: the view goes first, then the new book resumes at its own page.
    r.openPageNumber(m_b.toString(), 3);
    QVERIFY(!r.viewActive());
    r.viewReleased();
    waitLoaded(loaded, 2);
    QCOMPARE(r.requestedPage(), 2);  // Page number 3 = index 2.
    r.setCurrentPage(1);
    // Back to A: B's position is saved at once, A resumes at 4.
    r.openBook(m_a.toString());
    QCOMPARE(positionOf(m_b), std::optional<int>(1));
    r.viewReleased();
    waitLoaded(loaded, 3);
    QCOMPARE(r.requestedPage(), 4);

    // The same book, another page: no reload, the view just moves.
    QSignalSpy requested(&r, &ReaderController::requestedPageChanged);
    r.openPageNumber(m_a.toString(), 8);
    QVERIFY(r.viewActive());
    QCOMPARE(r.requestedPage(), 7);
    QCOMPARE(requested.size(), 1);
    QCOMPARE(loaded.size(), 3);
}

void TestReaderController::documentNeverChangesUnderALiveView()
{
    ReaderController r;
    r.setLibrary(m_library);
    QSignalSpy loaded(&r, &ReaderController::bookLoaded);
    r.openBook(m_a.toString());
    waitLoaded(loaded, 1);
    const QUrl a = r.documentUrl();
    QSignalSpy documents(&r, &ReaderController::documentChanged);

    r.openBook(m_b.toString());
    QVERIFY(!r.viewActive());
    QTest::qWait(300);  // The view has not confirmed its release yet.
    QCOMPARE(r.documentUrl(), a);
    QCOMPARE(documents.size(), 0);
    QCOMPARE(loaded.size(), 1);

    r.viewReleased();
    waitLoaded(loaded, 2);
    QVERIFY(r.documentUrl() != a);
    QCOMPARE(documents.size(), 1);
    QVERIFY(r.viewActive());
}

void TestReaderController::closeWaitsForTheViewThenClosesTheDocument()
{
    ReaderController r;
    r.setLibrary(m_library);
    QSignalSpy loaded(&r, &ReaderController::bookLoaded);
    r.openBook(m_a.toString());
    waitLoaded(loaded, 1);
    r.setCurrentPage(6);
    r.close();
    QCOMPARE(positionOf(m_a), std::optional<int>(6));  // Saved at once.
    QVERIFY(!r.viewActive());
    QTest::qWait(100);
    QVERIFY(!r.documentUrl().isEmpty());  // Still open: the view is being destroyed.
    r.viewReleased();
    QTRY_VERIFY_WITH_TIMEOUT(r.documentUrl().isEmpty(), 5000);
    QVERIFY(!r.isOpen());
    QCOMPARE(r.bookId(), QString());

    // Reopening resumes where it was closed.
    r.openBook(m_a.toString());
    waitLoaded(loaded, 2);
    QCOMPARE(r.requestedPage(), 6);
}

void TestReaderController::newestOpenWins()
{
    ReaderController r;
    r.setLibrary(m_library);
    QSignalSpy loaded(&r, &ReaderController::bookLoaded);
    r.openBook(m_a.toString());
    r.openBook(m_b.toString());  // Before A's load arrives.
    waitLoaded(loaded, 1);
    QTest::qWait(200);
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(r.bookId(), m_b.toString());

    // A close while a book loads drops the load.
    r.viewReleased();
    r.openBook(m_a.toString());
    r.close();
    QTest::qWait(300);
    QCOMPARE(loaded.size(), 1);
    QVERIFY(!r.isOpen());
}

void TestReaderController::pagesAreClampedAndUnknownBooksReported()
{
    ReaderController r;
    r.setLibrary(m_library);
    QSignalSpy loaded(&r, &ReaderController::bookLoaded);
    r.openPageNumber(m_b.toString(), 99);  // B has 5 pages.
    waitLoaded(loaded, 1);
    QCOMPARE(r.requestedPage(), 4);
    r.goToPageNumber(50);
    QCOMPARE(r.requestedPage(), 4);
    r.goToPageNumber(0);  // Not a page number: ignored.
    QCOMPARE(r.requestedPage(), 4);

    QSignalSpy errors(&r, &ReaderController::errorChanged);
    r.openBook(BookId::create().toString());
    r.viewReleased();
    QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 5000);
    QCOMPARE(r.error(), QStringLiteral("This book is no longer in the library."));
    QVERIFY(!r.isOpen());
    QVERIFY(!r.viewActive());
}

QTEST_GUILESS_MAIN(TestReaderController)
#include "tst_readercontroller.moc"
