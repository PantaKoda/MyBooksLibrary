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
using mbl::processing::ContentsAnalysis;
using mbl::processing::ContentsAnalyzer;
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

// Contents of a 3-page book: page 0 is a valid destination; "1.1 Scope" names
// a parent that is listed after it; "Index" is unresolved and left out of the
// plan with a reason; "Appendix" is ambiguous between two pages.
ContentsAnalysis sampleContents(const QString& path)
{
    ContentsAnalysis r;
    r.status = ContentsAnalysis::Status::Completed;
    r.sourceSha256 = storage::sha256OfFile(path).value_or(QString());
    r.pageCount = 3;
    r.toc.outcome = QStringLiteral("analysis_partial");
    r.toc.planReady = false;
    r.toc.parseComplete = true;
    r.toc.searchCoveredDocument = true;
    r.toc.planBlockers << QStringLiteral("2 entries have no confirmed page");
    r.toc.stopReasons << QStringLiteral("searched every page");
    r.toc.planJson = QByteArrayLiteral("{\"nodes\":[]}");
    auto entry = [](const char* id, int order, const char* title, HierarchyState h) {
        TocEntry e;
        e.sdkEntryId = QString::fromLatin1(id);
        e.order = order;
        e.title = QString::fromUtf8(title);
        e.hierarchy = h;
        e.sourceTocPage = 1;
        e.evidence.sourcePages = {1};
        return e;
    };
    TocEntry intro = entry("e0", 0, "1 Introduction", HierarchyState::Root);
    intro.printedLabel = QStringLiteral("1");
    intro.destinationState = DestinationState::Resolved;
    intro.destinationPage = 0;
    intro.inExportPlan = true;
    intro.evidence.destinationMethod = QStringLiteral("inferred_offset");
    intro.evidence.hierarchyReasons << QStringLiteral("Aligned to column root margin");
    TocEntry scope = entry("e1", 1, "1.1 Scope", HierarchyState::KnownParent);
    scope.parentSdkEntryId = QStringLiteral("e2");  // Listed after its child.
    scope.printedLabel = QStringLiteral("2");
    scope.destinationState = DestinationState::Resolved;
    scope.destinationPage = 1;
    scope.inExportPlan = true;
    TocEntry methods = entry("e2", 2, "2 Méthodes", HierarchyState::Root);
    methods.printedLabel = QStringLiteral("3");
    methods.destinationState = DestinationState::Resolved;
    methods.destinationPage = 2;
    methods.inExportPlan = true;
    TocEntry appendix = entry("e3", 3, "Appendix", HierarchyState::Root);
    appendix.printedLabel = QStringLiteral("A-1");
    appendix.destinationState = DestinationState::Ambiguous;
    appendix.evidence.alternativePages = {1, 2};
    appendix.evidence.destinationReasons << QStringLiteral("two pages carry the label");
    appendix.evidence.omissionReason = QStringLiteral("no confirmed page");
    TocEntry index = entry("e4", 4, "Index", HierarchyState::Unknown);
    index.printedLabel = QStringLiteral("xii");
    index.evidence.printedLabelUncertain = true;
    index.evidence.destinationReasons << QStringLiteral("no page shows the label xii");
    index.evidence.omissionReason = QStringLiteral("unresolved");
    r.toc.entries = {intro, scope, methods, appendix, index};
    r.reportJson = QByteArrayLiteral("{\"kind\":\"fake-analysis\"}");
    r.sdkVersion = QStringLiteral("fake");
    r.optionsJson = QStringLiteral("{}");
    r.outcome = r.toc.outcome;
    return r;
}

// Contents step with gates: a test holds a stage by clearing its release
// flag and observes the cancel flag the coordinator passes in.
class FakeAnalyzer : public ContentsAnalyzer {
public:
    std::atomic_int analyzeCalls{0};
    std::atomic_int bookCalls{0};
    std::atomic_bool releaseMetadata{true};
    std::atomic_bool releaseContents{true};
    std::atomic_bool inMetadataStage{false};
    std::atomic_bool inContentsStage{false};
    std::atomic_bool cancelSeenInMetadataStage{false};
    std::function<MetadataExtraction(const QString&)> metadata;
    std::function<ContentsAnalysis(const QString&)> contents = [](const QString& p) { return sampleContents(p); };

    ContentsAnalysis analyze(const QString& path, const std::atomic_bool& cancel, const Progress& progress) override
    {
        ++analyzeCalls;
        if (progress)
            progress(QStringLiteral("search"), 3);
        return contentsStage(path, cancel);
    }

    ContentsAnalysis analyzeBook(const QString& path, const std::atomic_bool& cancel, const Progress& progress,
                                 const MetadataReady& onMetadata) override
    {
        ++bookCalls;
        if (progress)
            progress(QStringLiteral("metadata"), 1);
        inMetadataStage = true;
        while (!releaseMetadata.load() && !cancel.load())
            QThread::msleep(2);
        cancelSeenInMetadataStage = cancel.load();
        inMetadataStage = false;
        if (cancel.load()) {
            MetadataExtraction m;
            m.status = MetadataExtraction::Status::Cancelled;
            onMetadata(m);
            ContentsAnalysis c;
            c.status = ContentsAnalysis::Status::Cancelled;
            return c;
        }
        onMetadata(metadata(path));
        if (progress)
            progress(QStringLiteral("search"), 2);
        return contentsStage(path, cancel);
    }

private:
    ContentsAnalysis contentsStage(const QString& path, const std::atomic_bool& cancel)
    {
        inContentsStage = true;
        while (!releaseContents.load() && !cancel.load())
            QThread::msleep(2);
        inContentsStage = false;
        if (cancel.load()) {
            ContentsAnalysis c;
            c.status = ContentsAnalysis::Status::Cancelled;
            return c;
        }
        return contents(path);
    }
};

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

    void pairedRunPublishesMetadataFirstThenContents();
    void cancellingOnlyTheContentsKeepsTheMetadata();
    void cancellingOnlyTheMetadataKeepsTheContents();
    void contentsOnlyJobUsesAnalyzeAndReportsProgress();
    void stopDuringContentsInterruptsOnlyContents();
    void rejectedContentsFailNotCancelled();
    void contentsAndEvidenceSurviveRestart();

private:
    template <typename Task>
    auto db(Task task) { return m_library->run(std::move(task)).result(); }
    // Metadata tests drop the contents job the import queues, so they see one
    // job per book; contents tests keep it.
    BookId importBook(const char* fixtureName, bool keepContentsJob = false);
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
    std::shared_ptr<FakeAnalyzer> m_analyzer;
    void dropJobs(const BookId& book)  // As if the book's earlier jobs had long finished.
    {
        QVERIFY(db([book](QSqlDatabase& d) {
            QSqlQuery q(d);
            q.prepare(QStringLiteral("DELETE FROM jobs WHERE book_id = ?"));
            q.addBindValue(book.toString());
            return q.exec();
        }));
    }
    JobRecord jobOfKind(const BookId& book, JobKind kind)
    {
        const auto jobs = db([](QSqlDatabase& d) { return catalog::listJobs(d, false, -1); }).value();
        for (const JobRecord& j : jobs) {  // Newest first.
            if (j.book == book && j.kind == kind)
                return j;
        }
        qFatal("no job of that kind");
        return {};
    }
};

void TestProcessingCoordinator::init()
{
    m_root = std::make_unique<QTemporaryDir>();
    auto opened = Library::open(m_root->path());
    QVERIFY2(opened, opened ? "" : qPrintable(opened.error().message));
    m_library = std::move(opened.value());
    m_fake = std::make_shared<FakeExtractor>();
    m_analyzer = std::make_shared<FakeAnalyzer>();
    m_analyzer->metadata = [](const QString& p) { return success(p, QStringLiteral("From The Book Run")); };
}

void TestProcessingCoordinator::cleanup()
{
    m_fake.reset();
    m_analyzer.reset();
    m_library.reset();
    m_root.reset();
}

BookId TestProcessingCoordinator::importBook(const char* fixtureName, bool keepContentsJob)
{
    storage::ImportService importer(*m_library);
    const auto r = importer.importFile(fixture(fixtureName));
    if (!r.book)
        qFatal("import failed: %s", qPrintable(r.error));
    if (!keepContentsJob) {
        const bool removed = db([book = *r.book](QSqlDatabase& d) {
            QSqlQuery q(d);
            q.prepare(QStringLiteral("DELETE FROM jobs WHERE book_id = ? AND kind = 'toc'"));
            q.addBindValue(book.toString());
            return q.exec();
        });
        if (!removed)
            qFatal("could not drop the contents job");
    }
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

// A book with both jobs queued is served by one analyzeBook call: the
// metadata is published as soon as its stage ends, before the contents.
void TestProcessingCoordinator::pairedRunPublishesMetadataFirstThenContents()
{
    const BookId book = importBook("title-page.pdf", true);
    m_analyzer->releaseContents = false;  // Hold the contents stage.
    m_fake->behavior = [](const QString& p, const std::atomic_bool&) { return success(p, QStringLiteral("Unused")); };
    ProcessingCoordinator c(*m_library, m_fake, m_analyzer);
    QSignalSpy metadataPublished(&c, &ProcessingCoordinator::metadataPublished);
    QSignalSpy contentsPublished(&c, &ProcessingCoordinator::contentsPublished);
    QSignalSpy progress(&c, &ProcessingCoordinator::jobProgress);
    c.start();
    QTRY_VERIFY_WITH_TIMEOUT(m_analyzer->inContentsStage.load(), 10000);
    // Early metadata: visible while the contents analysis still runs.
    QTRY_COMPARE_WITH_TIMEOUT(metadataPublished.size(), 1, 5000);
    QCOMPARE(*detailsOf(book).summary.metadata.title, QStringLiteral("From The Book Run"));
    QCOMPARE(jobOfKind(book, JobKind::Metadata).state, JobState::Succeeded);
    QCOMPARE(jobOfKind(book, JobKind::Toc).state, JobState::Running);
    QVERIFY(!detailsOf(book).toc);

    m_analyzer->releaseContents = true;
    QTRY_COMPARE_WITH_TIMEOUT(contentsPublished.size(), 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 5000);
    QCOMPARE(m_analyzer->bookCalls.load(), 1);  // One SDK call for both...
    QCOMPARE(m_analyzer->analyzeCalls.load(), 0);
    QCOMPARE(m_fake->calls.load(), 0);           // ...and no separate metadata call.
    const JobRecord toc = jobOfKind(book, JobKind::Toc);
    QCOMPARE(toc.state, JobState::Succeeded);
    QCOMPARE(toc.attempt, 1);
    const BookDetails d = detailsOf(book);
    QVERIFY(d.toc);
    QCOMPARE(d.toc->entries.size(), 5);  // Unresolved and omitted entries included.
    QCOMPARE(d.summary.tocEntryCount, 5);
    QCOMPARE(d.asset.pageCount, 3);
    QCOMPARE(reportFiles().size(), 2);  // One report per run.
    // Progress: the "metadata" stage belongs to the metadata job, the rest to the contents job.
    QVERIFY(progress.size() >= 2);
    QCOMPARE(progress.first().at(0).value<JobId>(), jobOfKind(book, JobKind::Metadata).id);
    QCOMPARE(progress.first().at(1).toString(), QStringLiteral("metadata"));
    QCOMPARE(progress.last().at(0).value<JobId>(), toc.id);
}

// Cancelling the contents job while the metadata stage runs must not cancel
// the shared SDK call before the metadata is in: the metadata is published,
// then the call stops and the contents job ends cancelled.
void TestProcessingCoordinator::cancellingOnlyTheContentsKeepsTheMetadata()
{
    const BookId book = importBook("title-page.pdf", true);
    m_analyzer->releaseMetadata = false;
    m_analyzer->releaseContents = false;
    ProcessingCoordinator c(*m_library, m_fake, m_analyzer);
    c.start();
    QTRY_VERIFY_WITH_TIMEOUT(m_analyzer->inMetadataStage.load(), 10000);
    const JobId toc = jobOfKind(book, JobKind::Toc).id;
    c.cancelJob(toc);
    QTRY_COMPARE_WITH_TIMEOUT(jobOf(toc).state, JobState::CancelRequested, 5000);
    QTest::qWait(100);
    QVERIFY(m_analyzer->inMetadataStage.load());  // The call was not cancelled.
    m_analyzer->releaseMetadata = true;
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    QVERIFY(!m_analyzer->cancelSeenInMetadataStage.load());
    QCOMPARE(jobOfKind(book, JobKind::Metadata).state, JobState::Succeeded);
    QCOMPARE(*detailsOf(book).summary.metadata.title, QStringLiteral("From The Book Run"));
    QCOMPARE(jobOf(toc).state, JobState::Cancelled);
    QVERIFY(!detailsOf(book).toc);
    QCOMPARE(reportFiles().size(), 1);
}

// Cancelling only the metadata job keeps the call running for the contents;
// the metadata result is discarded.
void TestProcessingCoordinator::cancellingOnlyTheMetadataKeepsTheContents()
{
    const BookId book = importBook("title-page.pdf", true);
    m_analyzer->releaseMetadata = false;
    ProcessingCoordinator c(*m_library, m_fake, m_analyzer);
    c.start();
    QTRY_VERIFY_WITH_TIMEOUT(m_analyzer->inMetadataStage.load(), 10000);
    const JobId metadata = jobOfKind(book, JobKind::Metadata).id;
    c.cancelJob(metadata);
    QTRY_COMPARE_WITH_TIMEOUT(jobOf(metadata).state, JobState::CancelRequested, 5000);
    QTest::qWait(100);
    m_analyzer->releaseMetadata = true;
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    QVERIFY(!m_analyzer->cancelSeenInMetadataStage.load());  // The shared call was not cancelled.
    QCOMPARE(jobOf(metadata).state, JobState::Cancelled);
    QVERIFY(!detailsOf(book).extracted);
    QCOMPARE(jobOfKind(book, JobKind::Toc).state, JobState::Succeeded);
    QCOMPARE(detailsOf(book).toc->entries.size(), 5);
}

void TestProcessingCoordinator::contentsOnlyJobUsesAnalyzeAndReportsProgress()
{
    const BookId book = importBook("title-page.pdf");
    dropJobs(book);  // Contents only: no metadata job to pair with.
    ProcessingCoordinator c(*m_library, m_fake, m_analyzer);
    QSignalSpy progress(&c, &ProcessingCoordinator::jobProgress);
    QSignalSpy contentsPublished(&c, &ProcessingCoordinator::contentsPublished);
    c.enqueueContents(book);
    QTRY_COMPARE_WITH_TIMEOUT(contentsPublished.size(), 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 5000);
    QCOMPARE(m_analyzer->analyzeCalls.load(), 1);
    QCOMPARE(m_analyzer->bookCalls.load(), 0);
    QCOMPARE(progress.size(), 1);
    QCOMPARE(progress.first().at(1).toString(), QStringLiteral("search"));
    QCOMPARE(progress.first().at(2).toInt(), 3);
    QVERIFY(!detailsOf(book).extracted);  // The metadata component is untouched.
}

// Closing after the metadata is in: the metadata stays published, the
// contents job is interrupted and queued again.
void TestProcessingCoordinator::stopDuringContentsInterruptsOnlyContents()
{
    const BookId book = importBook("title-page.pdf", true);
    m_analyzer->releaseContents = false;
    {
        ProcessingCoordinator c(*m_library, m_fake, m_analyzer);
        c.start();
        QTRY_VERIFY_WITH_TIMEOUT(m_analyzer->inContentsStage.load(), 10000);
        c.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    }
    QCOMPARE(jobOfKind(book, JobKind::Metadata).state, JobState::Succeeded);
    const auto jobs = db([](QSqlDatabase& d) { return catalog::listJobs(d, false, -1); }).value();
    QStringList toc;
    for (const JobRecord& j : jobs) {
        if (j.kind == JobKind::Toc)
            toc << toCode(j.state);
    }
    toc.sort();
    QCOMPARE(toc, (QStringList{QStringLiteral("interrupted"), QStringLiteral("queued")}));
}

// A contents result the catalog rejects (a destination past the last page)
// fails the job; it is not reported as a cancellation.
void TestProcessingCoordinator::rejectedContentsFailNotCancelled()
{
    const BookId book = importBook("title-page.pdf");
    dropJobs(book);  // Contents only: no metadata job to pair with.
    m_analyzer->contents = [](const QString& p) {
        ContentsAnalysis r = sampleContents(p);
        r.toc.entries[0].destinationPage = 7;  // The book has 3 pages.
        return r;
    };
    ProcessingCoordinator c(*m_library, m_fake, m_analyzer);
    c.enqueueContents(book);
    QTRY_VERIFY_WITH_TIMEOUT(!isOpen(jobOfKind(book, JobKind::Toc).state), 10000);
    const JobRecord job = jobOfKind(book, JobKind::Toc);
    QCOMPARE(job.state, JobState::Failed);
    QCOMPARE(job.outcome, QStringLiteral("publish_failed"));
    QVERIFY2(job.error.contains(QStringLiteral("beyond")), qPrintable(job.error));
    QVERIFY(!detailsOf(book).toc);
    QVERIFY(reportFiles().isEmpty());
}

void TestProcessingCoordinator::contentsAndEvidenceSurviveRestart()
{
    const BookId book = importBook("title-page.pdf", true);
    {
        ProcessingCoordinator c(*m_library, m_fake, m_analyzer);
        QSignalSpy contentsPublished(&c, &ProcessingCoordinator::contentsPublished);
        c.start();
        QTRY_COMPARE_WITH_TIMEOUT(contentsPublished.size(), 1, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 5000);
    }
    restartLibrary();
    const BookDetails d = detailsOf(book);
    QVERIFY(d.toc);
    const TocAnalysis& toc = *d.toc;
    QCOMPARE(toc.outcome, QStringLiteral("analysis_partial"));
    QVERIFY(!toc.planReady);
    QCOMPARE(toc.parseComplete, std::optional<bool>(true));
    QCOMPARE(toc.searchCoveredDocument, std::optional<bool>(true));
    QCOMPARE(toc.planBlockers, QStringList{QStringLiteral("2 entries have no confirmed page")});
    QCOMPARE(toc.stopReasons, QStringList{QStringLiteral("searched every page")});
    QCOMPARE(toc.planJson, QByteArrayLiteral("{\"nodes\":[]}"));
    QCOMPARE(toc.entries.size(), 5);
    const TocEntry& intro = toc.entries.at(0);
    QCOMPARE(intro.destinationPage, std::optional<int>(0));  // Page zero is a destination.
    QCOMPARE(intro.evidence.destinationMethod, std::optional<QString>(QStringLiteral("inferred_offset")));
    QCOMPARE(intro.evidence.hierarchyReasons, QStringList{QStringLiteral("Aligned to column root margin")});
    const TocEntry& scope = toc.entries.at(1);
    QCOMPARE(scope.hierarchy, HierarchyState::KnownParent);
    QCOMPARE(scope.parentSdkEntryId, std::optional<QString>(QStringLiteral("e2")));  // Parent after child.
    QCOMPARE(toc.entries.at(2).title, QStringLiteral("2 Méthodes"));
    const TocEntry& appendix = toc.entries.at(3);
    QCOMPARE(appendix.destinationState, DestinationState::Ambiguous);
    QVERIFY(!appendix.destinationPage);
    QCOMPARE(appendix.evidence.alternativePages, (QList<int>{1, 2}));
    QCOMPARE(appendix.evidence.omissionReason, std::optional<QString>(QStringLiteral("no confirmed page")));
    QVERIFY(!appendix.inExportPlan);
    const TocEntry& index = toc.entries.at(4);
    QCOMPARE(index.destinationState, DestinationState::Unresolved);
    QCOMPARE(index.printedLabel, std::optional<QString>(QStringLiteral("xii")));
    QVERIFY(index.evidence.printedLabelUncertain);
    QCOMPARE(index.evidence.sourcePages, QList<int>{1});
    QCOMPARE(index.sourceTocPage, std::optional<int>(1));
}

QTEST_GUILESS_MAIN(TestProcessingCoordinator)
#include "tst_processingcoordinator.moc"
