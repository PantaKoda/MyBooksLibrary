// A4 + A2: durable metadata jobs with a fake extractor, so races, cancellation
// and restarts are deterministic. The real SDK path is tst_sdkmetadataextractor.
#include "catalog/catalog.h"
#include "catalog/jobs.h"
#include "catalog/library.h"
#include "processing/processingcoordinator.h"
#include "storage/filecopy.h"
#include "storage/importservice.h"
#include "storage/reportstore.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <functional>
#include <stdexcept>

using namespace mbl::domain;
using mbl::catalog::Library;
using mbl::processing::MetadataExtraction;
using mbl::processing::MetadataExtractor;
using mbl::processing::ProcessingCoordinator;
namespace catalog = mbl::catalog;
namespace storage = mbl::storage;

namespace {

QString fixture(const char* name)
{
    return QDir(QStringLiteral(MBL_FIXTURES_DIR)).filePath(QLatin1StringView(name));
}

// Returns what `behavior` returns; records the calling thread.
class FakeExtractor : public MetadataExtractor {
public:
    std::function<MetadataExtraction(const QString&, const std::atomic_bool&)> behavior;
    std::atomic<QThread*> thread{nullptr};
    std::atomic_int calls{0};
    std::atomic_bool started{false};

    MetadataExtraction extract(const QString& path, const std::atomic_bool& cancel) override
    {
        thread = QThread::currentThread();
        ++calls;
        started = true;
        return behavior(path, cancel);
    }
};

// A successful extraction of the file at `path` with `title`.
MetadataExtraction success(const QString& path, const QString& title)
{
    MetadataExtraction r;
    r.status = MetadataExtraction::Status::Completed;
    r.sourceSha256 = storage::sha256OfFile(path).value_or(QString());
    r.pageCount = 3;
    r.metadata.titleStatus = FieldStatus::Resolved;
    r.metadata.title = title;
    r.metadata.publicationYearStatus = FieldStatus::Resolved;
    r.metadata.publicationYear = 2020;
    MetadataFieldDetail d;
    d.field = MetadataField::Title;
    d.evidence << MetadataEvidence{0, title, QStringLiteral("largest text on the title page")};
    d.alternatives << MetadataCandidate{QStringLiteral("Runner-up"), 0.4, {}, {QStringLiteral("smaller")}};
    d.reasons << QStringLiteral("one prominent candidate");
    r.details << d;
    r.reportJson = QByteArrayLiteral("{\"kind\":\"fake\"}");
    r.sdkVersion = QStringLiteral("fake");
    r.optionsJson = QStringLiteral("{}");
    r.outcome = QStringLiteral("completed");
    return r;
}

// Blocks until `release` is set or the job is cancelled (if `honourCancel`).
void waitFor(const std::atomic_bool& release, const std::atomic_bool& cancel, bool honourCancel)
{
    while (!release.load() && !(honourCancel && cancel.load()))
        QThread::msleep(2);
}

} // namespace

class TestProcessingCoordinator : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void publishesMetadataFromTheWorker();
    void enqueueReturnsTheOpenJob();
    void cancelStopsARunningJob();
    void cancelWhileTheSdkCompletesDoesNotPublish();
    void correctionsMadeWhileRunningSurvive();
    void trashedWhileRunningIsNotPublished();
    void supersededResultIsNotPublished();
    void failureKeepsEarlierResults();
    void sourceMismatchFails();
    void restartRecoversInterruptedJobs();
    void recoveryRemovesOnlyUnpublishedReports();
    void queuedJobOfTrashedBookNeverRuns();
    void metadataJobsRunBeforeTocJobs();
    void destructionDuringAJobInterruptsIt();
    void stopKeepsWorkForRestart();
    void stopDoesNotDiscardACompletedResult();
    void retryWhileCancellingIsNotLost();
    void cancelRacingTheClaimReachesTheSdk();
    void cancelAllCancelsRunningAndQueued();
    void extractorExceptionFailsTheJob();

private:
    template <typename Task>
    auto db(Task task) { return m_library->run(std::move(task)).result(); }
    BookId importBook(const char* fixtureName);
    JobRecord jobOf(const JobId& id) { return db([id](QSqlDatabase& d) { return catalog::job(d, id); }).value(); }
    BookDetails detailsOf(const BookId& id)
    {
        return db([id](QSqlDatabase& d) { return catalog::bookDetails(d, id); }).value();
    }
    // Enqueues and waits until the job reaches a terminal state.
    JobRecord runToEnd(ProcessingCoordinator& c, const BookId& book);
    QStringList reportFiles() { return QDir(m_root->filePath(QStringLiteral("reports"))).entryList(QDir::Files); }
    void restartLibrary()
    {
        m_library.reset();
        auto opened = Library::open(m_root->path());
        QVERIFY(opened);
        m_library = std::move(opened.value());
    }

    std::unique_ptr<QTemporaryDir> m_root;
    std::unique_ptr<Library> m_library;
    std::shared_ptr<FakeExtractor> m_fake;
};

void TestProcessingCoordinator::init()
{
    m_root = std::make_unique<QTemporaryDir>();
    auto opened = Library::open(m_root->path());
    QVERIFY2(opened, opened ? "" : qPrintable(opened.error().message));
    m_library = std::move(opened.value());
    m_fake = std::make_shared<FakeExtractor>();
}

void TestProcessingCoordinator::cleanup()
{
    m_fake.reset();
    m_library.reset();
    m_root.reset();
}

BookId TestProcessingCoordinator::importBook(const char* fixtureName)
{
    storage::ImportService importer(*m_library);
    const auto r = importer.importFile(fixture(fixtureName));
    if (!r.book)
        qFatal("import failed: %s", qPrintable(r.error));
    return *r.book;
}

JobRecord TestProcessingCoordinator::runToEnd(ProcessingCoordinator& c, const BookId& book)
{
    QSignalSpy changed(&c, &ProcessingCoordinator::jobChanged);
    c.enqueueMetadata(book);
    std::optional<JobId> id;
    const bool ended = QTest::qWaitFor(
        [&] {
            for (const auto& args : changed) {
                const auto j = args.at(0).value<JobRecord>();
                if (j.book == book) {
                    id = j.id;
                    if (!isOpen(j.state))
                        return true;
                }
            }
            return false;
        },
        20000);
    if (!ended || !id) {
        QTest::qFail("the job did not finish", __FILE__, __LINE__);
        return {};
    }
    return jobOf(*id);
}

void TestProcessingCoordinator::publishesMetadataFromTheWorker()
{
    const BookId book = importBook("title-page.pdf");
    m_fake->behavior = [](const QString& p, const std::atomic_bool&) { return success(p, QStringLiteral("Fake Title")); };
    ProcessingCoordinator c(*m_library, m_fake);
    QSignalSpy published(&c, &ProcessingCoordinator::metadataPublished);
    QThread* signalThread = nullptr;
    connect(&c, &ProcessingCoordinator::jobChanged, this, [&] { signalThread = QThread::currentThread(); });

    const JobRecord job = runToEnd(c, book);
    QVERIFY2(job.state == JobState::Succeeded, qPrintable(job.outcome + QStringLiteral(": ") + job.error));
    QCOMPARE(job.outcome, QStringLiteral("published"));
    QCOMPARE(job.attempt, 1);
    QVERIFY(job.run);
    QTRY_COMPARE_WITH_TIMEOUT(published.size(), 1, 5000);
    QVERIFY(m_fake->thread.load() != QThread::currentThread());  // SDK work off the GUI thread.
    QCOMPARE(signalThread, QThread::currentThread());            // Signals on the owner thread.

    const BookDetails d = detailsOf(book);
    QCOMPARE(*d.summary.metadata.title, QStringLiteral("Fake Title"));
    QCOMPARE(*d.summary.metadata.publicationYear, 2020);
    QCOMPARE(d.asset.pageCount, 3);  // Unknown at import, filled by the run.
    auto details = db([run = *job.run](QSqlDatabase& dd) { return catalog::metadataDetails(dd, run); }).value();
    QCOMPARE(details.size(), 1);
    QCOMPARE(details.first().alternatives.first().value, QStringLiteral("Runner-up"));
    QCOMPARE(details.first().evidence.first().reason, QStringLiteral("largest text on the title page"));

    // The immutable report is stored and referenced by the run.
    const QString report = m_root->filePath(storage::reportPath(*job.run));
    QFile f(report);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArrayLiteral("{\"kind\":\"fake\"}"));
}

void TestProcessingCoordinator::enqueueReturnsTheOpenJob()
{
    const BookId book = importBook("title-page.pdf");
    auto release = std::make_shared<std::atomic_bool>(false);
    m_fake->behavior = [release](const QString& p, const std::atomic_bool& cancel) {
        waitFor(*release, cancel, true);
        return success(p, QStringLiteral("T"));
    };
    ProcessingCoordinator c(*m_library, m_fake);
    c.enqueueMetadata(book);
    QTRY_VERIFY_WITH_TIMEOUT(m_fake->started.load(), 10000);
    const auto first = db([](QSqlDatabase& d) { return catalog::listJobs(d, true); }).value();
    QCOMPARE(first.size(), 1);
    const qint64 generation = detailsOf(book).metadataGeneration;

    auto again = db([book](QSqlDatabase& d) { return catalog::enqueueJob(d, book, JobKind::Metadata); });
    QVERIFY(again);
    QCOMPARE(again.value().id, first.first().id);           // The same open job...
    QCOMPARE(detailsOf(book).metadataGeneration, generation);  // ...and no new request generation.
    release->store(true);
    QTRY_COMPARE_WITH_TIMEOUT(jobOf(first.first().id).state, JobState::Succeeded, 10000);
    QCOMPARE(m_fake->calls.load(), 1);
}

void TestProcessingCoordinator::cancelStopsARunningJob()
{
    const BookId book = importBook("title-page.pdf");
    auto never = std::make_shared<std::atomic_bool>(false);
    m_fake->behavior = [never](const QString&, const std::atomic_bool& cancel) {
        waitFor(*never, cancel, true);
        MetadataExtraction r;
        r.status = MetadataExtraction::Status::Cancelled;
        return r;
    };
    ProcessingCoordinator c(*m_library, m_fake);
    c.enqueueMetadata(book);
    QTRY_VERIFY_WITH_TIMEOUT(m_fake->started.load(), 10000);
    const JobId id = db([](QSqlDatabase& d) { return catalog::listJobs(d, true); }).value().first().id;
    c.cancelJob(id);
    QTRY_COMPARE_WITH_TIMEOUT(jobOf(id).state, JobState::Cancelled, 10000);
    QVERIFY(!detailsOf(book).extracted);  // Nothing published.
    QVERIFY(reportFiles().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 5000);
}

// The SDK call ignores the flag and completes, but the job was cancelled
// meanwhile: the result must not be published.
void TestProcessingCoordinator::cancelWhileTheSdkCompletesDoesNotPublish()
{
    const BookId book = importBook("title-page.pdf");
    auto release = std::make_shared<std::atomic_bool>(false);
    m_fake->behavior = [release](const QString& p, const std::atomic_bool& cancel) {
        waitFor(*release, cancel, false);
        return success(p, QStringLiteral("Too Late"));
    };
    ProcessingCoordinator c(*m_library, m_fake);
    c.enqueueMetadata(book);
    QTRY_VERIFY_WITH_TIMEOUT(m_fake->started.load(), 10000);
    const JobId id = db([](QSqlDatabase& d) { return catalog::listJobs(d, true); }).value().first().id;
    c.cancelJob(id);
    QTRY_COMPARE_WITH_TIMEOUT(jobOf(id).state, JobState::CancelRequested, 5000);
    release->store(true);
    QTRY_COMPARE_WITH_TIMEOUT(jobOf(id).state, JobState::Cancelled, 10000);
    QVERIFY(!detailsOf(book).extracted);
    QVERIFY(reportFiles().isEmpty());  // The unpublished report was removed.
}

void TestProcessingCoordinator::correctionsMadeWhileRunningSurvive()
{
    const BookId book = importBook("title-page.pdf");
    auto release = std::make_shared<std::atomic_bool>(false);
    m_fake->behavior = [release](const QString& p, const std::atomic_bool& cancel) {
        waitFor(*release, cancel, true);
        return success(p, QStringLiteral("Extracted Title"));
    };
    ProcessingCoordinator c(*m_library, m_fake);
    c.enqueueMetadata(book);
    QTRY_VERIFY_WITH_TIMEOUT(m_fake->started.load(), 10000);
    QVERIFY(db([book](QSqlDatabase& d) {
        return catalog::setOverride(d, book, MetadataField::Title, MetadataOverride::withText(QStringLiteral("My Title")));
    }));
    QVERIFY(db([book](QSqlDatabase& d) {
        return catalog::setOverride(d, book, MetadataField::PublicationYear, MetadataOverride::cleared());
    }));
    release->store(true);
    const JobId id = db([](QSqlDatabase& d) { return catalog::listJobs(d, false); }).value().first().id;
    QTRY_COMPARE_WITH_TIMEOUT(jobOf(id).state, JobState::Succeeded, 10000);
    const BookDetails d = detailsOf(book);
    QCOMPARE(*d.summary.metadata.title, QStringLiteral("My Title"));  // The correction wins...
    QCOMPARE(*d.extracted->title, QStringLiteral("Extracted Title")); // ...the extraction is kept.
    QVERIFY(!d.summary.metadata.publicationYear);                     // Cleared stays cleared.
    QCOMPARE(d.summary.metadata.publicationYearSource, ValueSource::Cleared);
}

void TestProcessingCoordinator::trashedWhileRunningIsNotPublished()
{
    const BookId book = importBook("title-page.pdf");
    auto release = std::make_shared<std::atomic_bool>(false);
    m_fake->behavior = [release](const QString& p, const std::atomic_bool& cancel) {
        waitFor(*release, cancel, false);
        return success(p, QStringLiteral("T"));
    };
    ProcessingCoordinator c(*m_library, m_fake);
    c.enqueueMetadata(book);
    QTRY_VERIFY_WITH_TIMEOUT(m_fake->started.load(), 10000);
    QVERIFY(db([book](QSqlDatabase& d) { return catalog::trashBook(d, book); }));
    release->store(true);
    const JobId id = db([](QSqlDatabase& d) { return catalog::listJobs(d, false); }).value().first().id;
    QTRY_COMPARE_WITH_TIMEOUT(jobOf(id).state, JobState::Cancelled, 10000);
    QVERIFY(jobOf(id).outcome == QStringLiteral("trashed") || jobOf(id).outcome == QStringLiteral("superseded"));
    QVERIFY(!detailsOf(book).extracted);
    QCOMPARE(detailsOf(book).summary.lifecycle, Lifecycle::Trashed);
    QVERIFY(reportFiles().isEmpty());
}

void TestProcessingCoordinator::supersededResultIsNotPublished()
{
    const BookId book = importBook("title-page.pdf");
    auto release = std::make_shared<std::atomic_bool>(false);
    m_fake->behavior = [release](const QString& p, const std::atomic_bool& cancel) {
        waitFor(*release, cancel, false);
        return success(p, QStringLiteral("Old Request"));
    };
    ProcessingCoordinator c(*m_library, m_fake);
    c.enqueueMetadata(book);
    QTRY_VERIFY_WITH_TIMEOUT(m_fake->started.load(), 10000);
    // A newer request for the same component supersedes the running one.
    QVERIFY(db([book](QSqlDatabase& d) { return catalog::requestMetadataRun(d, book); }));
    release->store(true);
    const JobId id = db([](QSqlDatabase& d) { return catalog::listJobs(d, false); }).value().first().id;
    QTRY_COMPARE_WITH_TIMEOUT(jobOf(id).state, JobState::Cancelled, 10000);
    QCOMPARE(jobOf(id).outcome, QStringLiteral("superseded"));
    QVERIFY(!detailsOf(book).extracted);
    QVERIFY(reportFiles().isEmpty());
}

void TestProcessingCoordinator::failureKeepsEarlierResults()
{
    const BookId book = importBook("title-page.pdf");
    m_fake->behavior = [](const QString& p, const std::atomic_bool&) { return success(p, QStringLiteral("First")); };
    ProcessingCoordinator c(*m_library, m_fake);
    QCOMPARE(runToEnd(c, book).state, JobState::Succeeded);

    m_fake->behavior = [](const QString&, const std::atomic_bool&) {
        MetadataExtraction r;
        r.status = MetadataExtraction::Status::Failed;
        r.error = QStringLiteral("Unsupported (encrypted) PDF");
        return r;
    };
    const JobRecord failed = runToEnd(c, book);
    QCOMPARE(failed.state, JobState::Failed);
    QCOMPARE(failed.outcome, QStringLiteral("sdk_error"));
    QCOMPARE(failed.error, QStringLiteral("Unsupported (encrypted) PDF"));
    QCOMPARE(*detailsOf(book).summary.metadata.title, QStringLiteral("First"));  // Still available.
    QCOMPARE(reportFiles().size(), 1);
}

void TestProcessingCoordinator::sourceMismatchFails()
{
    const BookId book = importBook("title-page.pdf");
    m_fake->behavior = [](const QString& p, const std::atomic_bool&) {
        auto r = success(p, QStringLiteral("Wrong Bytes"));
        r.sourceSha256 = QString(64, u'0');
        return r;
    };
    ProcessingCoordinator c(*m_library, m_fake);
    const JobRecord job = runToEnd(c, book);
    QCOMPARE(job.state, JobState::Failed);
    QCOMPARE(job.outcome, QStringLiteral("source_mismatch"));
    QVERIFY(!detailsOf(book).extracted);
}

void TestProcessingCoordinator::restartRecoversInterruptedJobs()
{
    const BookId running = importBook("title-page.pdf");
    const BookId cancelling = importBook("contents-book.pdf");
    // A previous process claimed both jobs, requested cancel of one, then died.
    const auto claimed = db([running, cancelling](QSqlDatabase& d) {
        catalog::enqueueJob(d, running, JobKind::Metadata);
        catalog::enqueueJob(d, cancelling, JobKind::Metadata);
        auto a = catalog::claimNextJob(d).value();
        auto b = catalog::claimNextJob(d).value();
        const JobRecord& toCancel = a->book == cancelling ? *a : *b;
        catalog::requestJobCancel(d, toCancel.id);
        return qMakePair(*a, *b);
    });
    QCOMPARE(claimed.first.state, JobState::Running);
    restartLibrary();

    m_fake->behavior = [](const QString& p, const std::atomic_bool&) { return success(p, QStringLiteral("Recovered")); };
    ProcessingCoordinator c(*m_library, m_fake);
    const auto recovery = c.recover().result();
    QVERIFY(recovery);
    QCOMPARE(recovery.value().jobs.interrupted, 1);
    QCOMPARE(recovery.value().jobs.requeued, 1);
    QCOMPARE(recovery.value().jobs.cancelled, 1);
    c.start();
    QTRY_VERIFY_WITH_TIMEOUT(detailsOf(running).extracted.has_value(), 10000);
    QCOMPARE(*detailsOf(running).summary.metadata.title, QStringLiteral("Recovered"));
    QVERIFY(!detailsOf(cancelling).extracted);  // The cancelled one is not re-run.
    const auto all = db([](QSqlDatabase& d) { return catalog::listJobs(d, false); }).value();
    QStringList states;
    for (const JobRecord& j : all)
        states << toCode(j.state);
    states.sort();
    QCOMPARE(states, (QStringList{QStringLiteral("cancelled"), QStringLiteral("interrupted"), QStringLiteral("succeeded")}));
    // Recovery is idempotent.
    const auto again = c.recover().result().value();
    QCOMPARE(again.jobs.interrupted + again.jobs.requeued + again.jobs.cancelled, 0);
    QVERIFY(again.removedReports.isEmpty());  // The published report is kept.
}

// A crash between writing a report and publishing its run leaves a report
// no run references; recovery removes it and nothing else.
void TestProcessingCoordinator::recoveryRemovesOnlyUnpublishedReports()
{
    const BookId book = importBook("title-page.pdf");
    m_fake->behavior = [](const QString& p, const std::atomic_bool&) { return success(p, QStringLiteral("T")); };
    JobRecord published;
    {
        ProcessingCoordinator c(*m_library, m_fake);
        published = runToEnd(c, book);
        QCOMPARE(published.state, JobState::Succeeded);
    }
    const auto write = [this](const QString& name) {
        QFile f(m_root->filePath(QStringLiteral("reports/") + name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{}");
    };
    const RunId orphan = RunId::create();
    write(orphan.toString() + QStringLiteral(".json"));
    write(QStringLiteral("notes.json"));  // Not named by a run ID: not ours to remove.
    restartLibrary();

    ProcessingCoordinator c(*m_library, m_fake);
    const auto recovery = c.recover().result();
    QVERIFY(recovery);
    QCOMPARE(recovery.value().removedReports, QStringList{storage::reportPath(orphan)});
    QStringList expected{published.run->toString() + QStringLiteral(".json"), QStringLiteral("notes.json")};
    expected.sort();
    QCOMPARE(reportFiles(), expected);
}

void TestProcessingCoordinator::queuedJobOfTrashedBookNeverRuns()
{
    const BookId book = importBook("title-page.pdf");
    auto queued = db([book](QSqlDatabase& d) { return catalog::enqueueJob(d, book, JobKind::Metadata); });
    QVERIFY(queued);
    QVERIFY(db([book](QSqlDatabase& d) { return catalog::trashBook(d, book); }));
    m_fake->behavior = [](const QString& p, const std::atomic_bool&) { return success(p, QStringLiteral("T")); };
    ProcessingCoordinator c(*m_library, m_fake);
    QSignalSpy idle(&c, &ProcessingCoordinator::idle);
    c.start();
    QTRY_VERIFY_WITH_TIMEOUT(idle.size() >= 1, 10000);
    QCOMPARE(m_fake->calls.load(), 0);
    QCOMPARE(jobOf(queued.value().id).state, JobState::Cancelled);
    QCOMPARE(jobOf(queued.value().id).outcome, QStringLiteral("trashed"));
    // A trashed book cannot be queued at all.
    auto refused = db([book](QSqlDatabase& d) { return catalog::enqueueJob(d, book, JobKind::Metadata); });
    QVERIFY(!refused);
    QCOMPARE(refused.error().code, ErrorCode::Trashed);
}

void TestProcessingCoordinator::metadataJobsRunBeforeTocJobs()
{
    const BookId a = importBook("title-page.pdf");
    const BookId b = importBook("contents-book.pdf");
    const auto order = db([a, b](QSqlDatabase& d) {
        catalog::enqueueJob(d, a, JobKind::Toc);
        catalog::enqueueJob(d, b, JobKind::Metadata);
        catalog::enqueueJob(d, a, JobKind::Metadata);
        QList<JobKind> kinds;
        while (auto next = catalog::claimNextJob(d).value())
            kinds << next->kind;
        return kinds;
    });
    QCOMPARE(order, (QList<JobKind>{JobKind::Metadata, JobKind::Metadata, JobKind::Toc}));
}

// Closing the application is not a user cancel: the running job is
// requeued and the next session runs it.
void TestProcessingCoordinator::destructionDuringAJobInterruptsIt()
{
    const BookId book = importBook("title-page.pdf");
    auto never = std::make_shared<std::atomic_bool>(false);
    m_fake->behavior = [never](const QString&, const std::atomic_bool& cancel) {
        waitFor(*never, cancel, true);
        MetadataExtraction r;
        r.status = MetadataExtraction::Status::Cancelled;
        return r;
    };
    JobId id;
    {
        ProcessingCoordinator c(*m_library, m_fake);
        c.enqueueMetadata(book);
        QTRY_VERIFY_WITH_TIMEOUT(m_fake->started.load(), 10000);
        id = db([](QSqlDatabase& d) { return catalog::listJobs(d, true); }).value().first().id;
    }  // The destructor stops and waits; it must not hang.
    QCOMPARE(jobOf(id).state, JobState::Interrupted);
    const auto open = db([](QSqlDatabase& d) { return catalog::listJobs(d, true); }).value();
    QCOMPARE(open.size(), 1);
    QCOMPARE(open.first().state, JobState::Queued);
    QVERIFY(open.first().id != id);
}

// The documented close path: stop(), wait for !busy(), destroy. Nothing is
// lost: the running job is requeued, the queued one stays queued.
void TestProcessingCoordinator::stopKeepsWorkForRestart()
{
    const BookId a = importBook("title-page.pdf");
    const BookId b = importBook("contents-book.pdf");
    auto never = std::make_shared<std::atomic_bool>(false);
    m_fake->behavior = [never](const QString&, const std::atomic_bool& cancel) {
        waitFor(*never, cancel, true);
        MetadataExtraction r;
        r.status = MetadataExtraction::Status::Cancelled;
        return r;
    };
    {
        ProcessingCoordinator c(*m_library, m_fake);
        QSignalSpy idle(&c, &ProcessingCoordinator::idle);
        c.enqueueMetadata(a);
        c.enqueueMetadata(b);
        QTRY_VERIFY_WITH_TIMEOUT(m_fake->started.load(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(db([](QSqlDatabase& d) { return catalog::listJobs(d, true); }).value().size(), 2, 5000);
        c.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(idle.size() >= 1, 5000);
        QCOMPARE(m_fake->calls.load(), 1);  // Nothing more was claimed.
        c.start();                           // Stopped for good.
        QVERIFY(!c.busy());
    }
    QStringList states;
    const auto jobs = db([](QSqlDatabase& d) { return catalog::listJobs(d, false); }).value();
    for (const JobRecord& j : jobs)
        states << toCode(j.state);
    states.sort();
    QCOMPARE(states, (QStringList{QStringLiteral("interrupted"), QStringLiteral("queued"), QStringLiteral("queued")}));

    restartLibrary();
    m_fake->behavior = [](const QString& p, const std::atomic_bool&) { return success(p, QStringLiteral("After Restart")); };
    ProcessingCoordinator c(*m_library, m_fake);
    const auto rec = c.recover().result();
    QVERIFY(rec);
    QCOMPARE(rec.value().jobs.interrupted + rec.value().jobs.requeued + rec.value().jobs.cancelled, 0);
    c.start();
    QTRY_VERIFY_WITH_TIMEOUT(detailsOf(a).extracted && detailsOf(b).extracted, 10000);
    QCOMPARE(*detailsOf(a).summary.metadata.title, QStringLiteral("After Restart"));
}

// A result that completes although stop() was called is still published.
void TestProcessingCoordinator::stopDoesNotDiscardACompletedResult()
{
    const BookId book = importBook("title-page.pdf");
    auto release = std::make_shared<std::atomic_bool>(false);
    m_fake->behavior = [release](const QString& p, const std::atomic_bool& cancel) {
        waitFor(*release, cancel, false);
        return success(p, QStringLiteral("Finished Anyway"));
    };
    ProcessingCoordinator c(*m_library, m_fake);
    c.enqueueMetadata(book);
    QTRY_VERIFY_WITH_TIMEOUT(m_fake->started.load(), 10000);
    c.stop();
    release->store(true);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    const auto title = detailsOf(book).summary.metadata.title;
    QVERIFY(title.has_value());
    QCOMPARE(*title, QStringLiteral("Finished Anyway"));
}

// The user cancels, then asks again before the SDK reaches its next cancel
// checkpoint: the new request must run, not be absorbed by the dying job.
void TestProcessingCoordinator::retryWhileCancellingIsNotLost()
{
    const BookId book = importBook("title-page.pdf");
    auto release = std::make_shared<std::atomic_bool>(false);
    auto first = std::make_shared<std::atomic_bool>(true);
    m_fake->behavior = [release, first](const QString& p, const std::atomic_bool& cancel) {
        if (first->exchange(false)) {
            waitFor(*release, cancel, false);
            MetadataExtraction r;
            r.status = MetadataExtraction::Status::Cancelled;
            return r;
        }
        return success(p, QStringLiteral("Retried"));
    };
    ProcessingCoordinator c(*m_library, m_fake);
    QSignalSpy changed(&c, &ProcessingCoordinator::jobChanged);
    c.enqueueMetadata(book);
    QTRY_VERIFY_WITH_TIMEOUT(m_fake->started.load(), 10000);
    const JobId id = db([](QSqlDatabase& d) { return catalog::listJobs(d, true); }).value().first().id;
    c.cancelJob(id);
    QTRY_COMPARE_WITH_TIMEOUT(jobOf(id).state, JobState::CancelRequested, 5000);
    QTest::qWait(50);  // Let the cancel's own jobChanged arrive first.
    changed.clear();
    c.enqueueMetadata(book);  // Retry.
    QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(), 5000);
    const JobRecord retry = changed.first().at(0).value<JobRecord>();
    QVERIFY(retry.id != id);
    QCOMPARE(retry.state, JobState::Queued);
    release->store(true);
    QTRY_VERIFY_WITH_TIMEOUT(detailsOf(book).extracted.has_value(), 10000);
    QCOMPARE(*detailsOf(book).summary.metadata.title, QStringLiteral("Retried"));
    QCOMPARE(jobOf(id).state, JobState::Cancelled);
    QTRY_COMPARE_WITH_TIMEOUT(jobOf(retry.id).state, JobState::Succeeded, 5000);
}

// The worker claims the job after cancelJob() checked for a flag but before
// the cancel is recorded: the SDK call must still see the cancel.
void TestProcessingCoordinator::cancelRacingTheClaimReachesTheSdk()
{
    const BookId book = importBook("title-page.pdf");
    const JobId id = db([book](QSqlDatabase& d) { return catalog::enqueueJob(d, book, JobKind::Metadata); }).value().id;
    auto sawCancel = std::make_shared<std::atomic_bool>(false);
    m_fake->behavior = [sawCancel](const QString& p, const std::atomic_bool& cancel) {
        QElapsedTimer t;
        t.start();
        while (!cancel.load() && t.elapsed() < 5000)
            QThread::msleep(2);
        if (cancel.load()) {
            sawCancel->store(true);
            MetadataExtraction r;
            r.status = MetadataExtraction::Status::Cancelled;
            return r;
        }
        return success(p, QStringLiteral("Ran To The End"));
    };
    // Hold the database thread so the worker's claim and the cancel queue up
    // behind it in that order.
    auto gate = std::make_shared<std::atomic_bool>(false);
    m_library->run([gate](QSqlDatabase&) {
        while (!gate->load())
            QThread::msleep(1);
        return 0;
    });
    ProcessingCoordinator c(*m_library, m_fake);
    c.start();
    QTest::qWait(300);  // The worker posts its claim.
    c.cancelJob(id);    // No flag exists yet.
    gate->store(true);
    QTRY_COMPARE_WITH_TIMEOUT(jobOf(id).state, JobState::Cancelled, 10000);
    QCOMPARE(m_fake->calls.load(), 1);  // Claimed first, as intended.
    QVERIFY(sawCancel->load());          // Stopped early, not after 5 s.
    QVERIFY(!detailsOf(book).extracted);
}

void TestProcessingCoordinator::cancelAllCancelsRunningAndQueued()
{
    const BookId a = importBook("title-page.pdf");
    const BookId b = importBook("contents-book.pdf");
    auto never = std::make_shared<std::atomic_bool>(false);
    m_fake->behavior = [never](const QString&, const std::atomic_bool& cancel) {
        waitFor(*never, cancel, true);
        MetadataExtraction r;
        r.status = MetadataExtraction::Status::Cancelled;
        return r;
    };
    ProcessingCoordinator c(*m_library, m_fake);
    c.enqueueMetadata(a);
    c.enqueueMetadata(b);
    QTRY_VERIFY_WITH_TIMEOUT(m_fake->started.load(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(db([](QSqlDatabase& d) { return catalog::listJobs(d, true); }).value().size(), 2, 5000);
    c.cancelAll();
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(db([](QSqlDatabase& d) { return catalog::listJobs(d, true); }).value().isEmpty(), 5000);
    const auto jobs = db([](QSqlDatabase& d) { return catalog::listJobs(d, false); }).value();
    for (const JobRecord& j : jobs)
        QCOMPARE(j.state, JobState::Cancelled);
    QCOMPARE(m_fake->calls.load(), 1);
}

void TestProcessingCoordinator::extractorExceptionFailsTheJob()
{
    const BookId book = importBook("title-page.pdf");
    m_fake->behavior = [](const QString&, const std::atomic_bool&) -> MetadataExtraction {
        throw std::runtime_error("boom");
    };
    ProcessingCoordinator c(*m_library, m_fake);
    const JobRecord job = runToEnd(c, book);
    QCOMPARE(job.state, JobState::Failed);
    QCOMPARE(job.outcome, QStringLiteral("exception"));
    QVERIFY(job.error.contains(QStringLiteral("boom")));
    // The worker survives and runs the next request.
    m_fake->behavior = [](const QString& p, const std::atomic_bool&) { return success(p, QStringLiteral("Next")); };
    QCOMPARE(runToEnd(c, book).state, JobState::Succeeded);
}

QTEST_GUILESS_MAIN(TestProcessingCoordinator)
#include "tst_processingcoordinator.moc"
