// A4 + A2 + A1: export jobs with a fake exporter, so cancels, shutdown and
// restarts are deterministic. The real SDK path is tst_sdkbookexporter.
// Destinations are validated when requested and again before the write; a
// committed copy is recorded as written whatever happens after the commit.
#include "catalog/catalog.h"
#include "catalog/exports.h"
#include "catalog/jobs.h"
#include "catalog/library.h"
#include "processing/processingcoordinator.h"
#include "storage/filecopy.h"
#include "storage/importservice.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <functional>

using namespace mbl::domain;
using mbl::catalog::Library;
using mbl::processing::BookExporter;
using mbl::processing::ExportResult;
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

// These tests queue no metadata work; a call would be a mistake.
class NoExtractor : public MetadataExtractor {
public:
    MetadataExtraction extract(const QString&, const std::atomic_bool&) override
    {
        MetadataExtraction r;
        r.status = MetadataExtraction::Status::Failed;
        r.error = QStringLiteral("not used");
        return r;
    }
};

class FakeExporter : public BookExporter {
public:
    using Behavior = std::function<ExportResult(const QString& output, const std::atomic_bool& cancel)>;
    Behavior behavior;
    std::atomic_int calls{0};
    std::atomic_bool started{false};
    std::atomic<QThread*> thread{nullptr};
    QString source;
    QString output;
    ExportPlan plan;
    bool replaceExisting = false;

    ExportResult exportCopy(const QString& sourcePath, const QString& outputPath, const ExportPlan& p, bool replace,
                            const std::atomic_bool& cancel) override
    {
        thread = QThread::currentThread();
        source = sourcePath;
        output = outputPath;
        plan = p;
        replaceExisting = replace;
        ++calls;
        started = true;
        return behavior(outputPath, cancel);
    }
};

ExportResult committedCopy(const QString& output)
{
    QFile f(output);
    if (!f.open(QIODevice::WriteOnly) || f.write("%PDF-1.7 bookmarked\n") < 0)
        qFatal("cannot write the fake copy");
    f.close();
    ExportResult r;
    r.status = ExportResult::Status::Committed;
    r.committed = true;
    r.output = output;
    r.outputSha256 = storage::sha256OfFile(output).value_or(QString());
    r.outlineItems = 2;
    r.pageCount = 27;
    r.structureMatches = true;
    r.sourceUnchanged = true;
    r.planJson = QStringLiteral("{\"nodes\":[]}");
    r.sdkVersion = QStringLiteral("fake");
    return r;
}

ExportResult cancelled()
{
    ExportResult r;
    r.status = ExportResult::Status::Cancelled;
    r.error = QStringLiteral("Cancelled.");
    r.errorCode = QStringLiteral("Cancelled");
    r.sdkVersion = QStringLiteral("fake");
    return r;
}

void waitFor(const std::atomic_bool& flag)
{
    while (!flag.load())
        QThread::msleep(2);
}

} // namespace

class TestExportJobs : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void writesTheCopyOnTheWorkerAndRecordsIt();
    void refusedRequestsQueueNothing();
    void theDestinationIsCheckedAgainBeforeTheWrite();
    void aCancelAfterTheCommitStillRecordsTheCopy();
    void aCancelBeforeTheCommitWritesNothing();
    void stopDuringAnExportDoesNotRequeueIt();
    void sdkRefusalsAreRecorded();
    void withoutAnExporterExportsAreUnsupported();

private:
    template <typename Task>
    auto db(Task task) { return m_library->run(std::move(task)).result(); }
    ProcessingCoordinator* coordinator(bool withExporter = true)
    {
        m_coordinator = std::make_unique<ProcessingCoordinator>(
            *m_library, std::make_shared<NoExtractor>(), nullptr,
            withExporter ? std::static_pointer_cast<BookExporter>(m_exporter) : nullptr);
        return m_coordinator.get();
    }
    JobRecord jobOf(const JobId& id) { return db([id](QSqlDatabase& d) { return catalog::job(d, id); }).value(); }
    ExportRecord recordOf(const JobId& id)
    {
        return db([id](QSqlDatabase& d) { return catalog::exportRecord(d, id); }).value();
    }
    // Enqueues through the coordinator and waits for the export to end.
    ExportRecord exportToEnd(const QString& destination, bool replace = false);
    JobId startExport(const QString& destination);  // Enqueues and waits until the exporter runs.
    QList<JobRecord> exportJobs()
    {
        QList<JobRecord> out;
        const auto all = db([](QSqlDatabase& d) { return catalog::listJobs(d, false, -1); });  // Kept alive for the loop.
        for (const JobRecord& j : all.value()) {
            if (j.kind == JobKind::Export)
                out << j;
        }
        return out;
    }

    std::unique_ptr<QTemporaryDir> m_root;
    std::unique_ptr<QTemporaryDir> m_out;
    std::unique_ptr<Library> m_library;
    std::unique_ptr<ProcessingCoordinator> m_coordinator;
    std::shared_ptr<FakeExporter> m_exporter;
    BookId m_book;
    QString m_managed;
    QString m_managedSha;
};

void TestExportJobs::init()
{
    m_root = std::make_unique<QTemporaryDir>();
    m_out = std::make_unique<QTemporaryDir>();
    auto opened = Library::open(m_root->path());
    QVERIFY2(opened, opened ? "" : qPrintable(opened.error().message));
    m_library = std::move(opened.value());
    m_exporter = std::make_shared<FakeExporter>();
    m_exporter->behavior = [](const QString& output, const std::atomic_bool&) { return committedCopy(output); };

    storage::ImportService importer(*m_library);
    const auto imported = importer.importFile(fixture("contents-book.pdf"));
    QVERIFY2(imported.book, qPrintable(imported.error));
    m_book = *imported.book;
    // Contents published (a 27-page book), and no metadata or contents work left.
    auto run = db([book = m_book](QSqlDatabase& d) -> Result<RunId> {
        QSqlQuery q(d);
        if (!q.exec(QStringLiteral("DELETE FROM jobs")) || !q.exec(QStringLiteral("UPDATE assets SET page_count = 27")))
            return makeError(ErrorCode::Database, QStringLiteral("setup"));
        auto ticket = catalog::requestTocRun(d, book);
        if (!ticket)
            return ticket.error();
        TocEntry intro;
        intro.sdkEntryId = QStringLiteral("a");
        intro.order = 0;
        intro.title = QStringLiteral("Introduction");
        intro.hierarchy = HierarchyState::Root;
        intro.destinationState = DestinationState::Resolved;
        intro.destinationPage = 0;
        TocEntry index = intro;
        index.sdkEntryId = QStringLiteral("b");
        index.order = 1;
        index.title = QStringLiteral("Index");
        index.destinationState = DestinationState::Unresolved;
        index.destinationPage.reset();
        TocAnalysis toc{QStringLiteral("analysis_partial"), false, {intro, index}};
        const RunIdentity identity{ticket.value().sourceSha256, QStringLiteral("test"), QString(), QStringLiteral("{}"),
                                   toc.outcome, std::nullopt};
        auto published = catalog::publishToc(d, ticket.value(), identity, toc);
        if (!published)
            return published;
        if (!q.exec(QStringLiteral("DELETE FROM jobs")))
            return makeError(ErrorCode::Database, QStringLiteral("setup"));
        return published;
    });
    QVERIFY2(run, run ? "" : qPrintable(run.error().message));
    const auto details = db([book = m_book](QSqlDatabase& d) { return catalog::bookDetails(d, book); }).value();
    m_managed = QDir(m_root->path()).filePath(details.asset.managedPath);
    m_managedSha = details.asset.sha256;
}

void TestExportJobs::cleanup()
{
    m_coordinator.reset();
    m_exporter.reset();
    m_library.reset();
    m_out.reset();
    m_root.reset();
}

ExportRecord TestExportJobs::exportToEnd(const QString& destination, bool replace)
{
    QSignalSpy finished(m_coordinator.get(), &ProcessingCoordinator::exportFinished);
    QSignalSpy refused(m_coordinator.get(), &ProcessingCoordinator::exportRefused);
    m_coordinator->enqueueExport(m_book, destination, replace);
    if (!QTest::qWaitFor([&] { return !finished.isEmpty() || !refused.isEmpty(); }, 20000) || finished.isEmpty()) {
        QTest::qFail(refused.isEmpty() ? "the export did not finish"
                                       : qPrintable(refused.first().at(1).toString()),
                     __FILE__, __LINE__);
        return {};
    }
    return finished.first().at(0).value<ExportRecord>();
}

JobId TestExportJobs::startExport(const QString& destination)
{
    QSignalSpy queued(m_coordinator.get(), &ProcessingCoordinator::exportQueued);
    m_coordinator->enqueueExport(m_book, destination);
    if (!QTest::qWaitFor([&] { return !queued.isEmpty() && m_exporter->started.load(); }, 20000)) {
        QTest::qFail("the export did not start", __FILE__, __LINE__);
        return {};
    }
    return queued.first().at(0).value<ExportRecord>().job;
}

void TestExportJobs::writesTheCopyOnTheWorkerAndRecordsIt()
{
    auto* c = coordinator();
    QSignalSpy queued(c, &ProcessingCoordinator::exportQueued);
    QSignalSpy jobs(c, &ProcessingCoordinator::jobChanged);
    const QString destination = m_out->filePath(QStringLiteral("Βιβλίο (bookmarked).pdf"));
    const ExportRecord r = exportToEnd(destination);

    QCOMPARE(queued.size(), 1);
    QVERIFY(m_exporter->thread.load() != QThread::currentThread());  // The worker, not the GUI thread.
    QCOMPARE(QDir::cleanPath(m_exporter->source), QDir::cleanPath(m_managed));
    QCOMPARE(m_exporter->output, destination);
    QVERIFY(!m_exporter->replaceExisting);
    // The plan from the effective contents: one bookmark, "Index" omitted.
    QCOMPARE(m_exporter->plan.sourceSha256, m_managedSha);
    QCOMPARE(m_exporter->plan.pageCount, 27);
    QCOMPARE(m_exporter->plan.nodes.size(), 1);
    QCOMPARE(m_exporter->plan.omitted.size(), 1);

    QCOMPARE(r.committed, std::optional<bool>(true));
    QCOMPARE(r.destination, destination);
    QCOMPARE(r.output.outputSha256, storage::sha256OfFile(destination).value_or(QString()));
    QCOMPARE(r.output.outlineItems, 2);
    QVERIFY(!r.plan.complete());  // Partial coverage stays visible on the record.
    const JobRecord job = jobOf(r.job);
    QCOMPARE(job.state, JobState::Succeeded);
    QCOMPARE(job.outcome, QStringLiteral("written"));
    QTRY_VERIFY([&] {
        for (const auto& args : jobs) {
            if (args.at(0).value<JobRecord>().state == JobState::Succeeded)
                return true;
        }
        return false;
    }());
    QCOMPARE(storage::sha256OfFile(m_managed).value_or(QString()), m_managedSha);  // The source is untouched.
}

void TestExportJobs::refusedRequestsQueueNothing()
{
    auto* c = coordinator();
    m_exporter->behavior = [](const QString&, const std::atomic_bool&) {
        ExportResult r;
        r.status = ExportResult::Status::Failed;
        r.error = QStringLiteral("must not be called");
        return r;
    };
    const QString existing = m_out->filePath(QStringLiteral("done.pdf"));
    QFile f(existing);
    QVERIFY(f.open(QIODevice::WriteOnly) && f.write("x") == 1);
    f.close();
    const QList<std::pair<QString, bool>> refusedDestinations{
        {m_root->filePath(QStringLiteral("inside.pdf")), false},  // Inside the library.
        {m_managed, true},                                        // The managed source, even when replacing.
        {fixture("contents-book.pdf"), true},                     // The imported original, even when replacing.
        {existing, false},                                        // An existing file, unless replacing.
        {m_out->filePath(QStringLiteral("book.txt")), false},
    };
    QSignalSpy refused(c, &ProcessingCoordinator::exportRefused);
    QSignalSpy queued(c, &ProcessingCoordinator::exportQueued);
    for (const auto& [destination, replace] : refusedDestinations)
        c->enqueueExport(m_book, destination, replace);
    QTRY_COMPARE_WITH_TIMEOUT(refused.size(), refusedDestinations.size(), 10000);
    for (const auto& args : refused) {
        QCOMPARE(args.at(0).value<BookId>(), m_book);
        QVERIFY(!args.at(1).toString().isEmpty());
    }
    QVERIFY(queued.isEmpty());
    QVERIFY(exportJobs().isEmpty());
    QCOMPARE(m_exporter->calls.load(), 0);

    // A book without contents to bookmark.
    QVERIFY(db([book = m_book](QSqlDatabase& d) {
        QSqlQuery q(d);
        q.prepare(QStringLiteral("UPDATE books SET active_toc_run_id = NULL WHERE id = ?"));
        q.addBindValue(book.toString());
        return q.exec();
    }));
    c->enqueueExport(m_book, m_out->filePath(QStringLiteral("new.pdf")));
    QTRY_COMPARE_WITH_TIMEOUT(refused.size(), refusedDestinations.size() + 1, 10000);
    QVERIFY(exportJobs().isEmpty());
}

void TestExportJobs::theDestinationIsCheckedAgainBeforeTheWrite()
{
    // Requested while the name was free (as the coordinator would), then a
    // file of that name appears before the worker gets to it.
    const QString destination = m_out->filePath(QStringLiteral("late.pdf"));
    auto queued = db([book = m_book, destination](QSqlDatabase& d) {
        return catalog::enqueueExport(d, book, destination, false);
    });
    QVERIFY(queued);
    QFile f(destination);
    QVERIFY(f.open(QIODevice::WriteOnly) && f.write("someone else's file") > 0);
    f.close();

    auto* c = coordinator();
    QSignalSpy finished(c, &ProcessingCoordinator::exportFinished);
    c->start();
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
    QCOMPARE(m_exporter->calls.load(), 0);  // Never reached the SDK.
    const JobRecord job = jobOf(queued.value().job);
    QCOMPARE(job.state, JobState::Failed);
    QCOMPARE(job.outcome, QStringLiteral("output_exists"));
    QCOMPARE(recordOf(job.id).committed, std::optional<bool>(false));
    QFile kept(destination);
    QVERIFY(kept.open(QIODevice::ReadOnly));
    QCOMPARE(kept.readAll(), QByteArray("someone else's file"));
}

void TestExportJobs::aCancelAfterTheCommitStillRecordsTheCopy()
{
    auto* c = coordinator();
    std::atomic_bool cancelSeen{false};
    m_exporter->behavior = [&](const QString& output, const std::atomic_bool& cancel) {
        waitFor(cancel);  // The SDK is past its last check: it commits anyway.
        cancelSeen = true;
        return committedCopy(output);
    };
    QSignalSpy finished(c, &ProcessingCoordinator::exportFinished);
    const QString destination = m_out->filePath(QStringLiteral("late cancel.pdf"));
    const JobId id = startExport(destination);
    c->cancelJob(id);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
    QVERIFY(cancelSeen.load());
    const JobRecord job = jobOf(id);
    QCOMPARE(job.state, JobState::Succeeded);  // Not "cancelled": the file exists.
    QCOMPARE(job.outcome, QStringLiteral("written"));
    QCOMPARE(recordOf(id).committed, std::optional<bool>(true));
    QVERIFY(QFile::exists(destination));
}

void TestExportJobs::aCancelBeforeTheCommitWritesNothing()
{
    auto* c = coordinator();
    m_exporter->behavior = [](const QString&, const std::atomic_bool& cancel) {
        waitFor(cancel);
        return cancelled();
    };
    QSignalSpy finished(c, &ProcessingCoordinator::exportFinished);
    const QString destination = m_out->filePath(QStringLiteral("cancelled.pdf"));
    const JobId id = startExport(destination);
    c->cancelJob(id);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
    const JobRecord job = jobOf(id);
    QCOMPARE(job.state, JobState::Cancelled);
    QCOMPARE(recordOf(id).committed, std::optional<bool>(false));
    QVERIFY(!QFile::exists(destination));
}

void TestExportJobs::stopDuringAnExportDoesNotRequeueIt()
{
    auto* c = coordinator();
    m_exporter->behavior = [](const QString&, const std::atomic_bool& cancel) {
        waitFor(cancel);
        return cancelled();
    };
    const JobId id = startExport(m_out->filePath(QStringLiteral("closing.pdf")));
    c->stop();
    QTRY_VERIFY_WITH_TIMEOUT(!c->busy(), 10000);
    const JobRecord job = jobOf(id);
    QCOMPARE(job.state, JobState::Interrupted);
    QVERIFY(job.error.contains(QStringLiteral("before the copy was written")));
    QCOMPARE(recordOf(id).committed, std::optional<bool>(false));  // Known: the SDK stopped before its commit.

    // The next session does not write it by itself.
    m_coordinator.reset();
    m_library.reset();
    auto reopened = Library::open(m_root->path());
    QVERIFY(reopened);
    m_library = std::move(reopened.value());
    auto* next = coordinator();
    QVERIFY(next->recover().result());
    for (const JobRecord& j : exportJobs())
        QVERIFY(!isOpen(j.state));
    QCOMPARE(m_exporter->calls.load(), 1);
}

void TestExportJobs::sdkRefusalsAreRecorded()
{
    auto* c = coordinator();
    m_exporter->behavior = [](const QString&, const std::atomic_bool&) {
        ExportResult r;
        r.status = ExportResult::Status::Failed;
        r.error = QStringLiteral("The bookmarks could not be prepared.");
        r.errorCode = QStringLiteral("InvalidPlan");
        r.planIssues = QStringList{QStringLiteral("x: destination out of range")};
        r.sdkVersion = QStringLiteral("fake");
        return r;
    };
    const ExportRecord r = exportToEnd(m_out->filePath(QStringLiteral("invalid.pdf")));
    QCOMPARE(r.committed, std::optional<bool>(false));
    QCOMPARE(r.output.sdkVersion, QStringLiteral("fake"));
    const JobRecord job = jobOf(r.job);
    QCOMPARE(job.state, JobState::Failed);
    QCOMPARE(job.outcome, QStringLiteral("invalid_plan"));
    QVERIFY(job.error.contains(QStringLiteral("destination out of range")));
    Q_UNUSED(c);
}

void TestExportJobs::withoutAnExporterExportsAreUnsupported()
{
    coordinator(false);
    const ExportRecord r = exportToEnd(m_out->filePath(QStringLiteral("none.pdf")));
    QCOMPARE(jobOf(r.job).outcome, QStringLiteral("unsupported"));
    QCOMPARE(r.committed, std::optional<bool>(false));
    QCOMPARE(m_exporter->calls.load(), 0);
}

QTEST_GUILESS_MAIN(TestExportJobs)
#include "tst_exportjobs.moc"
