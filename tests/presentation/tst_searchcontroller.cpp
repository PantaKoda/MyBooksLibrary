// Presentation: the search field's controller. Debounced, newest request
// wins, honest texts for entries without a confirmed page, paginated.
#include "catalog/catalog.h"
#include "catalog/library.h"
#include "presentation/searchcontroller.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace mbl::domain;
using mbl::catalog::Library;
using mbl::presentation::SearchController;
using mbl::presentation::SearchResultsModel;

namespace {

BookId addBook(QSqlDatabase& db, int n, const QString& title)
{
    NewBook book;
    book.asset.id = AssetId::create();
    book.asset.sha256 = QStringLiteral("%1").arg(n, 64, 10, QLatin1Char('0'));
    book.asset.byteSize = 1;
    book.asset.pageCount = 20;
    book.asset.managedPath = QStringLiteral("files/%1/source.pdf").arg(book.asset.id.toString());
    book.originalFileName = QStringLiteral("book %1.pdf").arg(n);
    book.originalPath = book.originalFileName;
    const BookId id = mbl::catalog::registerBook(db, book).value();
    if (!title.isEmpty()) {
        const PublishTicket ticket = mbl::catalog::requestMetadataRun(db, id).value();
        ExtractedMetadata m;
        m.titleStatus = FieldStatus::Resolved;
        m.title = title;
        RunIdentity run;
        run.sourceSha256 = book.asset.sha256;
        run.sdkVersion = QStringLiteral("test");
        run.optionsJson = QStringLiteral("{}");
        run.outcome = QStringLiteral("completed");
        mbl::catalog::publishMetadata(db, ticket, run, m).value();
    }
    return id;
}

void addContents(QSqlDatabase& db, const BookId& id, int n, const QList<TocEntry>& entries)
{
    const PublishTicket ticket = mbl::catalog::requestTocRun(db, id).value();
    TocAnalysis toc;
    toc.outcome = QStringLiteral("analysis_partial");
    toc.entries = entries;
    RunIdentity run;
    run.sourceSha256 = QStringLiteral("%1").arg(n, 64, 10, QLatin1Char('0'));
    run.sdkVersion = QStringLiteral("test");
    run.optionsJson = QStringLiteral("{}");
    run.outcome = toc.outcome;
    mbl::catalog::publishToc(db, ticket, run, toc).value();
}

TocEntry tocEntry(const char* id, int order, const char* title)
{
    TocEntry e;
    e.sdkEntryId = QString::fromLatin1(id);
    e.order = order;
    e.title = QString::fromUtf8(title);
    e.hierarchy = HierarchyState::Root;
    return e;
}

QVariantMap chapter(SearchController& s, int row, int hit)
{
    return s.results()->data(s.results()->index(row), SearchResultsModel::ChaptersRole).toList().at(hit).toMap();
}

QString titleAt(SearchController& s, int row)
{
    return s.results()->data(s.results()->index(row), SearchResultsModel::TitleRole).toString();
}

} // namespace

class TestSearchController : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void debouncedAndNewestWins();
    void clearDropsAResponseInFlight();
    void chapterHitsNeverGuessAPage();
    void honestEmptyStates();
    void scopeRerunsTheQuery();
    void paginatesByBook();

private:
    template <typename Task>
    auto db(Task task) { return m_library->run(std::move(task)).result(); }

    std::unique_ptr<QTemporaryDir> m_dir;
    std::shared_ptr<Library> m_library;
};

void TestSearchController::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    auto opened = Library::open(m_dir->path());
    QVERIFY(opened);
    m_library = std::shared_ptr<Library>(std::move(opened.value()));
    db([](QSqlDatabase& d) {
        const BookId net = addBook(d, 1, QStringLiteral("Computer Networks"));
        TocEntry intro = tocEntry("e0", 0, "Introduction");
        intro.destinationState = DestinationState::Resolved;
        intro.destinationPage = 0;  // Page index 0.
        intro.printedLabel = QStringLiteral("i");
        TocEntry tcp = tocEntry("e1", 1, "TCP/IP Basics");
        tcp.sourceTocPage = 4;       // Unresolved, but listed on page index 4.
        TocEntry routing = tocEntry("e2", 2, "Routing Appendix");
        routing.destinationState = DestinationState::Ambiguous;
        routing.evidence.alternativePages = {7, 9};
        addContents(d, net, 1, {intro, tcp, routing});
        addBook(d, 2, QStringLiteral("Cooking at Home"));
        return true;
    });
}

void TestSearchController::cleanup()
{
    m_library.reset();
    m_dir.reset();
}

void TestSearchController::debouncedAndNewestWins()
{
    SearchController s;
    s.setLibrary(m_library);
    QAbstractItemModelTester tester(s.results());
    QSignalSpy applied(&s, &SearchController::responseApplied);
    // Typing: only the text standing when the pause ends is searched.
    s.setText(QStringLiteral("c"));
    s.setText(QStringLiteral("co"));
    s.setText(QStringLiteral("cooking"));
    QVERIFY(s.active());
    QVERIFY(!s.searching());  // Debounced: nothing runs while typing continues.
    QTest::qWait(SearchController::kDebounceMs / 3);
    QVERIFY(!s.searching());
    QTRY_COMPARE_WITH_TIMEOUT(applied.size(), 1, 5000);
    QTest::qWait(SearchController::kDebounceMs * 2);
    QCOMPARE(applied.size(), 1);
    QCOMPARE(s.results()->rowCount(), 1);
    QCOMPARE(titleAt(s, 0), QStringLiteral("Cooking at Home"));

    // Two requests in flight: only the newer one is applied.
    s.setText(QStringLiteral("networks"));
    s.refresh();
    s.setText(QStringLiteral("cooking home"));
    s.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(applied.size(), 2, 5000);
    QTest::qWait(SearchController::kDebounceMs * 2);
    QCOMPARE(applied.size(), 2);
    QCOMPARE(titleAt(s, 0), QStringLiteral("Cooking at Home"));
    QVERIFY(!s.searching());
}

void TestSearchController::clearDropsAResponseInFlight()
{
    SearchController s;
    s.setLibrary(m_library);
    QSignalSpy applied(&s, &SearchController::responseApplied);
    s.setText(QStringLiteral("networks"));
    s.refresh();
    s.clear();
    QTest::qWait(200);
    QCOMPARE(applied.size(), 0);
    QVERIFY(!s.active());
    QCOMPARE(s.results()->rowCount(), 0);
    QVERIFY(!s.searching());
    QCOMPARE(s.statusText(), QString());
}

void TestSearchController::chapterHitsNeverGuessAPage()
{
    SearchController s;
    s.setLibrary(m_library);
    QSignalSpy applied(&s, &SearchController::responseApplied);
    s.setText(QStringLiteral("introduction"));
    s.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(applied.size(), 1, 5000);
    QCOMPARE(chapter(s, 0, 0).value(QStringLiteral("pageText")).toString(), QStringLiteral("Page 1"));  // Index 0.
    QCOMPARE(chapter(s, 0, 0).value(QStringLiteral("page")).toInt(), 1);
    QCOMPARE(chapter(s, 0, 0).value(QStringLiteral("printedLabel")).toString(), QStringLiteral("i"));

    s.setText(QStringLiteral("TCP/IP"));
    s.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(applied.size(), 2, 5000);
    const QVariantMap tcp = chapter(s, 0, 0);
    QCOMPARE(tcp.value(QStringLiteral("pageText")).toString(), QStringLiteral("Page not found"));
    QCOMPARE(tcp.value(QStringLiteral("page")).toInt(), -1);  // Nothing to open...
    QCOMPARE(tcp.value(QStringLiteral("sourcePage")).toInt(), 5);  // ...but where it is listed.
    QCOMPARE(tcp.value(QStringLiteral("stateText")).toString(), QStringLiteral("Listed on page 5 of the PDF"));
    QCOMPARE(s.results()->data(s.results()->index(0), SearchResultsModel::MatchTextRole).toString(), QStringLiteral("Contents"));

    s.setText(QStringLiteral("appendix"));
    s.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(applied.size(), 3, 5000);
    QCOMPARE(chapter(s, 0, 0).value(QStringLiteral("pageText")).toString(), QStringLiteral("Page uncertain"));
    QCOMPARE(chapter(s, 0, 0).value(QStringLiteral("page")).toInt(), -1);
}

void TestSearchController::honestEmptyStates()
{
    SearchController s;
    s.setLibrary(m_library);
    QSignalSpy applied(&s, &SearchController::responseApplied);
    s.setText(QStringLiteral("quantum"));
    s.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(applied.size(), 1, 5000);
    QCOMPARE(s.results()->rowCount(), 0);
    QCOMPARE(s.statusText(), QStringLiteral("No matches in indexed titles and contents."));

    s.setText(QStringLiteral("- / :"));  // Only punctuation: not a query, not an error.
    s.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(applied.size(), 2, 5000);
    QCOMPARE(s.statusText(), QStringLiteral("Type letters or numbers to search titles, authors and contents."));

    s.setText(QStringLiteral("   "));  // Blank: back to the book list.
    QVERIFY(!s.active());
    QCOMPARE(s.statusText(), QString());
}

void TestSearchController::scopeRerunsTheQuery()
{
    SearchController s;
    s.setLibrary(m_library);
    QSignalSpy applied(&s, &SearchController::responseApplied);
    s.setText(QStringLiteral("routing"));
    s.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(applied.size(), 1, 5000);
    QCOMPARE(s.results()->rowCount(), 1);
    s.setScope(int(SearchScope::Titles));  // Runs at once.
    QTRY_COMPARE_WITH_TIMEOUT(applied.size(), 2, 5000);
    QCOMPARE(s.results()->rowCount(), 0);
    s.setScope(int(SearchScope::Contents));
    QTRY_COMPARE_WITH_TIMEOUT(applied.size(), 3, 5000);
    QCOMPARE(s.results()->rowCount(), 1);
}

void TestSearchController::paginatesByBook()
{
    constexpr int kBooks = SearchController::kPageSize + 10;
    db([](QSqlDatabase& d) {
        for (int i = 0; i < kBooks; ++i)
            addBook(d, 100 + i, QStringLiteral("Gardening Volume %1").arg(i));
        return true;
    });
    SearchController s;
    s.setLibrary(m_library);
    QSignalSpy applied(&s, &SearchController::responseApplied);
    s.setText(QStringLiteral("gardening"));
    s.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(applied.size(), 1, 5000);
    QCOMPARE(s.results()->rowCount(), SearchController::kPageSize);
    QVERIFY(s.canLoadMore());
    QCOMPARE(s.statusText(), QStringLiteral("60 book(s) found in titles, authors and contents."));
    s.loadMore();
    QTRY_COMPARE_WITH_TIMEOUT(applied.size(), 2, 5000);
    QCOMPARE(s.results()->rowCount(), kBooks);
    QVERIFY(!s.canLoadMore());
}

QTEST_GUILESS_MAIN(TestSearchController)
#include "tst_searchcontroller.moc"
