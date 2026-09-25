// A2 catalog and A3 projection behaviour on a real library folder, using
// synthetic records only.
#include "catalog/catalog.h"
#include "catalog/library.h"
#include "search/searchindex.h"

#include <QCryptographicHash>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace mbl::domain;
using mbl::catalog::Library;
namespace catalog = mbl::catalog;
namespace search = mbl::search;

namespace {

QString shaOf(const QString& seed)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Sha256).toHex());
}

NewBook newBook(const QString& fileName, std::optional<int> pages = 100)
{
    NewBook b;
    b.asset.id = AssetId::create();
    b.asset.sha256 = shaOf(fileName);
    b.asset.byteSize = 1234;
    b.asset.pageCount = pages;
    b.asset.managedPath = QStringLiteral("files/%1/source.pdf").arg(b.asset.id.toString());
    b.originalFileName = fileName;
    b.originalPath = QStringLiteral("C:/Downloads/") + fileName;
    return b;
}

RunIdentity runFor(const PublishTicket& t, const QString& outcome = QStringLiteral("complete"))
{
    return {t.sourceSha256, QStringLiteral("0.1.0"), QStringLiteral("paddle-test"), QStringLiteral("{}"),
            outcome, QStringLiteral("reports/x.json")};
}

ExtractedMetadata resolvedTitle(const QString& title)
{
    ExtractedMetadata m;
    m.titleStatus = FieldStatus::Resolved;
    m.title = title;
    return m;
}

TocEntry entry(const QString& id, int order, const QString& title, HierarchyState h = HierarchyState::Root,
               std::optional<QString> parent = std::nullopt)
{
    TocEntry e;
    e.sdkEntryId = id;
    e.order = order;
    e.title = title;
    e.hierarchy = h;
    e.parentSdkEntryId = parent;
    return e;
}

TocEntry resolved(TocEntry e, int page, const QString& label)
{
    e.destinationState = DestinationState::Resolved;
    e.destinationPage = page;
    e.printedLabel = label;
    e.inExportPlan = true;
    return e;
}

} // namespace

class TestCatalog : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void secondOpenIsLocked();
    void registerIndexesFileNameFallback();
    void duplicateAssetIsRejected();
    void metadataPublicationAndOverrides();
    void clearedDoesNotFallBackAfterRerun();
    void ambiguousIsNotAutoAccepted();
    void staleAndMismatchedResultsAreRejected();
    void tocKeepsEveryEntryAndHierarchyState();
    void chapterSearchNavigatesHonestly();
    void punctuationTermsStayDistinct();
    void rankingTiersAndChapterBound();
    void metadataSurvivesFailedTocPublication();
    void trashAndRestore();
    void rebuildMatchesCatalog();
    void recordsSurviveRestart();

private:
    template <typename Task>
    auto db(Task task) { return m_library->run(std::move(task)).result(); }
    BookId addBook(const QString& fileName, std::optional<int> pages = 100);
    SearchResponse find(const QString& text, SearchScope scope = SearchScope::All, int chapters = 5);
    void publishMetadata(const BookId& book, const ExtractedMetadata& m);
    RunId publishToc(const BookId& book, const QList<TocEntry>& entries);

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Library> m_library;
};

void TestCatalog::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    auto opened = Library::open(m_dir->path());
    QVERIFY2(opened, opened ? "" : qPrintable(opened.error().message));
    m_library = std::move(opened.value());
}

void TestCatalog::cleanup()
{
    m_library.reset();
    m_dir.reset();
}

BookId TestCatalog::addBook(const QString& fileName, std::optional<int> pages)
{
    const NewBook b = newBook(fileName, pages);
    auto id = db([b](QSqlDatabase& d) { return catalog::registerBook(d, b); });
    if (!id)
        qFatal("registerBook: %s", qPrintable(id.error().message));
    return id.value();
}

SearchResponse TestCatalog::find(const QString& text, SearchScope scope, int chapters)
{
    SearchRequest request;
    request.text = text;
    request.scope = scope;
    request.chapterHitsPerBook = chapters;
    auto response = db([request](QSqlDatabase& d) { return search::search(d, request); });
    if (!response)
        qFatal("search: %s", qPrintable(response.error().message));
    return response.value();
}

void TestCatalog::publishMetadata(const BookId& book, const ExtractedMetadata& m)
{
    auto result = db([book, m](QSqlDatabase& d) -> Result<RunId> {
        auto ticket = catalog::requestMetadataRun(d, book);
        if (!ticket)
            return ticket.error();
        return catalog::publishMetadata(d, ticket.value(), runFor(ticket.value()), m);
    });
    if (!result)
        qFatal("publishMetadata: %s", qPrintable(result.error().message));
}

RunId TestCatalog::publishToc(const BookId& book, const QList<TocEntry>& entries)
{
    auto result = db([book, entries](QSqlDatabase& d) -> Result<RunId> {
        auto ticket = catalog::requestTocRun(d, book);
        if (!ticket)
            return ticket.error();
        TocAnalysis toc{QStringLiteral("analysis_partial"), false, entries};
        return catalog::publishToc(d, ticket.value(), runFor(ticket.value()), toc);
    });
    if (!result)
        qFatal("publishToc: %s", qPrintable(result.error().message));
    return result.value();
}

void TestCatalog::secondOpenIsLocked()
{
    auto second = Library::open(m_dir->path());
    QVERIFY(!second);
    QCOMPARE(second.error().code, ErrorCode::LibraryLocked);

    m_library.reset();
    auto third = Library::open(m_dir->path());
    QVERIFY2(third, third ? "" : qPrintable(third.error().message));
}

void TestCatalog::registerIndexesFileNameFallback()
{
    const BookId id = addBook(QStringLiteral("Unknown_scan_0042.pdf"));
    auto books = db([](QSqlDatabase& d) { return catalog::listBooks(d, Lifecycle::Active); });
    QVERIFY(books);
    QCOMPARE(books.value().size(), 1);
    QCOMPARE(books.value().first().id, id);
    QCOMPARE(books.value().first().displayTitle, QStringLiteral("Unknown_scan_0042"));
    QVERIFY(books.value().first().displayTitleFromFileName);
    QVERIFY(!books.value().first().metadata.title);  // Not extracted metadata.

    const auto hits = find(QStringLiteral("scan"));
    QCOMPARE(hits.books.size(), 1);
    QCOMPARE(hits.books.first().book, id);
}

void TestCatalog::duplicateAssetIsRejected()
{
    addBook(QStringLiteral("a.pdf"));
    const NewBook again = newBook(QStringLiteral("a.pdf"));
    auto result = db([again](QSqlDatabase& d) { return catalog::registerBook(d, again); });
    QVERIFY(!result);
    auto found = db([sha = again.asset.sha256](QSqlDatabase& d) { return catalog::findBookBySha256(d, sha); });
    QVERIFY(found && found.value().has_value());
}

void TestCatalog::metadataPublicationAndOverrides()
{
    const BookId id = addBook(QStringLiteral("x.pdf"));
    ExtractedMetadata m = resolvedTitle(QStringLiteral("Practical Networking"));
    m.contributorsStatus = FieldStatus::Resolved;
    m.contributors = {{QStringLiteral("Alice Author"), ContributorRole::Author},
                      {QStringLiteral("Bob Editor"), ContributorRole::Editor}};
    m.publicationYearStatus = FieldStatus::Resolved;
    m.publicationYear = 2020;
    m.copyrightYearStatus = FieldStatus::Resolved;
    m.copyrightYear = 2019;
    publishMetadata(id, m);

    auto details = db([id](QSqlDatabase& d) { return catalog::bookDetails(d, id); });
    QVERIFY(details);
    const EffectiveMetadata& e = details.value().summary.metadata;
    QCOMPARE(*e.title, QStringLiteral("Practical Networking"));
    QCOMPARE(e.titleSource, ValueSource::Extracted);
    QCOMPARE(e.contributors, m.contributors);  // Order and roles preserved.
    QCOMPARE(*e.publicationYear, 2020);
    QCOMPARE(*e.copyrightYear, 2019);          // Kept separate from publication year.
    QCOMPARE(find(QStringLiteral("alice"), SearchScope::Contributors).books.size(), 1);
    QCOMPARE(find(QStringLiteral("alice"), SearchScope::Titles).books.size(), 0);

    // Value override replaces the title everywhere, including search.
    QVERIFY(db([id](QSqlDatabase& d) {
        return catalog::setOverride(d, id, MetadataField::Title,
                                    MetadataOverride::withText(QStringLiteral("Networking in Practice")));
    }));
    QCOMPARE(find(QStringLiteral("practical"), SearchScope::Titles).books.size(), 0);
    QCOMPARE(find(QStringLiteral("practice"), SearchScope::Titles).books.size(), 1);

    // Cleared publication year does not fall back to the extracted 2020.
    QVERIFY(db([id](QSqlDatabase& d) {
        return catalog::setOverride(d, id, MetadataField::PublicationYear, MetadataOverride::cleared());
    }));
    // Contributors override with a new order.
    const QList<Contributor> mine = {{QStringLiteral("Carol"), ContributorRole::Translator}};
    QVERIFY(db([id, mine](QSqlDatabase& d) {
        return catalog::setOverride(d, id, MetadataField::Contributors, MetadataOverride::withContributors(mine));
    }));
    details = db([id](QSqlDatabase& d) { return catalog::bookDetails(d, id); });
    const EffectiveMetadata& e2 = details.value().summary.metadata;
    QCOMPARE(*e2.title, QStringLiteral("Networking in Practice"));
    QCOMPARE(e2.titleSource, ValueSource::User);
    QVERIFY(!e2.publicationYear);
    QCOMPARE(e2.publicationYearSource, ValueSource::Cleared);
    QCOMPARE(e2.contributors, mine);
    QCOMPARE(details.value().extracted->publicationYear, 2020);  // Extraction itself is kept.
    QCOMPARE(find(QStringLiteral("alice"), SearchScope::Contributors).books.size(), 0);
    QCOMPARE(find(QStringLiteral("carol"), SearchScope::Contributors).books.size(), 1);

    // Back to Auto restores the extracted title.
    QVERIFY(db([id](QSqlDatabase& d) {
        return catalog::setOverride(d, id, MetadataField::Title, MetadataOverride::automatic());
    }));
    details = db([id](QSqlDatabase& d) { return catalog::bookDetails(d, id); });
    QCOMPARE(*details.value().summary.metadata.title, QStringLiteral("Practical Networking"));

    // Invalid values are refused.
    auto empty = db([id](QSqlDatabase& d) {
        return catalog::setOverride(d, id, MetadataField::Title, MetadataOverride::withText(QStringLiteral("  ")));
    });
    QVERIFY(!empty);
    QCOMPARE(empty.error().code, ErrorCode::InvalidArgument);
}

void TestCatalog::clearedDoesNotFallBackAfterRerun()
{
    const BookId id = addBook(QStringLiteral("rerun.pdf"));
    publishMetadata(id, resolvedTitle(QStringLiteral("First Title")));
    QVERIFY(db([id](QSqlDatabase& d) {
        return catalog::setOverride(d, id, MetadataField::Title, MetadataOverride::cleared());
    }));
    QVERIFY(db([id](QSqlDatabase& d) {
        return catalog::setOverride(d, id, MetadataField::Subtitle,
                                    MetadataOverride::withText(QStringLiteral("My Subtitle")));
    }));
    publishMetadata(id, resolvedTitle(QStringLiteral("Second Title")));  // Reanalysis.

    auto details = db([id](QSqlDatabase& d) { return catalog::bookDetails(d, id); });
    QVERIFY(details);
    QVERIFY(!details.value().summary.metadata.title);
    QCOMPARE(details.value().summary.metadata.titleSource, ValueSource::Cleared);
    QCOMPARE(*details.value().summary.metadata.subtitle, QStringLiteral("My Subtitle"));
    QCOMPARE(*details.value().extracted->title, QStringLiteral("Second Title"));
    QCOMPARE(details.value().summary.displayTitle, QStringLiteral("rerun"));  // File-name fallback.
    QCOMPARE(find(QStringLiteral("second"), SearchScope::Titles).books.size(), 0);
}

void TestCatalog::ambiguousIsNotAutoAccepted()
{
    const BookId id = addBook(QStringLiteral("amb.pdf"));
    ExtractedMetadata m;
    m.titleStatus = FieldStatus::Ambiguous;
    m.title = QStringLiteral("Maybe This");  // Must not be stored as a value.
    publishMetadata(id, m);
    auto details = db([id](QSqlDatabase& d) { return catalog::bookDetails(d, id); });
    QCOMPARE(details.value().extracted->titleStatus, FieldStatus::Ambiguous);
    QVERIFY(!details.value().extracted->title);
    QVERIFY(!details.value().summary.metadata.title);
    QVERIFY(details.value().summary.displayTitleFromFileName);
}

void TestCatalog::staleAndMismatchedResultsAreRejected()
{
    const BookId id = addBook(QStringLiteral("stale.pdf"));
    auto tickets = db([id](QSqlDatabase& d) {
        // Sequenced: function-argument evaluation order is unspecified.
        const PublishTicket first = catalog::requestMetadataRun(d, id).value();
        const PublishTicket second = catalog::requestMetadataRun(d, id).value();
        return qMakePair(first, second);
    });
    const PublishTicket older = tickets.first;
    const PublishTicket newer = tickets.second;
    QVERIFY(newer.generation > older.generation);

    auto stale = db([older](QSqlDatabase& d) {
        return catalog::publishMetadata(d, older, runFor(older), resolvedTitle(QStringLiteral("Old")));
    });
    QVERIFY(!stale);
    QCOMPARE(stale.error().code, ErrorCode::StaleGeneration);

    PublishTicket wrongSource = newer;
    wrongSource.sourceSha256 = shaOf(QStringLiteral("other bytes"));
    auto mismatch = db([wrongSource](QSqlDatabase& d) {
        return catalog::publishMetadata(d, wrongSource, runFor(wrongSource), resolvedTitle(QStringLiteral("X")));
    });
    QVERIFY(!mismatch);
    QCOMPARE(mismatch.error().code, ErrorCode::SourceMismatch);

    auto current = db([newer](QSqlDatabase& d) {
        return catalog::publishMetadata(d, newer, runFor(newer), resolvedTitle(QStringLiteral("New")));
    });
    QVERIFY(current);
    QCOMPARE(find(QStringLiteral("old"), SearchScope::Titles).books.size(), 0);
    QCOMPARE(find(QStringLiteral("new"), SearchScope::Titles).books.size(), 1);

    // A TOC request does not invalidate metadata tickets, and vice versa.
    auto both = db([id](QSqlDatabase& d) {
        auto meta = catalog::requestMetadataRun(d, id).value();
        auto toc = catalog::requestTocRun(d, id).value();
        auto m = catalog::publishMetadata(d, meta, runFor(meta), resolvedTitle(QStringLiteral("Meta")));
        auto t = catalog::publishToc(d, toc, runFor(toc), TocAnalysis{QStringLiteral("x"), false, {}});
        return m.ok() && t.ok();
    });
    QVERIFY(both);
}

void TestCatalog::tocKeepsEveryEntryAndHierarchyState()
{
    const BookId id = addBook(QStringLiteral("toc.pdf"));
    QList<TocEntry> entries = {
        resolved(entry(QStringLiteral("e1"), 0, QStringLiteral("Introduction")), 0, QStringLiteral("1")),
        // Child listed before its parent.
        entry(QStringLiteral("e2"), 1, QStringLiteral("TCP/IP Basics"), HierarchyState::KnownParent, QStringLiteral("e3")),
        entry(QStringLiteral("e3"), 2, QStringLiteral("Networking Fundamentals")),
        entry(QStringLiteral("e4"), 3, QStringLiteral("Orphan"), HierarchyState::KnownParent, QStringLiteral("ghost")),
        entry(QStringLiteral("e6"), 5, QStringLiteral("Cycle A"), HierarchyState::KnownParent, QStringLiteral("e7")),
        entry(QStringLiteral("e7"), 6, QStringLiteral("Cycle B"), HierarchyState::KnownParent, QStringLiteral("e6")),
    };
    entries[1].sourceTocPage = 4;
    entries[2].destinationState = DestinationState::Ambiguous;
    publishToc(id, entries);

    auto details = db([id](QSqlDatabase& d) { return catalog::bookDetails(d, id); });
    QVERIFY(details);
    const QList<TocEntry>& stored = details.value().toc->entries;
    QCOMPARE(stored.size(), entries.size());  // Nothing dropped.
    QCOMPARE(details.value().summary.tocEntryCount, int(entries.size()));
    QCOMPARE(stored[0].destinationPage, 0);    // Page zero is a valid destination.
    QCOMPARE(*stored[0].printedLabel, QStringLiteral("1"));
    QCOMPARE(stored[1].hierarchy, HierarchyState::KnownParent);
    QCOMPARE(*stored[1].parentSdkEntryId, QStringLiteral("e3"));
    QCOMPARE(stored[2].destinationState, DestinationState::Ambiguous);
    QVERIFY(!stored[2].destinationPage);
    QCOMPARE(stored[3].hierarchy, HierarchyState::Unknown);  // Missing parent: not invented.
    QVERIFY(!stored[3].parentSdkEntryId);
    QCOMPARE(stored[4].hierarchy, HierarchyState::Unknown);  // Cycle broken...
    QCOMPARE(stored[5].hierarchy, HierarchyState::KnownParent);  // ...once.
    QCOMPARE(*stored[5].parentSdkEntryId, QStringLiteral("e6"));

    // A resolved destination beyond the document is refused.
    auto beyond = db([id](QSqlDatabase& d) {
        auto t = catalog::requestTocRun(d, id).value();
        TocAnalysis toc{QStringLiteral("x"), false,
                        {resolved(entry(QStringLiteral("z"), 0, QStringLiteral("Z")), 100, QStringLiteral("9"))}};
        return catalog::publishToc(d, t, runFor(t), toc);
    });
    QVERIFY(!beyond);
    QCOMPARE(beyond.error().code, ErrorCode::InvalidArgument);
}

void TestCatalog::chapterSearchNavigatesHonestly()
{
    const BookId id = addBook(QStringLiteral("net.pdf"));
    TocEntry unresolvedEntry = entry(QStringLiteral("e2"), 1, QStringLiteral("TCP/IP Basics"));
    unresolvedEntry.sourceTocPage = 4;
    TocEntry omitted = entry(QStringLiteral("e3"), 2, QStringLiteral("Routing Tables"));
    publishToc(id, {resolved(entry(QStringLiteral("e1"), 0, QStringLiteral("Introduction")), 0, QStringLiteral("i")),
                    unresolvedEntry, omitted});

    auto intro = find(QStringLiteral("introduction"));
    QCOMPARE(intro.books.size(), 1);
    QCOMPARE(intro.books[0].tier, 1);
    const ChapterHit& i = intro.books[0].chapters.at(0);
    QCOMPARE(i.destinationState, DestinationState::Resolved);
    QCOMPARE(i.destinationPage, 0);
    QCOMPARE(*i.printedLabel, QStringLiteral("i"));  // Label kept apart from the page index.

    auto tcp = find(QStringLiteral("TCP/IP"));
    QCOMPARE(tcp.books.size(), 1);
    const ChapterHit& t = tcp.books[0].chapters.at(0);
    QCOMPARE(t.destinationState, DestinationState::Unresolved);
    QVERIFY(!t.destinationPage);  // No guessed page...
    QCOMPARE(t.sourceTocPage, 4); // ...but the source TOC page is offered.

    auto routing = find(QStringLiteral("routing"), SearchScope::Contents);
    QCOMPARE(routing.books.size(), 1);  // Omitted from the plan, still searchable.
    QVERIFY(!routing.books[0].chapters.at(0).inExportPlan);
    QCOMPARE(find(QStringLiteral("routing"), SearchScope::Titles).books.size(), 0);
}

void TestCatalog::punctuationTermsStayDistinct()
{
    const BookId cpp = addBook(QStringLiteral("cpp.pdf"));
    const BookId c = addBook(QStringLiteral("c.pdf"));
    const BookId cs = addBook(QStringLiteral("cs.pdf"));
    const BookId http = addBook(QStringLiteral("http.pdf"));
    publishMetadata(cpp, resolvedTitle(QStringLiteral("The C++ Programming Language")));
    publishMetadata(c, resolvedTitle(QStringLiteral("The C Programming Language")));
    publishMetadata(cs, resolvedTitle(QStringLiteral("C# in Depth")));
    publishMetadata(http, resolvedTitle(QStringLiteral("HTTP/2 in Action — Ünïcode Édition")));

    auto r = find(QStringLiteral("C++"));
    QCOMPARE(r.books.size(), 1);
    QCOMPARE(r.books[0].book, cpp);
    r = find(QStringLiteral("c#"));
    QCOMPARE(r.books.size(), 1);
    QCOMPARE(r.books[0].book, cs);
    r = find(QStringLiteral("\"C Programming\""));
    QCOMPARE(r.books.size(), 1);
    QCOMPARE(r.books[0].book, c);
    r = find(QStringLiteral("http/2"));
    QCOMPARE(r.books.size(), 1);
    QCOMPARE(find(QStringLiteral("unicode edition")).books.size(), 1);  // Diacritics folded.
    QCOMPARE(find(QStringLiteral("progr*")).books.size(), 2);
    QVERIFY(find(QStringLiteral("NOT AND (")).books.isEmpty());      // No syntax error.
    QVERIFY(find(QStringLiteral(" - ")).queryEmpty);
}

void TestCatalog::rankingTiersAndChapterBound()
{
    const BookId titled = addBook(QStringLiteral("a.pdf"));
    const BookId contents = addBook(QStringLiteral("b.pdf"));
    publishMetadata(titled, resolvedTitle(QStringLiteral("Networking Basics")));
    publishMetadata(contents, resolvedTitle(QStringLiteral("Cooking for Engineers")));
    QList<TocEntry> many;
    for (int i = 0; i < 12; ++i)
        many << entry(QStringLiteral("n%1").arg(i), i, QStringLiteral("Networking dinner %1").arg(i));
    publishToc(contents, many);

    auto r = find(QStringLiteral("networking"), SearchScope::All, 3);
    QCOMPARE(r.books.size(), 2);
    QCOMPARE(r.books[0].book, titled);  // Title match outranks a long TOC.
    QCOMPARE(r.books[0].tier, 0);
    QCOMPARE(r.books[1].book, contents);
    QCOMPARE(r.books[1].tier, 1);
    QCOMPARE(r.books[1].chapters.size(), 3);
    QCOMPARE(r.books[1].totalChapterMatches, 12);
    QCOMPARE(r.books[1].displayTitle, QStringLiteral("Cooking for Engineers"));

    SearchRequest paged;
    paged.text = QStringLiteral("networking");
    paged.offset = 1;
    paged.limit = 1;
    paged.generation = 42;
    auto page = db([paged](QSqlDatabase& d) { return search::search(d, paged); }).value();
    QCOMPARE(page.generation, quint64(42));
    QCOMPARE(page.totalBooks, 2);
    QCOMPARE(page.books.size(), 1);
    QCOMPARE(page.books[0].book, contents);
}

void TestCatalog::metadataSurvivesFailedTocPublication()
{
    const BookId id = addBook(QStringLiteral("fail.pdf"));
    publishMetadata(id, resolvedTitle(QStringLiteral("Distributed Systems")));
    publishToc(id, {entry(QStringLiteral("a"), 0, QStringLiteral("Consensus"))});

    // Duplicate entry IDs violate a constraint mid-transaction.
    auto failed = db([id](QSqlDatabase& d) {
        auto t = catalog::requestTocRun(d, id).value();
        TocAnalysis toc{QStringLiteral("x"), false,
                        {entry(QStringLiteral("d"), 0, QStringLiteral("Replication")),
                         entry(QStringLiteral("d"), 1, QStringLiteral("Duplicate"))}};
        return catalog::publishToc(d, t, runFor(t), toc);
    });
    QVERIFY(!failed);
    QCOMPARE(find(QStringLiteral("replication")).books.size(), 0);  // No stale index rows.
    QCOMPARE(find(QStringLiteral("consensus")).books.size(), 1);    // Old TOC still active.
    QCOMPARE(find(QStringLiteral("distributed"), SearchScope::Titles).books.size(), 1);
    const int runs = db([](QSqlDatabase& d) {
        QSqlQuery q(d);
        q.exec(QStringLiteral("SELECT count(*) FROM toc_runs"));
        q.next();
        return q.value(0).toInt();
    });
    QCOMPARE(runs, 1);
}

void TestCatalog::trashAndRestore()
{
    const BookId id = addBook(QStringLiteral("trash.pdf"));
    publishMetadata(id, resolvedTitle(QStringLiteral("Operating Systems")));
    const PublishTicket pending = db([id](QSqlDatabase& d) { return catalog::requestTocRun(d, id).value(); });

    QVERIFY(db([id](QSqlDatabase& d) { return catalog::trashBook(d, id); }));
    QVERIFY(db([id](QSqlDatabase& d) { return catalog::trashBook(d, id); }));  // Idempotent.
    QCOMPARE(find(QStringLiteral("operating")).books.size(), 0);
    QCOMPARE(db([](QSqlDatabase& d) { return catalog::listBooks(d, Lifecycle::Active); }).value().size(), 0);
    QCOMPARE(db([](QSqlDatabase& d) { return catalog::listBooks(d, Lifecycle::Trashed); }).value().size(), 1);

    // A late completion cannot publish into a trashed book...
    auto late = db([pending](QSqlDatabase& d) {
        return catalog::publishToc(d, pending, runFor(pending),
                                   TocAnalysis{QStringLiteral("x"), false, {entry(QStringLiteral("l"), 0, QStringLiteral("Late"))}});
    });
    QCOMPARE(late.error().code, ErrorCode::Trashed);
    auto request = db([id](QSqlDatabase& d) { return catalog::requestMetadataRun(d, id); });
    QCOMPARE(request.error().code, ErrorCode::Trashed);

    QVERIFY(db([id](QSqlDatabase& d) { return catalog::restoreBook(d, id); }));
    QCOMPARE(find(QStringLiteral("operating")).books.size(), 1);
    // ...nor after restore, because trashing invalidated its generation.
    late = db([pending](QSqlDatabase& d) {
        return catalog::publishToc(d, pending, runFor(pending),
                                   TocAnalysis{QStringLiteral("x"), false, {entry(QStringLiteral("l"), 0, QStringLiteral("Late"))}});
    });
    QCOMPARE(late.error().code, ErrorCode::StaleGeneration);
    QCOMPARE(find(QStringLiteral("late")).books.size(), 0);
}

void TestCatalog::rebuildMatchesCatalog()
{
    const BookId a = addBook(QStringLiteral("r1.pdf"));
    const BookId b = addBook(QStringLiteral("r2.pdf"));
    publishMetadata(a, resolvedTitle(QStringLiteral("Compilers")));
    publishToc(b, {entry(QStringLiteral("x"), 0, QStringLiteral("Parsing Compilers"))});
    QVERIFY(db([b](QSqlDatabase& d) { return catalog::trashBook(d, b); }));
    const auto before = find(QStringLiteral("compilers"));
    QCOMPARE(before.books.size(), 1);

    // Damage the derived index, then rebuild it without any SDK work.
    db([](QSqlDatabase& d) {
        QSqlQuery q(d);
        q.exec(QStringLiteral("DELETE FROM search_books"));
        q.exec(QStringLiteral("INSERT INTO search_toc(book_id, entry_key, title) VALUES ('bogus', 1, 'compilers')"));
        return true;
    });
    QVERIFY(find(QStringLiteral("compilers")).books.size() != 1 || find(QStringLiteral("compilers")).books[0].book != a);

    QVERIFY(db([](QSqlDatabase& d) { return catalog::rebuildSearchIndex(d); }));
    const auto after = find(QStringLiteral("compilers"));
    QCOMPARE(after.books.size(), 1);
    QCOMPARE(after.books[0].book, a);
    QCOMPARE(after.books[0].tier, before.books[0].tier);
}

void TestCatalog::recordsSurviveRestart()
{
    const BookId id = addBook(QStringLiteral("Δίκτυα και ü.pdf"), 0);
    ExtractedMetadata m = resolvedTitle(QStringLiteral("Δίκτυα Υπολογιστών"));
    m.editionStatus = FieldStatus::Resolved;
    m.editionStatement = QStringLiteral("Third Edition");
    m.editionOrdinal = 3;
    publishMetadata(id, m);
    QVERIFY(db([id](QSqlDatabase& d) {
        return catalog::setOverride(d, id, MetadataField::CopyrightYear, MetadataOverride::withYear(1999));
    }));
    publishToc(id, {entry(QStringLiteral("k"), 0, QStringLiteral("Πρωτόκολλα"))});
    auto before = db([id](QSqlDatabase& d) { return catalog::bookDetails(d, id); }).value();

    m_library.reset();  // Restart.
    auto reopened = Library::open(m_dir->path());
    QVERIFY(reopened);
    m_library = std::move(reopened.value());

    auto after = db([id](QSqlDatabase& d) { return catalog::bookDetails(d, id); });
    QVERIFY(after);
    QCOMPARE(after.value().summary.displayTitle, QStringLiteral("Δίκτυα Υπολογιστών"));
    QCOMPARE(*after.value().summary.metadata.edition, QStringLiteral("Third Edition"));
    QCOMPARE(*after.value().extracted->editionOrdinal, 3);
    QCOMPARE(*after.value().summary.metadata.copyrightYear, 1999);
    QCOMPARE(after.value().summary.metadata.copyrightYearSource, ValueSource::User);
    QCOMPARE(after.value().asset.pageCount, 0);
    QCOMPARE(after.value().originalFileName, QStringLiteral("Δίκτυα και ü.pdf"));
    QCOMPARE(after.value().summary.revision, before.summary.revision);
    QCOMPARE(after.value().metadataGeneration, before.metadataGeneration);
    QCOMPARE(find(QStringLiteral("πρωτόκολλα")).books.size(), 1);
    QCOMPARE(find(QStringLiteral("δίκτυα"), SearchScope::Titles).books.size(), 1);
}

QTEST_GUILESS_MAIN(TestCatalog)
#include "tst_catalog.moc"
