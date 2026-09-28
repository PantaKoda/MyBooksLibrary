// A2/A3 organization on a real library folder with synthetic records:
// collections are membership only, trashed books keep their memberships but
// are hidden, search can be limited to a collection, and trash and restore
// end and requeue work without a stale job blocking or publishing.
#include "catalog/catalog.h"
#include "catalog/collections.h"
#include "catalog/jobs.h"
#include "catalog/library.h"
#include "search/searchindex.h"

#include <QCryptographicHash>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace mbl::domain;
using mbl::catalog::Library;
namespace catalog = mbl::catalog;
namespace search = mbl::search;

class TestOrganization : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void collectionsAreMembershipOnly();
    void namesAreCheckedAndDeletingKeepsTheBooks();
    void trashedBooksKeepMembershipsButAreHidden();
    void searchWithinACollection();
    void trashEndsJobsAndRestoreRequeuesMissingWork();
    void aStaleJobCannotBlockOrPublishAfterRestore();
    void restoreEndsJobsLeftOpenByAnOlderTrash();
    void restoreResumesExactlyWhatTheTrashStopped();

private:
    template <typename Task>
    auto db(Task task) { return m_library->run(std::move(task)).result(); }
    BookId addBook(const QString& fileName, const QString& title = {});
    QList<JobRecord> jobsOf(const BookId& book);
    int count(const QString& sql);

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Library> m_library;
};

void TestOrganization::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    auto opened = Library::open(m_dir->path());
    QVERIFY2(opened, opened ? "" : qPrintable(opened.error().message));
    m_library = std::move(opened.value());
}

void TestOrganization::cleanup()
{
    m_library.reset();
    m_dir.reset();
}

BookId TestOrganization::addBook(const QString& fileName, const QString& title)
{
    auto id = db([fileName, title](QSqlDatabase& d) -> Result<BookId> {
        NewBook b;
        b.asset.id = AssetId::create();
        b.asset.sha256 = QString::fromLatin1(QCryptographicHash::hash(fileName.toUtf8(), QCryptographicHash::Sha256).toHex());
        b.asset.byteSize = 1;
        b.asset.pageCount = 10;
        b.asset.managedPath = QStringLiteral("files/%1/source.pdf").arg(b.asset.id.toString());
        b.originalFileName = fileName;
        b.originalPath = fileName;
        auto book = catalog::registerBook(d, b);
        if (!book || title.isEmpty())
            return book;
        auto ticket = catalog::requestMetadataRun(d, book.value());
        if (!ticket)
            return ticket.error();
        ExtractedMetadata m;
        m.titleStatus = FieldStatus::Resolved;
        m.title = title;
        const RunIdentity run{ticket.value().sourceSha256, QStringLiteral("t"), QString(), QStringLiteral("{}"),
                              QStringLiteral("completed"), std::nullopt};
        if (auto published = catalog::publishMetadata(d, ticket.value(), run, m); !published)
            return published.error();
        return book;
    });
    if (!id)
        qFatal("addBook: %s", qPrintable(id.error().message));
    return id.value();
}

QList<JobRecord> TestOrganization::jobsOf(const BookId& book)
{
    QList<JobRecord> out;
    const auto all = db([](QSqlDatabase& d) { return catalog::listJobs(d, false, -1); });  // Kept alive for the loop.
    for (const JobRecord& j : all.value()) {
        if (j.book == book)
            out << j;
    }
    return out;
}

int TestOrganization::count(const QString& sql)
{
    return db([sql](QSqlDatabase& d) {
        QSqlQuery q(d);
        return q.exec(sql) && q.next() ? q.value(0).toInt() : -1;
    });
}

void TestOrganization::collectionsAreMembershipOnly()
{
    const BookId a = addBook(QStringLiteral("a.pdf"), QStringLiteral("Algorithms"));
    const BookId b = addBook(QStringLiteral("b.pdf"), QStringLiteral("Networks"));
    const BookId c = addBook(QStringLiteral("c.pdf"), QStringLiteral("Compilers"));
    const int assets = count(QStringLiteral("SELECT COUNT(*) FROM assets"));
    auto study = db([](QSqlDatabase& d) { return catalog::createCollection(d, QStringLiteral(" Study ")); });
    auto work = db([](QSqlDatabase& d) { return catalog::createCollection(d, QStringLiteral("Work")); });
    QVERIFY(study && work);
    // b is in both; adding it twice changes nothing.
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::addToCollection(d, study.value(), {a, b, b}); }));
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::addToCollection(d, work.value(), {b, c}); }));
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::addToCollection(d, study.value(), {a}); }));

    auto list = db([](QSqlDatabase& d) { return catalog::listCollections(d); }).value();
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.at(0).name, QStringLiteral("Study"));  // Trimmed, by name.
    QCOMPARE(list.at(0).bookCount, 2);
    QCOMPARE(list.at(1).bookCount, 2);
    auto books = db([&](QSqlDatabase& d) { return catalog::listCollectionBooks(d, study.value()); }).value();
    // Library order is by import time, and books imported in the same
    // millisecond are in no particular order: compare the titles as a set.
    QStringList titles;
    for (const BookSummary& book : books)
        titles << book.displayTitle;
    titles.sort();
    QCOMPARE(titles, (QStringList{QStringLiteral("Algorithms"), QStringLiteral("Networks")}));
    auto ofB = db([&](QSqlDatabase& d) { return catalog::collectionsOf(d, b); }).value();
    QCOMPARE(ofB.size(), 2);
    // No file or book was copied.
    QCOMPARE(count(QStringLiteral("SELECT COUNT(*) FROM assets")), assets);
    QCOMPARE(count(QStringLiteral("SELECT COUNT(*) FROM books")), 3);

    QVERIFY(db([&](QSqlDatabase& d) { return catalog::removeFromCollection(d, study.value(), {b}); }));
    QCOMPARE(db([&](QSqlDatabase& d) { return catalog::collectionsOf(d, b); }).value().size(), 1);

    // After a restart.
    m_library.reset();
    auto reopened = Library::open(m_dir->path());
    QVERIFY(reopened);
    m_library = std::move(reopened.value());
    books = db([&](QSqlDatabase& d) { return catalog::listCollectionBooks(d, study.value()); }).value();
    QCOMPARE(books.size(), 1);
    QCOMPARE(books.at(0).id, a);
}

void TestOrganization::namesAreCheckedAndDeletingKeepsTheBooks()
{
    const BookId a = addBook(QStringLiteral("a.pdf"));
    auto first = db([](QSqlDatabase& d) { return catalog::createCollection(d, QStringLiteral("Reading")); });
    QVERIFY(first);
    auto same = db([](QSqlDatabase& d) { return catalog::createCollection(d, QStringLiteral("reading")); });
    QCOMPARE(same.error().code, ErrorCode::Duplicate);
    auto empty = db([](QSqlDatabase& d) { return catalog::createCollection(d, QStringLiteral("  ")); });
    QCOMPARE(empty.error().code, ErrorCode::InvalidArgument);
    auto other = db([](QSqlDatabase& d) { return catalog::createCollection(d, QStringLiteral("Later")); });
    QVERIFY(other);
    auto clash = db([&](QSqlDatabase& d) { return catalog::renameCollection(d, other.value(), QStringLiteral("READING")); });
    QCOMPARE(clash.error().code, ErrorCode::Duplicate);
    // Renaming to itself in another case is allowed.
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::renameCollection(d, first.value(), QStringLiteral("READING")); }));
    auto missing = db([](QSqlDatabase& d) { return catalog::addToCollection(d, CollectionId::create(), {}); });
    QCOMPARE(missing.error().code, ErrorCode::NotFound);
    auto unknownBook = db([&](QSqlDatabase& d) { return catalog::addToCollection(d, first.value(), {a, BookId::create()}); });
    QCOMPARE(unknownBook.error().code, ErrorCode::NotFound);
    QCOMPARE(db([&](QSqlDatabase& d) { return catalog::listCollectionBooks(d, first.value()); }).value().size(), 0);  // All or nothing.

    QVERIFY(db([&](QSqlDatabase& d) { return catalog::addToCollection(d, first.value(), {a}); }));
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::deleteCollection(d, first.value()); }));
    QCOMPARE(count(QStringLiteral("SELECT COUNT(*) FROM collection_books")), 0);
    QCOMPARE(db([](QSqlDatabase& d) { return catalog::listBooks(d, Lifecycle::Active); }).value().size(), 1);
}

void TestOrganization::trashedBooksKeepMembershipsButAreHidden()
{
    const BookId a = addBook(QStringLiteral("a.pdf"), QStringLiteral("Algorithms"));
    const BookId b = addBook(QStringLiteral("b.pdf"), QStringLiteral("Networks"));
    auto study = db([](QSqlDatabase& d) { return catalog::createCollection(d, QStringLiteral("Study")); }).value();
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::addToCollection(d, study, {a, b}); }));

    QVERIFY(db([&](QSqlDatabase& d) { return catalog::trashBook(d, b); }));
    QCOMPARE(db([](QSqlDatabase& d) { return catalog::listCollections(d); }).value().first().bookCount, 1);
    QCOMPARE(db([&](QSqlDatabase& d) { return catalog::listCollectionBooks(d, study); }).value().size(), 1);
    QCOMPARE(db([&](QSqlDatabase& d) { return catalog::collectionsOf(d, b); }).value().size(), 1);  // Kept.
    auto trashed = db([](QSqlDatabase& d) { return catalog::listBooks(d, Lifecycle::Trashed); }).value();
    QCOMPARE(trashed.size(), 1);
    QVERIFY(trashed.first().trashedAt.isValid());
    auto refused = db([&](QSqlDatabase& d) { return catalog::addToCollection(d, study, {b}); });
    QCOMPARE(refused.error().code, ErrorCode::Trashed);

    QVERIFY(db([&](QSqlDatabase& d) { return catalog::restoreBook(d, b); }));
    QCOMPARE(db([&](QSqlDatabase& d) { return catalog::listCollectionBooks(d, study); }).value().size(), 2);
    QVERIFY(!db([&](QSqlDatabase& d) { return catalog::bookDetails(d, b); }).value().summary.trashedAt.isValid());
}

void TestOrganization::searchWithinACollection()
{
    const BookId a = addBook(QStringLiteral("a.pdf"), QStringLiteral("Networks in practice"));
    const BookId b = addBook(QStringLiteral("b.pdf"), QStringLiteral("Networks in theory"));
    auto study = db([](QSqlDatabase& d) { return catalog::createCollection(d, QStringLiteral("Study")); }).value();
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::addToCollection(d, study, {b}); }));
    const auto find = [this](std::optional<CollectionId> collection) {
        SearchRequest request;
        request.text = QStringLiteral("networks");
        request.collection = collection;
        auto response = db([request](QSqlDatabase& d) { return search::search(d, request); });
        QList<BookId> ids;
        for (const BookHit& h : response.value().books)
            ids << h.book;
        return ids;
    };
    QCOMPARE(find(std::nullopt).size(), 2);
    QCOMPARE(find(study), QList<BookId>{b});
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::trashBook(d, b); }));
    QVERIFY(find(study).isEmpty());
    QCOMPARE(find(CollectionId::create()), QList<BookId>{});
    Q_UNUSED(a);
}

void TestOrganization::trashEndsJobsAndRestoreRequeuesMissingWork()
{
    const BookId book = addBook(QStringLiteral("jobs.pdf"), QStringLiteral("Has a title"));  // Metadata published.
    const BookId bare = addBook(QStringLiteral("bare.pdf"));                                // Nothing published.
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::enqueueJob(d, book, JobKind::Toc); }));
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::enqueueJob(d, bare, JobKind::Metadata); }));
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::enqueueJob(d, bare, JobKind::Toc); }));

    QVERIFY(db([&](QSqlDatabase& d) { return catalog::trashBook(d, book); }));
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::trashBook(d, bare); }));
    const QList<JobRecord> ended = jobsOf(book) + jobsOf(bare);
    for (const JobRecord& j : ended) {
        QCOMPARE(j.state, JobState::Cancelled);
        QCOMPARE(j.outcome, QStringLiteral("trashed"));
    }

    QVERIFY(db([&](QSqlDatabase& d) { return catalog::restoreBook(d, book); }));
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::restoreBook(d, bare); }));
    const auto queued = [this](const BookId& b) {
        QList<JobKind> kinds;
        for (const JobRecord& j : jobsOf(b)) {
            if (j.state == JobState::Queued)
                kinds << j.kind;
        }
        return kinds;
    };
    QCOMPARE(queued(book), QList<JobKind>{JobKind::Toc});  // Its metadata is already published.
    QCOMPARE(queued(bare).size(), 2);
    const BookDetails d = db([&](QSqlDatabase& s) { return catalog::bookDetails(s, bare); }).value();
    for (const JobRecord& j : jobsOf(bare)) {
        if (j.state == JobState::Queued)
            QCOMPARE(j.generation, j.kind == JobKind::Metadata ? d.metadataGeneration : d.tocGeneration);
    }
    // Restoring again queues nothing more.
    QVERIFY(db([&](QSqlDatabase& s) { return catalog::restoreBook(s, bare); }));
    QCOMPARE(queued(bare).size(), 2);
}

void TestOrganization::aStaleJobCannotBlockOrPublishAfterRestore()
{
    const BookId book = addBook(QStringLiteral("race.pdf"));
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::enqueueJob(d, book, JobKind::Metadata); }));
    auto claimed = db([](QSqlDatabase& d) { return catalog::claimNextJob(d); });
    QVERIFY(claimed && claimed.value());
    const JobRecord stale = *claimed.value();

    // Trashed and restored while the SDK call runs.
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::trashBook(d, book); }));
    QCOMPARE(db([&](QSqlDatabase& d) { return catalog::job(d, stale.id); }).value().state, JobState::CancelRequested);
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::restoreBook(d, book); }));
    JobRecord fresh;  // The new metadata request (a contents job is queued too).
    for (const JobRecord& j : jobsOf(book)) {
        if (j.state == JobState::Queued && j.kind == JobKind::Metadata)
            fresh = j;
    }
    QVERIFY(!fresh.id.isNull());  // Not blocked by the stale job.
    QVERIFY(fresh.generation > stale.generation);

    // The stale job's result is refused; ending it keeps the reason.
    ExtractedMetadata m;
    m.titleStatus = FieldStatus::Resolved;
    m.title = QStringLiteral("Stale");
    const RunIdentity run{stale.sourceSha256, QStringLiteral("t"), QString(), QStringLiteral("{}"),
                          QStringLiteral("completed"), std::nullopt};
    auto late = db([&](QSqlDatabase& d) { return catalog::completeMetadataJob(d, stale, RunId::create(), run, m, {}, 10); });
    QVERIFY(!late);
    QVERIFY(db([&](QSqlDatabase& d) {
        return catalog::finishJob(d, stale.id, JobState::Cancelled, QStringLiteral("cancelled"), QStringLiteral("Cancelled."));
    }));
    const JobRecord ended = db([&](QSqlDatabase& d) { return catalog::job(d, stale.id); }).value();
    QCOMPARE(ended.state, JobState::Cancelled);
    QCOMPARE(ended.outcome, QStringLiteral("trashed"));

    // The fresh job runs and publishes.
    auto next = db([](QSqlDatabase& d) { return catalog::claimNextJob(d); });
    QVERIFY(next && next.value());
    QCOMPARE(next.value()->id, fresh.id);
    m.title = QStringLiteral("Fresh");
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::completeMetadataJob(d, *next.value(), RunId::create(), run, m, {}, 10); }));
    QCOMPARE(*db([&](QSqlDatabase& d) { return catalog::bookDetails(d, book); }).value().summary.metadata.title,
             QStringLiteral("Fresh"));
}

// A book trashed by a version before migration 7 may still have a queued job
// with a generation from before the trash; restore must not keep it.
void TestOrganization::restoreEndsJobsLeftOpenByAnOlderTrash()
{
    const BookId book = addBook(QStringLiteral("old.pdf"));
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::enqueueJob(d, book, JobKind::Metadata); }));
    const JobId old = jobsOf(book).first().id;
    QVERIFY(db([&](QSqlDatabase& d) {
        QSqlQuery q(d);
        return q.exec(QStringLiteral("UPDATE books SET lifecycle = 'trashed', metadata_generation = metadata_generation + 1, "
                                     "toc_generation = toc_generation + 1 WHERE id = '%1'").arg(book.toString()));
    }));
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::restoreBook(d, book); }));
    QCOMPARE(db([&](QSqlDatabase& d) { return catalog::job(d, old); }).value().state, JobState::Cancelled);
    const BookDetails d = db([&](QSqlDatabase& s) { return catalog::bookDetails(s, book); }).value();
    int current = 0;
    for (const JobRecord& j : jobsOf(book)) {
        if (j.state == JobState::Queued && j.kind == JobKind::Metadata) {
            QCOMPARE(j.generation, d.metadataGeneration);
            ++current;
        }
    }
    QCOMPARE(current, 1);
}

// Restore resumes the jobs the trash ended, not "whatever has no result":
// an extraction that failed before the trash is not retried on its own, a
// queued rerun of a book with results is resumed, and a later trash and
// restore with nothing running queues nothing.
void TestOrganization::restoreResumesExactlyWhatTheTrashStopped()
{
    const auto queuedKinds = [this](const BookId& b) {
        QList<JobKind> kinds;
        for (const JobRecord& j : jobsOf(b)) {
            if (j.state == JobState::Queued)
                kinds << j.kind;
        }
        std::sort(kinds.begin(), kinds.end());
        return kinds;
    };

    // (a) Metadata failed before the trash; contents were still queued.
    const BookId failed = addBook(QStringLiteral("failed.pdf"));
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::enqueueJob(d, failed, JobKind::Metadata); }));
    auto claimed = db([](QSqlDatabase& d) { return catalog::claimNextJob(d); });
    QVERIFY(claimed && claimed.value());
    QVERIFY(db([&](QSqlDatabase& d) {
        return catalog::finishJob(d, claimed.value()->id, JobState::Failed, QStringLiteral("sdk_error"), QStringLiteral("Unreadable."));
    }));
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::enqueueJob(d, failed, JobKind::Toc); }));
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::trashBook(d, failed); }));
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::restoreBook(d, failed); }));
    QCOMPARE(queuedKinds(failed), QList<JobKind>{JobKind::Toc});
    QCOMPARE(db([&](QSqlDatabase& d) { return catalog::job(d, claimed.value()->id); }).value().outcome,
             QStringLiteral("sdk_error"));  // Still failed, until the user retries.

    // (b) Metadata published; the user's rerun was queued when the book was trashed.
    const BookId rerun = addBook(QStringLiteral("rerun.pdf"), QStringLiteral("Has a title"));
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::enqueueJob(d, rerun, JobKind::Metadata); }));
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::trashBook(d, rerun); }));
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::restoreBook(d, rerun); }));
    QCOMPARE(queuedKinds(rerun), QList<JobKind>{JobKind::Metadata});

    // A later cycle with nothing running: the jobs "trashed" in the first
    // cycle are older than this trash and are not resumed again.
    auto next = db([](QSqlDatabase& d) { return catalog::claimNextJob(d); });
    QVERIFY(next && next.value());
    QVERIFY(db([&](QSqlDatabase& d) {
        return catalog::finishJob(d, next.value()->id, JobState::Failed, QStringLiteral("sdk_error"), QStringLiteral("x"));
    }));
    auto again = db([](QSqlDatabase& d) { return catalog::claimNextJob(d); });
    while (again && again.value()) {  // Drain whatever else is queued, as failures.
        QVERIFY(db([&](QSqlDatabase& d) {
            return catalog::finishJob(d, again.value()->id, JobState::Failed, QStringLiteral("sdk_error"), QStringLiteral("x"));
        }));
        again = db([](QSqlDatabase& d) { return catalog::claimNextJob(d); });
    }
    QTest::qWait(5);  // A later trash time than the first cycle's jobs.
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::trashBook(d, rerun); }));
    QVERIFY(db([&](QSqlDatabase& d) { return catalog::restoreBook(d, rerun); }));
    QVERIFY(queuedKinds(rerun).isEmpty());
}

QTEST_GUILESS_MAIN(TestOrganization)
#include "tst_organization.moc"
