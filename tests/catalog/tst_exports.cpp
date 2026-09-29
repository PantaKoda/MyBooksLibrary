// A2 export records on a real library folder, with synthetic runs: an export
// request builds its plan from the effective contents (edits included) and
// queues a job; the record tells exactly whether a copy was written, also
// after cancels, the trash and a restart.
#include "catalog/catalog.h"
#include "catalog/exports.h"
#include "catalog/jobs.h"
#include "catalog/library.h"
#include "catalog/tocedits.h"

#include <QCryptographicHash>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace mbl::domain;
using mbl::catalog::Library;
namespace catalog = mbl::catalog;

namespace {

TocEntry entry(const QString& id, int order, const QString& title, std::optional<int> page,
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
    return e;
}

ExportOutput written()
{
    ExportOutput out;
    out.committed = true;
    out.outputSha256 = QString(64, u'b');
    out.outlineItems = 2;
    out.pageCount = 50;
    out.structureMatches = true;
    out.sourceUnchanged = true;
    out.sdkVersion = QStringLiteral("test");
    out.sdkPlanJson = QStringLiteral("{\"nodes\":[]}");
    return out;
}

} // namespace

class TestExports : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void requestBuildsThePlanFromTheEffectiveContents();
    void refusedRequests();
    void exportsAreClaimedFirst();
    void aCommittedCopyIsRecordedWhateverCameAfter();
    void copiesThatWereNotWritten();
    void restartDoesNotRequeueAnExport();
    void trashEndsExportsAndRestoreDoesNotResumeThem();
    void protectedFilesListEveryBook();

private:
    template <typename Task>
    auto db(Task task) { return m_library->run(std::move(task)).result(); }
    BookId addBook(const QString& fileName, int pages = 50);
    RunId publish(const BookId& book, const QList<TocEntry>& entries);
    Result<ExportRecord> request(const BookId& book, const QString& name = QStringLiteral("copy.pdf"))
    {
        const QString destination = m_out.filePath(name);
        return db([book, destination](QSqlDatabase& d) { return catalog::enqueueExport(d, book, destination, false); });
    }
    JobRecord claim()
    {
        auto claimed = db([](QSqlDatabase& d) { return catalog::claimNextJob(d); });
        if (!claimed || !claimed.value())
            qFatal("nothing to claim");
        return *claimed.value();
    }
    ExportRecord record(const JobId& id)
    {
        return db([id](QSqlDatabase& d) { return catalog::exportRecord(d, id); }).value();
    }
    JobRecord jobOf(const JobId& id) { return db([id](QSqlDatabase& d) { return catalog::job(d, id); }).value(); }
    void dropJobs()  // Only the jobs a test queues.
    {
        QVERIFY(db([](QSqlDatabase& d) { return QSqlQuery(d).exec(QStringLiteral("DELETE FROM jobs")); }));
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    QTemporaryDir m_out;
    std::unique_ptr<Library> m_library;
};

void TestExports::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    auto opened = Library::open(m_dir->path());
    QVERIFY2(opened, opened ? "" : qPrintable(opened.error().message));
    m_library = std::move(opened.value());
}

void TestExports::cleanup()
{
    m_library.reset();
    m_dir.reset();
}

BookId TestExports::addBook(const QString& fileName, int pages)
{
    NewBook b;
    b.asset.id = AssetId::create();
    b.asset.sha256 = QString::fromLatin1(QCryptographicHash::hash(fileName.toUtf8(), QCryptographicHash::Sha256).toHex());
    b.asset.byteSize = 1;
    b.asset.pageCount = pages;
    b.asset.managedPath = QStringLiteral("files/%1/source.pdf").arg(b.asset.id.toString());
    b.originalFileName = fileName;
    b.originalPath = QStringLiteral("C:/Downloads/") + fileName;
    auto id = db([b](QSqlDatabase& d) { return catalog::registerBook(d, b); });
    if (!id)
        qFatal("registerBook: %s", qPrintable(id.error().message));
    return id.value();
}

RunId TestExports::publish(const BookId& book, const QList<TocEntry>& entries)
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

void TestExports::requestBuildsThePlanFromTheEffectiveContents()
{
    const BookId book = addBook(QStringLiteral("book.pdf"));
    const RunId run = publish(book, {entry(QStringLiteral("a"), 0, QStringLiteral("Intro"), 0),
                                     entry(QStringLiteral("b"), 1, QStringLiteral("Chapter"), std::nullopt)});
    dropJobs();
    // The user gives "Chapter" its page: the edited revision is what gets bookmarked.
    TocEdit setPage;
    setPage.kind = TocEdit::Kind::SetPage;
    setPage.entryKey = QStringLiteral("b");
    setPage.page = 9;
    auto revision = db([book, run, setPage](QSqlDatabase& d) {
        return catalog::editToc(d, book, TocEditBase{run, std::nullopt}, {setPage});
    });
    QVERIFY2(revision, revision ? "" : qPrintable(revision.error().message));

    auto queued = request(book);
    QVERIFY2(queued, queued ? "" : qPrintable(queued.error().message));
    const ExportRecord& r = queued.value();
    QCOMPARE(r.book, book);
    QCOMPARE(r.destination, m_out.filePath(QStringLiteral("copy.pdf")));
    QVERIFY(!r.replaceExisting);
    QCOMPARE(r.tocRun, std::optional<RunId>(run));
    QCOMPARE(r.tocRevision, std::optional<TocRevisionId>(revision.value().id));
    QVERIFY(!r.committed);  // Not known yet.
    QCOMPARE(r.plan.pageCount, 50);
    QCOMPARE(r.plan.nodes.size(), 2);
    QCOMPARE(r.plan.nodes.at(1).page, 9);
    QVERIFY(r.plan.complete());

    const JobRecord job = jobOf(r.job);
    QCOMPARE(job.kind, JobKind::Export);
    QCOMPARE(job.state, JobState::Queued);
    QCOMPARE(job.sourceSha256, r.plan.sourceSha256);

    // Kept across a restart, with the plan's omissions and uncertain levels.
    const BookId partial = addBook(QStringLiteral("partial.pdf"));
    TocEntry unknown = entry(QStringLiteral("u"), 2, QStringLiteral("Loose"), 3);
    unknown.hierarchy = HierarchyState::Unknown;
    publish(partial, {entry(QStringLiteral("a"), 0, QStringLiteral("Intro"), 1),
                      entry(QStringLiteral("x"), 1, QStringLiteral("Index"), std::nullopt), unknown});
    auto second = request(partial, QStringLiteral("partial.pdf"));
    QVERIFY(second);
    m_library.reset();
    auto reopened = Library::open(m_dir->path());
    QVERIFY(reopened);
    m_library = std::move(reopened.value());
    const ExportRecord again = record(second.value().job);
    QCOMPARE(again.plan.nodes.size(), 2);
    QCOMPARE(again.plan.omitted.size(), 1);
    QCOMPARE(again.plan.omitted.at(0).title, QStringLiteral("Index"));
    QCOMPARE(again.plan.uncertainLevels.size(), 1);
    QVERIFY(!again.plan.complete());
    const auto list = db([partial](QSqlDatabase& d) { return catalog::bookExports(d, partial); });
    QVERIFY(list);
    QCOMPARE(list.value().size(), 1);
}

void TestExports::refusedRequests()
{
    const BookId noContents = addBook(QStringLiteral("new.pdf"));
    QCOMPARE(request(noContents).error().code, ErrorCode::InvalidArgument);

    const BookId noPages = addBook(QStringLiteral("unmapped.pdf"));
    publish(noPages, {entry(QStringLiteral("a"), 0, QStringLiteral("Intro"), std::nullopt)});
    QCOMPARE(request(noPages).error().code, ErrorCode::InvalidArgument);  // Nothing to bookmark.

    const BookId book = addBook(QStringLiteral("book.pdf"));
    publish(book, {entry(QStringLiteral("a"), 0, QStringLiteral("Intro"), 0)});
    QVERIFY(request(book));
    QCOMPARE(request(book, QStringLiteral("other.pdf")).error().code, ErrorCode::Duplicate);  // One at a time.
    QCOMPARE(db([book](QSqlDatabase& d) { return catalog::enqueueExport(d, book, QString(), false); }).error().code,
             ErrorCode::InvalidArgument);

    const BookId trashed = addBook(QStringLiteral("trashed.pdf"));
    publish(trashed, {entry(QStringLiteral("a"), 0, QStringLiteral("Intro"), 0)});
    QVERIFY(db([trashed](QSqlDatabase& d) { return catalog::trashBook(d, trashed); }));
    QCOMPARE(request(trashed).error().code, ErrorCode::Trashed);

    QCOMPARE(request(BookId::create()).error().code, ErrorCode::NotFound);
    // The metadata and contents queue does not take exports.
    QCOMPARE(db([book](QSqlDatabase& d) { return catalog::enqueueJob(d, book, JobKind::Export); }).error().code,
             ErrorCode::InvalidArgument);
}

void TestExports::exportsAreClaimedFirst()
{
    const BookId book = addBook(QStringLiteral("book.pdf"));
    publish(book, {entry(QStringLiteral("a"), 0, QStringLiteral("Intro"), 0)});
    dropJobs();
    QVERIFY(db([book](QSqlDatabase& d) { return catalog::enqueueJob(d, book, JobKind::Metadata); }));
    QVERIFY(db([book](QSqlDatabase& d) { return catalog::enqueueJob(d, book, JobKind::Toc); }));
    auto queued = request(book);
    QVERIFY(queued);
    const JobRecord first = claim();
    QCOMPARE(first.id, queued.value().job);
    QCOMPARE(first.state, JobState::Running);
    QCOMPARE(claim().kind, JobKind::Metadata);
}

void TestExports::aCommittedCopyIsRecordedWhateverCameAfter()
{
    const BookId book = addBook(QStringLiteral("book.pdf"));
    publish(book, {entry(QStringLiteral("a"), 0, QStringLiteral("Intro"), 0)});
    dropJobs();
    auto queued = request(book);
    QVERIFY(queued);
    const JobRecord job = claim();
    // The user cancels, and the book goes to Trash, while the SDK commits.
    QCOMPARE(db([id = job.id](QSqlDatabase& d) { return catalog::requestJobCancel(d, id); }).value().state,
             JobState::CancelRequested);
    QVERIFY(db([book](QSqlDatabase& d) { return catalog::trashBook(d, book); }));
    QVERIFY(db([id = job.id](QSqlDatabase& d) {
        return catalog::finishExportJob(d, id, JobState::Cancelled, QStringLiteral("cancelled"), QString(), written());
    }));
    const JobRecord done = jobOf(job.id);
    QCOMPARE(done.state, JobState::Succeeded);
    QCOMPARE(done.outcome, QStringLiteral("written"));
    QVERIFY(!done.run);
    const ExportRecord r = record(job.id);
    QCOMPARE(r.committed, std::optional<bool>(true));
    QCOMPARE(r.output.outputSha256, QString(64, u'b'));
    QCOMPARE(r.output.outlineItems, 2);
    QVERIFY(r.output.structureMatches && r.output.sourceUnchanged);
    QCOMPARE(r.output.sdkPlanJson, QStringLiteral("{\"nodes\":[]}"));
    QVERIFY(r.finishedAt.isValid());
    // Closed: it cannot be closed again.
    QVERIFY(!db([id = job.id](QSqlDatabase& d) {
        return catalog::finishExportJob(d, id, JobState::Failed, QStringLiteral("x"), QString(), ExportOutput{});
    }));
}

void TestExports::copiesThatWereNotWritten()
{
    const BookId book = addBook(QStringLiteral("book.pdf"));
    publish(book, {entry(QStringLiteral("a"), 0, QStringLiteral("Intro"), 0)});
    dropJobs();

    // Cancelled while queued: never ran, so nothing was written.
    auto queued = request(book);
    QVERIFY(queued);
    QVERIFY(db([id = queued.value().job](QSqlDatabase& d) { return catalog::requestJobCancel(d, id); }));
    QCOMPARE(record(queued.value().job).committed, std::optional<bool>(false));

    // Failed by the SDK.
    auto failing = request(book, QStringLiteral("b.pdf"));
    QVERIFY(failing);
    const JobRecord job = claim();
    QVERIFY(!record(job.id).committed);  // Running: not known yet.
    // A copy that wrote nothing cannot be recorded as succeeded.
    QVERIFY(!db([id = job.id](QSqlDatabase& d) {
        return catalog::finishExportJob(d, id, JobState::Succeeded, QStringLiteral("written"), QString(), ExportOutput{});
    }));
    ExportOutput nothing;
    nothing.sdkVersion = QStringLiteral("test");
    QVERIFY(db([id = job.id, nothing](QSqlDatabase& d) {
        return catalog::finishExportJob(d, id, JobState::Failed, QStringLiteral("output_exists"),
                                        QStringLiteral("A file with that name already exists."), nothing);
    }));
    const JobRecord failed = jobOf(job.id);
    QCOMPARE(failed.state, JobState::Failed);
    QCOMPARE(failed.outcome, QStringLiteral("output_exists"));
    const ExportRecord r = record(job.id);
    QCOMPARE(r.committed, std::optional<bool>(false));
    QVERIFY(r.output.outputSha256.isEmpty());
    QCOMPARE(r.output.sdkVersion, QStringLiteral("test"));

    // A metadata job is not closed as an export.
    QVERIFY(db([book](QSqlDatabase& d) { return catalog::enqueueJob(d, book, JobKind::Metadata); }));
    const JobRecord metadata = claim();
    QVERIFY(!db([id = metadata.id](QSqlDatabase& d) {
        return catalog::finishExportJob(d, id, JobState::Failed, QStringLiteral("x"), QString(), ExportOutput{});
    }));
}

void TestExports::restartDoesNotRequeueAnExport()
{
    const BookId book = addBook(QStringLiteral("book.pdf"));
    publish(book, {entry(QStringLiteral("a"), 0, QStringLiteral("Intro"), 0)});
    const BookId other = addBook(QStringLiteral("other.pdf"));
    publish(other, {entry(QStringLiteral("a"), 0, QStringLiteral("Intro"), 0)});
    dropJobs();
    auto running = request(book);
    QVERIFY(running);
    QCOMPARE(claim().id, running.value().job);
    auto waiting = request(other, QStringLiteral("other.pdf"));
    QVERIFY(waiting);

    // The process died while the SDK wrote the copy.
    m_library.reset();
    auto reopened = Library::open(m_dir->path());
    QVERIFY(reopened);
    m_library = std::move(reopened.value());
    auto recovery = db([](QSqlDatabase& d) { return catalog::recoverJobs(d); });
    QVERIFY(recovery);
    QCOMPARE(recovery.value().interrupted, 1);
    QCOMPARE(recovery.value().requeued, 0);

    const JobRecord interrupted = jobOf(running.value().job);
    QCOMPARE(interrupted.state, JobState::Interrupted);
    QVERIFY(interrupted.error.contains(QStringLiteral("may or may not")));
    QVERIFY(!record(running.value().job).committed);  // Not known: the file may exist.
    const auto open = db([](QSqlDatabase& d) { return catalog::listJobs(d, true, -1); }).value();
    QCOMPARE(open.size(), 1);  // The waiting export stays queued, and nothing else.
    QCOMPARE(open.first().id, waiting.value().job);
}

void TestExports::trashEndsExportsAndRestoreDoesNotResumeThem()
{
    const BookId book = addBook(QStringLiteral("book.pdf"));
    publish(book, {entry(QStringLiteral("a"), 0, QStringLiteral("Intro"), 0)});
    dropJobs();
    auto queued = request(book);
    QVERIFY(queued);
    QVERIFY(db([book](QSqlDatabase& d) { return catalog::trashBook(d, book); }));
    const JobRecord ended = jobOf(queued.value().job);
    QCOMPARE(ended.state, JobState::Cancelled);
    QCOMPARE(ended.outcome, QStringLiteral("trashed"));
    QCOMPARE(record(queued.value().job).committed, std::optional<bool>(false));

    QVERIFY(db([book](QSqlDatabase& d) { return catalog::restoreBook(d, book); }));
    const auto open = db([](QSqlDatabase& d) { return catalog::listJobs(d, true, -1); }).value();
    for (const JobRecord& j : open)
        QVERIFY(j.kind != JobKind::Export);  // The user asks again, with a destination.

    // Running when trashed, then not written: the trash stays the reason.
    auto second = request(book);
    QVERIFY(second);
    const JobRecord job = claim();
    QCOMPARE(job.id, second.value().job);
    QVERIFY(db([book](QSqlDatabase& d) { return catalog::trashBook(d, book); }));
    QVERIFY(db([id = job.id](QSqlDatabase& d) {
        return catalog::finishExportJob(d, id, JobState::Cancelled, QStringLiteral("cancelled"), QString(), ExportOutput{});
    }));
    QCOMPARE(jobOf(job.id).outcome, QStringLiteral("trashed"));
    QCOMPARE(record(job.id).committed, std::optional<bool>(false));
}

void TestExports::protectedFilesListEveryBook()
{
    const BookId a = addBook(QStringLiteral("a.pdf"));
    const BookId b = addBook(QStringLiteral("b.pdf"));
    QVERIFY(db([b](QSqlDatabase& d) { return catalog::trashBook(d, b); }));  // Trashed books too.
    auto files = db([](QSqlDatabase& d) { return catalog::protectedFiles(d); });
    QVERIFY(files);
    QCOMPARE(files.value().managedPaths.size(), 2);
    for (const QString& path : files.value().managedPaths)
        QVERIFY(path.startsWith(QStringLiteral("files/")));
    QCOMPARE(QSet<QString>(files.value().originalPaths.cbegin(), files.value().originalPaths.cend()),
             (QSet<QString>{QStringLiteral("C:/Downloads/a.pdf"), QStringLiteral("C:/Downloads/b.pdf")}));
    Q_UNUSED(a);
}

QTEST_GUILESS_MAIN(TestExports)
#include "tst_exports.moc"
