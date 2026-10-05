// Presentation: export jobs in the job queue and the book list. Writing a
// bookmarked copy is its own kind of work: it has its own words in the queue,
// is asked for again with a destination rather than retried, and never
// changes a book's metadata or contents state in the list. And a book's
// activity (the row's spinner): running while a job of it runs, waiting
// while one is queued, nothing otherwise.
#include "presentation/booklistmodel.h"
#include "presentation/joblistmodel.h"

#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

using namespace mbl::domain;
using mbl::presentation::BookListModel;
using mbl::presentation::JobListModel;

namespace {

JobRecord jobOf(const BookId& book, JobKind kind, JobState state, int secondsAfter)
{
    JobRecord j;
    j.id = JobId::create();
    j.book = book;
    j.kind = kind;
    j.state = state;
    j.sourceSha256 = QString(64, u'a');
    j.createdAt = QDateTime(QDate(2026, 9, 29), QTime(12, 0), QTimeZone::UTC).addSecs(secondsAfter);
    j.updatedAt = j.createdAt;
    return j;
}

} // namespace

class TestJobModels : public QObject {
    Q_OBJECT

private slots:
    void exportsHaveTheirOwnWordsInTheQueue();
    void exportsDoNotChangeTheBookList();
    void activityFollowsTheJobs();
};

void TestJobModels::exportsHaveTheirOwnWordsInTheQueue()
{
    const BookId book = BookId::create();
    JobListModel model;
    model.setTitleLookup([](const BookId&) { return QStringLiteral("Networks"); });
    JobRecord running = jobOf(book, JobKind::Export, JobState::Running, 0);
    model.upsert(running);
    const QModelIndex row = model.index(0);
    QCOMPARE(model.data(row, JobListModel::KindTextRole).toString(), QStringLiteral("Bookmarked copy"));
    QCOMPARE(model.data(row, JobListModel::StateTextRole).toString(), QStringLiteral("Writing the bookmarked copy…"));
    QCOMPARE(model.summary(), QStringLiteral("Writing a bookmarked copy: Networks"));

    JobRecord failed = running;
    failed.state = JobState::Failed;
    failed.outcome = QStringLiteral("output_exists");
    failed.error = QStringLiteral("A file with that name already exists.");
    failed.updatedAt = running.updatedAt.addSecs(1);
    model.upsert(failed);
    QCOMPARE(model.data(row, JobListModel::StateTextRole).toString(),
             QStringLiteral("Failed: a file with that name already exists"));
    QVERIFY(!model.data(row, JobListModel::CanRetryRole).toBool());  // Asked for again with a destination.

    JobRecord interrupted = jobOf(book, JobKind::Export, JobState::Interrupted, 5);
    interrupted.error = QStringLiteral("The application closed while the copy was being written; "
                                       "the file may or may not have been saved.");
    model.upsert(interrupted);
    const QModelIndex newest = model.index(0);
    QCOMPARE(model.data(newest, JobListModel::StateTextRole).toString(),
             QStringLiteral("Interrupted when the application closed; the copy may not have been saved"));
    QCOMPARE(model.data(newest, JobListModel::DetailRole).toString(), interrupted.error);

    JobRecord written = jobOf(book, JobKind::Export, JobState::Succeeded, 9);
    model.upsert(written);
    QCOMPARE(model.data(model.index(0), JobListModel::StateTextRole).toString(), QStringLiteral("Copy saved"));
}

void TestJobModels::exportsDoNotChangeTheBookList()
{
    BookSummary summary;
    summary.id = BookId::create();
    summary.displayTitle = QStringLiteral("Networks");
    BookListModel books;
    books.setBooks({summary});
    books.setLatestJobs({jobOf(summary.id, JobKind::Toc, JobState::Succeeded, 0)});
    const QString before = books.processingStateOf(summary.id);

    books.updateJob(jobOf(summary.id, JobKind::Export, JobState::Running, 10));
    QCOMPARE(books.processingStateOf(summary.id), before);
    books.setLatestJobs({jobOf(summary.id, JobKind::Export, JobState::Failed, 20)});
    QCOMPARE(books.processingStateOf(summary.id), before);
}

void TestJobModels::activityFollowsTheJobs()
{
    BookSummary summary;
    summary.id = BookId::create();
    summary.displayTitle = QStringLiteral("Neutron Stars");
    BookListModel books;
    books.setBooks({summary});
    const QModelIndex row = books.index(0);
    const auto activity = [&] { return books.data(row, BookListModel::ActivityRole).toString(); };
    QCOMPARE(activity(), QString());
    QCOMPARE(books.roleNames().value(BookListModel::ActivityRole), QByteArray("activity"));

    // Both jobs queued: waiting.
    books.setLatestJobs({jobOf(summary.id, JobKind::Metadata, JobState::Queued, 0),
                         jobOf(summary.id, JobKind::Toc, JobState::Queued, 0)});
    QCOMPARE(activity(), QStringLiteral("waiting"));
    // One running: running, and the row says it changed.
    JobRecord metadata = jobOf(summary.id, JobKind::Metadata, JobState::Running, 1);
    QSignalSpy changed(&books, &BookListModel::dataChanged);
    books.updateJob(metadata);
    QCOMPARE(activity(), QStringLiteral("running"));
    QCOMPARE(changed.size(), 1);
    QVERIFY(changed.first().at(2).value<QList<int>>().contains(BookListModel::ActivityRole));
    // Metadata done, contents still queued: waiting again.
    metadata.state = JobState::Succeeded;
    metadata.updatedAt = metadata.updatedAt.addSecs(5);
    books.updateJob(metadata);
    QCOMPARE(activity(), QStringLiteral("waiting"));
    // A cancel being honoured still runs.
    books.updateJob(jobOf(summary.id, JobKind::Toc, JobState::CancelRequested, 10));
    QCOMPARE(activity(), QStringLiteral("running"));
    // All finished, or only an export running: nothing.
    books.updateJob(jobOf(summary.id, JobKind::Toc, JobState::Failed, 20));
    QCOMPARE(activity(), QString());
    books.updateJob(jobOf(summary.id, JobKind::Export, JobState::Running, 30));
    QCOMPARE(activity(), QString());
    QCOMPARE(books.activityOf(summary.id), QString());
}

QTEST_GUILESS_MAIN(TestJobModels)
#include "tst_jobmodels.moc"
