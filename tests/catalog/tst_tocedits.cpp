// A2 contents edits on a real library folder, with synthetic runs: edits are
// saved as revisions based on one run, refused when stale, followed by
// search, kept across restarts and reruns, and reconciled only by the user.
#include "catalog/catalog.h"
#include "catalog/library.h"
#include "catalog/tocedits.h"
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

TocEntry entry(const QString& id, int order, const QString& title, std::optional<int> page = std::nullopt,
               std::optional<QString> parent = std::nullopt)
{
    TocEntry e;
    e.sdkEntryId = id;
    e.order = order;
    e.title = title;
    e.hierarchy = parent ? HierarchyState::KnownParent : HierarchyState::Root;
    e.parentSdkEntryId = parent;
    if (page) {
        e.destinationState = DestinationState::Resolved;
        e.destinationPage = page;
        e.inExportPlan = true;
    }
    e.sourceTocPage = 2;
    return e;
}

// A book's contents as the analysis finds them. `prefix` changes only the
// SDK entry IDs, as a rerun does.
QList<TocEntry> analyzed(const QString& prefix)
{
    return {entry(prefix + QStringLiteral("1"), 0, QStringLiteral("1 Introduction"), 4),
            entry(prefix + QStringLiteral("2"), 1, QStringLiteral("1.1 Getting started"), 6, prefix + QStringLiteral("1")),
            entry(prefix + QStringLiteral("3"), 2, QStringLiteral("2 Netwrking wth TCP"), std::nullopt),  // OCR noise.
            entry(prefix + QStringLiteral("4"), 3, QStringLiteral("Index"), 40)};
}

TocEdit rename(const QString& key, const QString& title)
{
    TocEdit e;
    e.kind = TocEdit::Kind::Rename;
    e.entryKey = key;
    e.title = title;
    return e;
}

TocEdit setPage(const QString& key, int page)
{
    TocEdit e;
    e.kind = TocEdit::Kind::SetPage;
    e.entryKey = key;
    e.page = page;
    return e;
}

TocEdit simple(TocEdit::Kind kind, const QString& key, const QString& parent = {})
{
    TocEdit e;
    e.kind = kind;
    e.entryKey = key;
    e.parentKey = parent;
    return e;
}

const TocEntry* find(const TocAnalysis& toc, const QString& title)
{
    for (const TocEntry& e : toc.entries) {
        if (e.title == title)
            return &e;
    }
    return nullptr;
}

} // namespace

class TestTocEdits : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void editsAreRevisionsFollowedBySearchAndKept();
    void staleOrInvalidEditsChangeNothing();
    void removeAndRestoreFollowTheHierarchy();
    void addedEntriesAreHiddenOnlyUnderARemovedParent();
    void editsOutliveAttemptsToDeleteTheirRun();
    void identicalRerunCarriesTheEdits();
    void changedRerunWaitsForTheUser();
    void editsDuringAnalysisSurviveAndStaleRunsCannotReplaceThem();
    void trashedBooksAreNotEdited();
    void rebuildMatchesTheEditedContents();

private:
    template <typename Task>
    auto db(Task task) { return m_library->run(std::move(task)).result(); }
    BookId addBook(const QString& fileName, int pages = 50);
    RunId publish(const BookId& book, const QList<TocEntry>& entries);
    BookDetails details(const BookId& book);
    TocEditBase baseOf(const BookId& book);
    Result<TocRevisionInfo> edit(const BookId& book, const QList<TocEdit>& edits);
    QStringList chapterHits(const QString& text);

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Library> m_library;
};

void TestTocEdits::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    auto opened = Library::open(m_dir->path());
    QVERIFY2(opened, opened ? "" : qPrintable(opened.error().message));
    m_library = std::move(opened.value());
}

void TestTocEdits::cleanup()
{
    m_library.reset();
    m_dir.reset();
}

BookId TestTocEdits::addBook(const QString& fileName, int pages)
{
    NewBook b;
    b.asset.id = AssetId::create();
    b.asset.sha256 = QString::fromLatin1(QCryptographicHash::hash(fileName.toUtf8(), QCryptographicHash::Sha256).toHex());
    b.asset.byteSize = 1;
    b.asset.pageCount = pages;
    b.asset.managedPath = QStringLiteral("files/%1/source.pdf").arg(b.asset.id.toString());
    b.originalFileName = fileName;
    b.originalPath = fileName;
    auto id = db([b](QSqlDatabase& d) { return catalog::registerBook(d, b); });
    if (!id)
        qFatal("registerBook: %s", qPrintable(id.error().message));
    return id.value();
}

RunId TestTocEdits::publish(const BookId& book, const QList<TocEntry>& entries)
{
    auto result = db([book, entries](QSqlDatabase& d) -> Result<RunId> {
        auto ticket = catalog::requestTocRun(d, book);
        if (!ticket)
            return ticket.error();
        TocAnalysis toc{QStringLiteral("plan_ready"), true, entries};
        const RunIdentity run{ticket.value().sourceSha256, QStringLiteral("test"), QString(), QStringLiteral("{}"),
                              toc.outcome, std::nullopt};
        return catalog::publishToc(d, ticket.value(), run, toc);
    });
    if (!result)
        qFatal("publishToc: %s", qPrintable(result.error().message));
    return result.value();
}

BookDetails TestTocEdits::details(const BookId& book)
{
    auto d = db([book](QSqlDatabase& s) { return catalog::bookDetails(s, book); });
    if (!d)
        qFatal("bookDetails: %s", qPrintable(d.error().message));
    return d.value();
}

TocEditBase TestTocEdits::baseOf(const BookId& book)
{
    const BookDetails d = details(book);
    return TocEditBase{d.tocRun, d.tocRevision ? std::optional<TocRevisionId>(d.tocRevision->id) : std::nullopt};
}

Result<TocRevisionInfo> TestTocEdits::edit(const BookId& book, const QList<TocEdit>& edits)
{
    const TocEditBase base = baseOf(book);
    return db([book, base, edits](QSqlDatabase& d) { return catalog::editToc(d, book, base, edits); });
}

QStringList TestTocEdits::chapterHits(const QString& text)
{
    SearchRequest request;
    request.text = text;
    request.scope = SearchScope::Contents;
    request.chapterHitsPerBook = 20;
    auto response = db([request](QSqlDatabase& d) { return search::search(d, request); });
    if (!response)
        qFatal("search: %s", qPrintable(response.error().message));
    QStringList titles;
    for (const BookHit& b : response.value().books) {
        for (const ChapterHit& c : b.chapters)
            titles << c.title;
    }
    titles.sort();
    return titles;
}

void TestTocEdits::editsAreRevisionsFollowedBySearchAndKept()
{
    const BookId book = addBook(QStringLiteral("networks.pdf"));
    const RunId run = publish(book, analyzed(QStringLiteral("a")));
    QVERIFY(!details(book).tocRevision);
    QCOMPARE(chapterHits(QStringLiteral("netwrking")), QStringList{QStringLiteral("2 Netwrking wth TCP")});

    // Fix the OCR noise and give the entry its page; page index 0 is valid.
    auto first = edit(book, {rename(QStringLiteral("a3"), QStringLiteral(" 2 Networking with TCP ")),
                             setPage(QStringLiteral("a3"), 0)});
    QVERIFY2(first, first ? "" : qPrintable(first.error().message));
    QCOMPARE(first.value().number, 1);
    QCOMPARE(first.value().baseRun, run);
    QVERIFY(!first.value().needsReconciliation);

    BookDetails d = details(book);
    QVERIFY(d.tocRevision);
    QVERIFY(d.summary.tocEdited);
    QVERIFY(!d.summary.tocNeedsReconciliation);
    const TocEntry* fixed = find(*d.toc, QStringLiteral("2 Networking with TCP"));
    QVERIFY(fixed);
    QCOMPARE(fixed->sdkEntryId, QStringLiteral("a3"));  // The key of an analyzed entry is its first run's ID.
    QCOMPARE(fixed->destinationState, DestinationState::Resolved);
    QCOMPARE(fixed->destinationPage, std::optional<int>(0));
    QCOMPARE(fixed->edits, (QStringList{QStringLiteral("title"), QStringLiteral("page")}));
    QCOMPARE(fixed->sourceTocPage, std::optional<int>(2));  // Evidence stays.
    QVERIFY(find(*d.toc, QStringLiteral("1 Introduction"))->edits.isEmpty());
    // The analysis itself is unchanged and still available.
    QVERIFY(d.analyzedToc);
    QVERIFY(find(*d.analyzedToc, QStringLiteral("2 Netwrking wth TCP")));
    QCOMPARE(d.tocRun, std::optional<RunId>(run));

    QCOMPARE(chapterHits(QStringLiteral("networking")), QStringList{QStringLiteral("2 Networking with TCP")});
    QVERIFY(chapterHits(QStringLiteral("netwrking")).isEmpty());

    // A second edit on top: a new revision; the first is kept.
    auto second = edit(book, {simple(TocEdit::Kind::SetParent, QStringLiteral("a3"), QStringLiteral("a1")),
                              simple(TocEdit::Kind::ClearPage, QStringLiteral("a4"))});
    QVERIFY2(second, second ? "" : qPrintable(second.error().message));
    QCOMPARE(second.value().number, 2);
    auto history = db([book](QSqlDatabase& s) { return catalog::tocRevisions(s, book); });
    QVERIFY(history);
    QCOMPARE(history.value().size(), 2);
    QCOMPARE(history.value().at(0).number, 2);

    // Add an entry after "1 Introduction" and its sub-entries, as its sibling.
    TocEdit add;
    add.kind = TocEdit::Kind::Add;
    add.entryKey = QStringLiteral("a1");
    add.title = QStringLiteral("Preface");
    add.page = 2;
    QVERIFY(edit(book, {add}));

    // After a restart.
    m_library.reset();
    auto reopened = Library::open(m_dir->path());
    QVERIFY(reopened);
    m_library = std::move(reopened.value());
    d = details(book);
    QCOMPARE(d.tocRevision->number, 3);
    QStringList titles;
    for (const TocEntry& e : d.toc->entries)
        titles << e.title;
    // "2 Networking with TCP" now sits under "1 Introduction"; "Preface" follows that subtree.
    QCOMPARE(titles, (QStringList{QStringLiteral("1 Introduction"), QStringLiteral("1.1 Getting started"),
                                  QStringLiteral("2 Networking with TCP"), QStringLiteral("Preface"),
                                  QStringLiteral("Index")}));
    const TocEntry* moved = find(*d.toc, QStringLiteral("2 Networking with TCP"));
    QCOMPARE(moved->hierarchy, HierarchyState::KnownParent);
    QCOMPARE(moved->parentSdkEntryId, std::optional<QString>(QStringLiteral("a1")));
    QCOMPARE(find(*d.toc, QStringLiteral("Index"))->destinationState, DestinationState::Unresolved);
    const TocEntry* preface = find(*d.toc, QStringLiteral("Preface"));
    QCOMPARE(preface->hierarchy, HierarchyState::Root);
    QCOMPARE(preface->edits, QStringList{QStringLiteral("added")});
    QCOMPARE(preface->destinationPage, std::optional<int>(2));
    QCOMPARE(chapterHits(QStringLiteral("preface")), QStringList{QStringLiteral("Preface")});
    QCOMPARE(d.summary.tocEntryCount, 5);
}

void TestTocEdits::staleOrInvalidEditsChangeNothing()
{
    const BookId book = addBook(QStringLiteral("stale.pdf"), 50);
    publish(book, analyzed(QStringLiteral("a")));
    const TocEditBase shown = baseOf(book);
    QVERIFY(edit(book, {rename(QStringLiteral("a1"), QStringLiteral("One"))}));

    // Based on what was shown before that edit: refused.
    auto stale = db([book, shown](QSqlDatabase& d) {
        return catalog::editToc(d, book, shown, {rename(QStringLiteral("a1"), QStringLiteral("Other"))});
    });
    QVERIFY(!stale);
    QCOMPARE(stale.error().code, ErrorCode::StaleGeneration);
    auto staleDiscard = db([book, shown](QSqlDatabase& d) { return catalog::useAnalyzedToc(d, book, shown); });
    QCOMPARE(staleDiscard.error().code, ErrorCode::StaleGeneration);
    QVERIFY(find(*details(book).toc, QStringLiteral("One")));

    // Invalid: nothing saved, even with a valid edit before it in the list.
    const QList<QList<TocEdit>> invalid{
        {rename(QStringLiteral("a2"), QStringLiteral("Valid")), rename(QStringLiteral("a1"), QStringLiteral("   "))},
        {setPage(QStringLiteral("a1"), 50)},  // 50 pages: indices 0-49.
        {setPage(QStringLiteral("a1"), -1)},
        {rename(QStringLiteral("nope"), QStringLiteral("X"))},
        {simple(TocEdit::Kind::SetParent, QStringLiteral("a1"), QStringLiteral("a2"))},  // a2 is under a1: a cycle.
        {simple(TocEdit::Kind::SetParent, QStringLiteral("a1"), QStringLiteral("a1"))},
        {simple(TocEdit::Kind::SetParent, QStringLiteral("a1"), QStringLiteral("nope"))},
        {},
    };
    for (const QList<TocEdit>& edits : invalid) {
        auto refused = edit(book, edits);
        QVERIFY(!refused);
        QCOMPARE(refused.error().code, ErrorCode::InvalidArgument);
    }
    const BookDetails d = details(book);
    QCOMPARE(d.tocRevision->number, 1);
    QVERIFY(!find(*d.toc, QStringLiteral("Valid")));
    auto history = db([book](QSqlDatabase& s) { return catalog::tocRevisions(s, book); });
    QCOMPARE(history.value().size(), 1);

    // A book without analyzed contents has nothing to edit.
    const BookId empty = addBook(QStringLiteral("empty.pdf"));
    auto none = edit(empty, {rename(QStringLiteral("a1"), QStringLiteral("X"))});
    QCOMPARE(none.error().code, ErrorCode::InvalidArgument);
}

void TestTocEdits::removeAndRestoreFollowTheHierarchy()
{
    const BookId book = addBook(QStringLiteral("remove.pdf"));
    // A parent listed after its child.
    publish(book, {entry(QStringLiteral("c"), 0, QStringLiteral("Child"), 5, QStringLiteral("p")),
                   entry(QStringLiteral("p"), 1, QStringLiteral("Parent"), 4),
                   entry(QStringLiteral("x"), 2, QStringLiteral("Other"), 9)});
    QVERIFY(edit(book, {simple(TocEdit::Kind::Remove, QStringLiteral("p"))}));
    BookDetails d = details(book);
    QVERIFY(find(*d.toc, QStringLiteral("Parent"))->removed);
    QVERIFY(find(*d.toc, QStringLiteral("Child"))->removed);  // With its sub-entries.
    QVERIFY(!find(*d.toc, QStringLiteral("Other"))->removed);
    QCOMPARE(d.summary.tocEntryCount, 1);
    QVERIFY(chapterHits(QStringLiteral("parent")).isEmpty());
    QVERIFY(chapterHits(QStringLiteral("child")).isEmpty());

    // A sub-entry cannot come back while its parent is removed.
    auto refused = edit(book, {simple(TocEdit::Kind::Restore, QStringLiteral("c"))});
    QCOMPARE(refused.error().code, ErrorCode::InvalidArgument);
    QVERIFY(edit(book, {simple(TocEdit::Kind::Restore, QStringLiteral("p"))}));
    d = details(book);
    QVERIFY(!find(*d.toc, QStringLiteral("Child"))->removed);
    QCOMPARE(chapterHits(QStringLiteral("child")), QStringList{QStringLiteral("Child")});
    QCOMPARE(d.summary.tocEntryCount, 3);

    // Moving the child to the top level, then under "Other" (listed after it).
    QVERIFY(edit(book, {simple(TocEdit::Kind::MakeRoot, QStringLiteral("c")),
                        simple(TocEdit::Kind::SetParent, QStringLiteral("c"), QStringLiteral("x"))}));
    d = details(book);
    QCOMPARE(find(*d.toc, QStringLiteral("Child"))->parentSdkEntryId, std::optional<QString>(QStringLiteral("x")));
    QCOMPARE(find(*d.toc, QStringLiteral("Child"))->edits, QStringList{QStringLiteral("level")});
}

// A new entry's visibility follows its parent, not the entry it was added after.
void TestTocEdits::addedEntriesAreHiddenOnlyUnderARemovedParent()
{
    const BookId book = addBook(QStringLiteral("add.pdf"));
    publish(book, analyzed(QStringLiteral("a")));
    TocEdit appendix;
    appendix.kind = TocEdit::Kind::Add;
    appendix.entryKey = QStringLiteral("a4");  // After "Index", removed in the same save.
    appendix.title = QStringLiteral("Appendix");
    appendix.page = 45;
    QVERIFY(edit(book, {simple(TocEdit::Kind::Remove, QStringLiteral("a4")), appendix}));
    BookDetails d = details(book);
    QVERIFY(!find(*d.toc, QStringLiteral("Appendix"))->removed);
    QVERIFY(find(*d.toc, QStringLiteral("Index"))->removed);
    QCOMPARE(d.summary.tocEntryCount, 4);
    QCOMPARE(chapterHits(QStringLiteral("appendix")), QStringList{QStringLiteral("Appendix")});

    // Added among the sub-entries of a removed parent: hidden with them.
    TocEdit note;
    note.kind = TocEdit::Kind::Add;
    note.entryKey = QStringLiteral("a2");  // Under "1 Introduction".
    note.title = QStringLiteral("1.2 Note");
    QVERIFY(edit(book, {simple(TocEdit::Kind::Remove, QStringLiteral("a1")), note}));
    d = details(book);
    QVERIFY(find(*d.toc, QStringLiteral("1.2 Note"))->removed);
    QVERIFY(chapterHits(QStringLiteral("note")).isEmpty());
    // Restoring the parent brings it back with the others.
    QVERIFY(edit(book, {simple(TocEdit::Kind::Restore, QStringLiteral("a1"))}));
    QCOMPARE(chapterHits(QStringLiteral("note")), QStringList{QStringLiteral("1.2 Note")});
}

// A run that edits are based on cannot be deleted by itself (the edits would
// silently disappear); deleting the whole book still removes everything.
void TestTocEdits::editsOutliveAttemptsToDeleteTheirRun()
{
    const BookId book = addBook(QStringLiteral("delete.pdf"));
    const RunId run = publish(book, analyzed(QStringLiteral("a")));
    QVERIFY(edit(book, {rename(QStringLiteral("a3"), QStringLiteral("2 Networking with TCP"))}));
    // The book's own pointer is cleared first, so only the revision refers to the run.
    const bool deleted = db([book, run](QSqlDatabase& d) {
        QSqlQuery q(d);
        q.exec(QStringLiteral("UPDATE books SET active_toc_run_id = NULL WHERE id = '%1'").arg(book.toString()));
        return q.exec(QStringLiteral("DELETE FROM toc_runs WHERE id = '%1'").arg(run.toString()));
    });
    QVERIFY(!deleted);
    auto revisions = db([book](QSqlDatabase& s) { return catalog::tocRevisions(s, book); });
    QCOMPARE(revisions.value().size(), 1);

    const bool bookDeleted = db([book](QSqlDatabase& d) {
        QSqlQuery q(d);
        return q.exec(QStringLiteral("DELETE FROM books WHERE id = '%1'").arg(book.toString()));
    });
    QVERIFY(bookDeleted);
    const int left = db([](QSqlDatabase& d) {
        QSqlQuery q(d);
        q.exec(QStringLiteral("SELECT (SELECT COUNT(*) FROM toc_edit_revisions) + (SELECT COUNT(*) FROM toc_edit_entries) "
                              "+ (SELECT COUNT(*) FROM toc_runs)"));
        return q.next() ? q.value(0).toInt() : -1;
    });
    QCOMPARE(left, 0);
}

void TestTocEdits::identicalRerunCarriesTheEdits()
{
    const BookId book = addBook(QStringLiteral("identical.pdf"));
    publish(book, analyzed(QStringLiteral("a")));
    QVERIFY(edit(book, {rename(QStringLiteral("a3"), QStringLiteral("2 Networking with TCP"))}));

    // The rerun finds the same entries under new SDK IDs.
    const RunId rerun = publish(book, analyzed(QStringLiteral("b")));
    const BookDetails d = details(book);
    QCOMPARE(d.tocRun, std::optional<RunId>(rerun));
    QVERIFY(d.tocRevision);
    QCOMPARE(d.tocRevision->number, 2);
    QCOMPARE(d.tocRevision->baseRun, rerun);
    QVERIFY(!d.tocRevision->needsReconciliation);
    const TocEntry* fixed = find(*d.toc, QStringLiteral("2 Networking with TCP"));
    QVERIFY(fixed);
    QCOMPARE(fixed->sdkEntryId, QStringLiteral("a3"));  // The key stays.
    QCOMPARE(chapterHits(QStringLiteral("networking")), QStringList{QStringLiteral("2 Networking with TCP")});
    // Later edits apply to the carried revision.
    QVERIFY(edit(book, {setPage(QStringLiteral("a3"), 8)}));
    QCOMPARE(find(*details(book).toc, QStringLiteral("2 Networking with TCP"))->destinationPage, std::optional<int>(8));
}

void TestTocEdits::changedRerunWaitsForTheUser()
{
    const BookId book = addBook(QStringLiteral("changed.pdf"));
    const RunId first = publish(book, analyzed(QStringLiteral("a")));
    QVERIFY(edit(book, {rename(QStringLiteral("a3"), QStringLiteral("2 Networking with TCP"))}));

    // The rerun reads the noisy title correctly and finds one entry more.
    QList<TocEntry> better = analyzed(QStringLiteral("b"));
    better[2].title = QStringLiteral("2 Networking with TCP/IP");
    better << entry(QStringLiteral("b5"), 4, QStringLiteral("Glossary"), 45);
    const RunId second = publish(book, better);

    // The edits stay in effect and searchable; the user decides.
    BookDetails d = details(book);
    QCOMPARE(d.tocRun, std::optional<RunId>(second));
    QVERIFY(d.tocRevision->needsReconciliation);
    QCOMPARE(d.tocRevision->baseRun, first);
    QVERIFY(d.summary.tocNeedsReconciliation);
    QVERIFY(find(*d.toc, QStringLiteral("2 Networking with TCP")));
    QVERIFY(!find(*d.toc, QStringLiteral("Glossary")));
    QVERIFY(find(*d.analyzedToc, QStringLiteral("Glossary")));  // The new analysis, to compare.
    QCOMPARE(chapterHits(QStringLiteral("networking")), QStringList{QStringLiteral("2 Networking with TCP")});
    QVERIFY(chapterHits(QStringLiteral("glossary")).isEmpty());

    // Editing while reconciliation is pending stays on the old base.
    auto more = edit(book, {rename(QStringLiteral("a4"), QStringLiteral("Index of terms"))});
    QVERIFY(more);
    QVERIFY(more.value().needsReconciliation);

    // Keep the edits: now based on the new run, no longer tied to its entries.
    const TocEditBase base = baseOf(book);
    auto kept = db([book, base](QSqlDatabase& s) { return catalog::keepTocEdits(s, book, base); });
    QVERIFY2(kept, kept ? "" : qPrintable(kept.error().message));
    QCOMPARE(kept.value().baseRun, second);
    d = details(book);
    QVERIFY(!d.tocRevision->needsReconciliation);
    QVERIFY(find(*d.toc, QStringLiteral("Index of terms")));
    auto again = db([book, b = baseOf(book)](QSqlDatabase& s) { return catalog::keepTocEdits(s, book, b); });
    QCOMPARE(again.error().code, ErrorCode::InvalidArgument);  // Nothing left to reconcile.

    // Or return to the analysis: its entries again; the revisions are kept.
    auto used = db([book, b = baseOf(book)](QSqlDatabase& s) { return catalog::useAnalyzedToc(s, book, b); });
    QVERIFY(used);
    d = details(book);
    QVERIFY(!d.tocRevision);
    QVERIFY(!d.analyzedToc);
    QVERIFY(find(*d.toc, QStringLiteral("Glossary")));
    QCOMPARE(chapterHits(QStringLiteral("glossary")), QStringList{QStringLiteral("Glossary")});
    QVERIFY(chapterHits(QStringLiteral("terms")).isEmpty());
    auto history = db([book](QSqlDatabase& s) { return catalog::tocRevisions(s, book); });
    QCOMPARE(history.value().size(), 3);
}

void TestTocEdits::editsDuringAnalysisSurviveAndStaleRunsCannotReplaceThem()
{
    const BookId book = addBook(QStringLiteral("running.pdf"));
    publish(book, analyzed(QStringLiteral("a")));
    // A rerun is requested (its job runs), then the user edits.
    auto oldTicket = db([book](QSqlDatabase& d) { return catalog::requestTocRun(d, book); });
    auto ticket = db([book](QSqlDatabase& d) { return catalog::requestTocRun(d, book); });
    QVERIFY(oldTicket && ticket);
    QVERIFY(edit(book, {rename(QStringLiteral("a2"), QStringLiteral("1.1 First steps"))}));

    // The superseded run cannot publish; the edits are untouched.
    const auto publishWith = [this, book](const PublishTicket& t, const QList<TocEntry>& entries) {
        return db([book, t, entries](QSqlDatabase& d) {
            TocAnalysis toc{QStringLiteral("plan_ready"), true, entries};
            const RunIdentity run{t.sourceSha256, QStringLiteral("test"), QString(), QStringLiteral("{}"), toc.outcome,
                                  std::nullopt};
            return catalog::publishToc(d, t, run, toc);
        });
    };
    auto late = publishWith(oldTicket.value(), analyzed(QStringLiteral("x")));
    QCOMPARE(late.error().code, ErrorCode::StaleGeneration);
    QVERIFY(find(*details(book).toc, QStringLiteral("1.1 First steps")));

    // The current run finds the same entries: the edit carries over.
    QVERIFY(publishWith(ticket.value(), analyzed(QStringLiteral("c"))));
    const BookDetails d = details(book);
    QVERIFY(!d.tocRevision->needsReconciliation);
    QVERIFY(find(*d.toc, QStringLiteral("1.1 First steps")));
}

void TestTocEdits::trashedBooksAreNotEdited()
{
    const BookId book = addBook(QStringLiteral("trash.pdf"));
    publish(book, analyzed(QStringLiteral("a")));
    QVERIFY(db([book](QSqlDatabase& d) { return catalog::trashBook(d, book); }));
    auto refused = edit(book, {rename(QStringLiteral("a1"), QStringLiteral("X"))});
    QCOMPARE(refused.error().code, ErrorCode::Trashed);
    QVERIFY(!details(book).tocRevision);
}

void TestTocEdits::rebuildMatchesTheEditedContents()
{
    const BookId book = addBook(QStringLiteral("rebuild.pdf"));
    publish(book, analyzed(QStringLiteral("a")));
    QVERIFY(edit(book, {rename(QStringLiteral("a3"), QStringLiteral("2 Networking with TCP")),
                        simple(TocEdit::Kind::Remove, QStringLiteral("a4"))}));
    const auto all = [this] {
        QStringList hits;
        for (const char* term : {"networking", "netwrking", "index", "introduction", "started"})
            hits << chapterHits(QLatin1String(term));
        hits.sort();
        return hits;
    };
    const QStringList before = all();
    QVERIFY(db([](QSqlDatabase& d) { return catalog::rebuildSearchIndex(d); }));
    const QStringList after = all();
    QCOMPARE(after, before);
    QVERIFY(!after.contains(QStringLiteral("Index")));
    QVERIFY(after.contains(QStringLiteral("2 Networking with TCP")));
}

QTEST_GUILESS_MAIN(TestTocEdits)
#include "tst_tocedits.moc"
