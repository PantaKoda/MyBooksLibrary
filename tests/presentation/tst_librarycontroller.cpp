// Presentation: the library session opens, recovers and imports off the GUI
// thread, runs metadata jobs, and updates its models only on the GUI thread.
#include "app/libraryroot.h"
#include "catalog/catalog.h"
#include "catalog/jobs.h"
#include "catalog/library.h"
#include "presentation/booklistmodel.h"
#include "presentation/joblistmodel.h"
#include "presentation/librarycontroller.h"
#include "processing/contentsanalyzer.h"
#include "processing/metadataextractor.h"
#include "storage/filecopy.h"
#include "storage/importservice.h"

#include <QAbstractItemModelTester>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

using mbl::presentation::BookListModel;
using mbl::presentation::JobListModel;
using mbl::presentation::LibraryController;
using mbl::processing::ContentsAnalysis;
using mbl::processing::MetadataExtraction;

namespace {

QString fixture(const char* name)
{
    return QDir(QStringLiteral(MBL_FIXTURES_DIR)).filePath(QLatin1StringView(name));
}

QStringList titles(BookListModel* model)
{
    QStringList out;
    for (int row = 0; row < model->rowCount(); ++row)
        out << model->data(model->index(row), BookListModel::TitleRole).toString();
    out.sort();
    return out;
}

// Metadata extraction stand-in: `title` for every book, or a run that waits
// until cancelled while `block` is set.
class FakeExtractor : public mbl::processing::MetadataExtractor {
public:
    std::atomic_bool block{false};
    std::atomic_int calls{0};
    std::atomic_bool started{false};
    QString title = QStringLiteral("Extracted Title");

    MetadataExtraction extract(const QString& path, const std::atomic_bool& cancel) override
    {
        ++calls;
        started = true;
        MetadataExtraction r;
        while (block.load() && !cancel.load())
            QThread::msleep(2);
        if (cancel.load()) {
            r.status = MetadataExtraction::Status::Cancelled;
            return r;
        }
        r.status = MetadataExtraction::Status::Completed;
        r.sourceSha256 = mbl::storage::sha256OfFile(path).value_or(QString());
        r.pageCount = 3;
        r.metadata.titleStatus = mbl::domain::FieldStatus::Resolved;
        r.metadata.title = title;
        r.reportJson = QByteArrayLiteral("{}");
        r.sdkVersion = QStringLiteral("fake");
        r.optionsJson = QStringLiteral("{}");
        r.outcome = QStringLiteral("completed");
        return r;
    }
};

// Contents step: its metadata stage is the FakeExtractor (so `block` holds a
// paired run there), then two entries, one of them unresolved.
class FakeAnalyzer : public mbl::processing::ContentsAnalyzer {
public:
    explicit FakeAnalyzer(std::shared_ptr<FakeExtractor> metadata) : m_metadata(std::move(metadata)) {}
    std::atomic_int analyzeCalls{0};
    std::atomic_int bookCalls{0};

    ContentsAnalysis analyze(const QString& path, const std::atomic_bool& cancel, const Progress&) override
    {
        ++analyzeCalls;
        return contents(path, cancel);
    }
    ContentsAnalysis analyzeBook(const QString& path, const std::atomic_bool& cancel, const Progress&,
                                 const MetadataReady& onMetadata) override
    {
        ++bookCalls;
        onMetadata(m_metadata->extract(path, cancel));
        return contents(path, cancel);
    }

private:
    static ContentsAnalysis contents(const QString& path, const std::atomic_bool& cancel)
    {
        ContentsAnalysis r;
        if (cancel.load()) {
            r.status = ContentsAnalysis::Status::Cancelled;
            return r;
        }
        r.status = ContentsAnalysis::Status::Completed;
        r.sourceSha256 = mbl::storage::sha256OfFile(path).value_or(QString());
        r.pageCount = 3;
        r.toc.outcome = QStringLiteral("analysis_partial");
        mbl::domain::TocEntry first;
        first.sdkEntryId = QStringLiteral("e0");
        first.title = QStringLiteral("1 Introduction");
        first.hierarchy = mbl::domain::HierarchyState::Root;
        first.destinationState = mbl::domain::DestinationState::Resolved;
        first.destinationPage = 0;
        mbl::domain::TocEntry second;
        second.sdkEntryId = QStringLiteral("e1");
        second.order = 1;
        second.title = QStringLiteral("Index");
        r.toc.entries = {first, second};
        r.reportJson = QByteArrayLiteral("{}");
        r.sdkVersion = QStringLiteral("fake");
        r.optionsJson = QStringLiteral("{}");
        r.outcome = r.toc.outcome;
        return r;
    }
    std::shared_ptr<FakeExtractor> m_metadata;
};

// The row of the newest job of that kind ("Title and authors" or "Contents").
int rowOfKind(JobListModel* model, const QString& kindText)
{
    for (int row = 0; row < model->rowCount(); ++row) {
        if (model->data(model->index(row), JobListModel::KindTextRole).toString() == kindText)
            return row;
    }
    return -1;
}

QString stateOf(BookListModel* model, int row)
{
    return model->data(model->index(row), BookListModel::ProcessingStateRole).toString();
}

QVariant jobData(JobListModel* model, int row, int role)
{
    return model->data(model->index(row), role);
}

} // namespace

class TestLibraryController : public QObject {
    Q_OBJECT

private slots:
    void opensEmptyLibraryOffTheGuiThread();
    void importsListAndSurviveRestart();
    void duplicatesAndFailuresAreReported();
    void filesQueuedWhileOpeningAreImported();
    void cancelDropsQueuedFiles();
    void startupRecoversInterruptedImport();
    void secondSessionOnSameLibraryFails();
    void libraryRootResolution();
    void filesAddedAsBatchEndsAreImported();
    void filesAddedWhileCancellingAreImported();
    void failedOpenReleasesQueuedFiles();
    void setBooksAppliesRowChangesWithoutReset();
    void importedBookGetsItsExtractedTitle();
    void closingDuringExtractionResumesNextSession();
    void cancelAndRetryFromTheActivityList();
    void startupRequeuesACrashedJob();
    void withoutAnExtractorJobsWait();
    void pendingCountIncludesTheWholeBacklog();
    void refreshesAreCoalesced();

private:
    void openAndWait(LibraryController& c, const QString& root)
    {
        c.open(root);
        QTRY_VERIFY_WITH_TIMEOUT(c.ready() || c.failed(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    }
};

void TestLibraryController::opensEmptyLibraryOffTheGuiThread()
{
    QTemporaryDir dir;
    LibraryController c;
    // booksRefreshed is emitted right after setBooks() in the same continuation.
    QThread* modelThreadAtReset = nullptr;
    connect(&c, &LibraryController::booksRefreshed, this,
            [&] { modelThreadAtReset = QThread::currentThread(); }, Qt::DirectConnection);
    c.open(dir.path());
    QVERIFY(c.opening());
    QVERIFY(c.busy());  // open() returned at once: the work runs elsewhere.
    QTRY_VERIFY_WITH_TIMEOUT(c.ready(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    QCOMPARE(c.books()->rowCount(), 0);
    QCOMPARE(modelThreadAtReset, QThread::currentThread());  // Model updated on the GUI thread.
    QCOMPARE(c.books()->thread(), QThread::currentThread());
    QVERIFY(QFile::exists(QDir(dir.path()).filePath(QStringLiteral("library.sqlite"))));
    QCOMPARE(c.libraryPath(), QDir::toNativeSeparators(QDir(dir.path()).absolutePath()));
}

void TestLibraryController::importsListAndSurviveRestart()
{
    QTemporaryDir dir;
    QTemporaryDir outside;
    const QString a = QDir(outside.path()).filePath(QStringLiteral("Βιβλίο ένα.pdf"));
    const QString b = QDir(outside.path()).filePath(QStringLiteral("scan_0042.pdf"));
    QVERIFY(QFile::copy(fixture("title-page.pdf"), a));
    QVERIFY(QFile::copy(fixture("contents-book.pdf"), b));
    {
        LibraryController c;
        openAndWait(c, dir.path());
        QSignalSpy finished(&c, &LibraryController::importsFinished);
        c.importUrls({QUrl::fromLocalFile(a), QUrl::fromLocalFile(b)});
        QVERIFY(c.importing());
        QCOMPARE(c.importTotal(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
        QCOMPARE(c.lastBatch().imported, 2);
        QCOMPARE(c.books()->rowCount(), 2);
        QCOMPARE(titles(c.books()), (QStringList{QStringLiteral("scan_0042"), QStringLiteral("Βιβλίο ένα")}));
        const QModelIndex first = c.books()->index(0);
        QVERIFY(c.books()->data(first, BookListModel::TitleFromFileNameRole).toBool());
        QVERIFY(!c.books()->data(first, BookListModel::ProcessingStateRole).toString().isEmpty());
        const QString id = c.books()->data(first, BookListModel::BookIdRole).toString();
        QCOMPARE(c.books()->rowOfBook(id), 0);
        QCOMPARE(c.books()->bookIdAt(0), id);
        QVERIFY(c.statusText().contains(QStringLiteral("2 imported")));
        QVERIFY(c.problems().isEmpty());
    }
    // Restart: a new session sees the same books.
    LibraryController again;
    openAndWait(again, dir.path());
    QCOMPARE(again.books()->rowCount(), 2);
}

void TestLibraryController::duplicatesAndFailuresAreReported()
{
    QTemporaryDir dir;
    QTemporaryDir outside;
    const QString copy = QDir(outside.path()).filePath(QStringLiteral("same bytes.pdf"));
    const QString notPdf = QDir(outside.path()).filePath(QStringLiteral("notes.pdf"));
    QVERIFY(QFile::copy(fixture("title-page.pdf"), copy));
    {
        QFile f(notPdf);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("plain text");
    }
    LibraryController c;
    openAndWait(c, dir.path());
    QSignalSpy finished(&c, &LibraryController::importsFinished);
    c.importFiles({fixture("title-page.pdf"), copy, notPdf});
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
    QCOMPARE(c.lastBatch().imported, 1);
    QCOMPARE(c.lastBatch().duplicates, 1);
    QCOMPARE(c.lastBatch().failed, 1);
    QCOMPARE(c.problems().size(), 2);
    QVERIFY(c.problems().join(u'\n').contains(QStringLiteral("already in the library")));
    QVERIFY(c.problems().join(u'\n').contains(QStringLiteral("not a PDF")));
    QTRY_COMPARE_WITH_TIMEOUT(c.books()->rowCount(), 1, 10000);
}

void TestLibraryController::filesQueuedWhileOpeningAreImported()
{
    QTemporaryDir dir;
    LibraryController c;
    QSignalSpy finished(&c, &LibraryController::importsFinished);
    c.open(dir.path());
    c.importFiles({fixture("title-page.pdf")});  // Before the library is ready.
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
    QCOMPARE(c.lastBatch().imported, 1);
    QTRY_COMPARE_WITH_TIMEOUT(c.books()->rowCount(), 1, 10000);
}

void TestLibraryController::cancelDropsQueuedFiles()
{
    QTemporaryDir dir;
    LibraryController c;
    QSignalSpy finished(&c, &LibraryController::importsFinished);
    c.open(dir.path());
    // Queued while opening, cancelled before any file starts.
    c.importFiles({fixture("title-page.pdf"), fixture("contents-book.pdf")});
    c.cancelImports();
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
    QCOMPARE(c.lastBatch().cancelled, 2);
    QCOMPARE(c.lastBatch().imported, 0);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    QCOMPARE(c.books()->rowCount(), 0);
    // The session keeps working afterwards.
    c.importFiles({fixture("title-page.pdf")});
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 20000);
    QCOMPARE(c.lastBatch().imported, 1);
}

void TestLibraryController::startupRecoversInterruptedImport()
{
    QTemporaryDir dir;
    {
        auto library = mbl::catalog::Library::open(dir.path());
        QVERIFY(library);
        mbl::storage::ImportService service(*library.value());
        service.setCrashHook([](mbl::storage::ImportStage s) { return s == mbl::storage::ImportStage::Installed; });
        QCOMPARE(service.importFile(fixture("contents-book.pdf")).outcome,
                 mbl::storage::ImportResult::Outcome::Interrupted);
    }
    LibraryController c;
    openAndWait(c, dir.path());
    QCOMPARE(c.books()->rowCount(), 1);  // Completed by recovery during open.
    QVERIFY2(c.statusText().contains(QStringLiteral("completed")), qPrintable(c.statusText()));
}

void TestLibraryController::secondSessionOnSameLibraryFails()
{
    QTemporaryDir dir;
    LibraryController first;
    openAndWait(first, dir.path());
    LibraryController second;
    second.open(dir.path());
    QTRY_VERIFY_WITH_TIMEOUT(second.failed(), 10000);
    QVERIFY(second.statusText().contains(QStringLiteral("already open")));
    second.importFiles({fixture("title-page.pdf")});  // Ignored when failed.
    QVERIFY(!second.busy());
}

void TestLibraryController::libraryRootResolution()
{
    QTemporaryDir dir;
    const auto fromArg = mbl::app::resolveLibraryRoot({QStringLiteral("app"), QStringLiteral("--library"), dir.path()});
    QCOMPARE(fromArg.source, QStringLiteral("command line"));
    QCOMPARE(fromArg.path, QDir::cleanPath(dir.path()));

    qputenv("MYBOOKSLIBRARY_ROOT", dir.path().toUtf8());
    const auto fromEnv = mbl::app::resolveLibraryRoot({QStringLiteral("app")});
    QCOMPARE(fromEnv.source, QStringLiteral("environment"));
    qunsetenv("MYBOOKSLIBRARY_ROOT");

    QCoreApplication::setOrganizationName(QStringLiteral("MyBooksLibrary"));
    QCoreApplication::setApplicationName(QStringLiteral("MyBooksLibrary"));
    const auto fallback = mbl::app::resolveLibraryRoot({QStringLiteral("app")});
    QCOMPARE(fallback.source, QStringLiteral("default"));
    QVERIFY(fallback.path.endsWith(QStringLiteral("/Library")));
    QVERIFY(fallback.path.contains(QStringLiteral("MyBooksLibrary")));
}

// PR #7 review, finding 1: files added after the worker's last empty-queue
// check but before onBatchFinished() ran used to deadlock the GUI thread
// (startBatch() re-locked m_queueMutex).
void TestLibraryController::filesAddedAsBatchEndsAreImported()
{
    QTemporaryDir dir;
    LibraryController c;
    openAndWait(c, dir.path());
    QSignalSpy finished(&c, &LibraryController::importsFinished);
    bool added = false;
    connect(&c, &LibraryController::importProgressChanged, this, [&] {
        if (added || c.importTotal() != 1 || c.importDone() != 1)
            return;  // onFileFinished of the only file.
        added = true;
        QThread::msleep(300);  // The worker sees an empty queue and posts onBatchFinished.
        c.importFiles({fixture("contents-book.pdf")});
    });
    c.importFiles({fixture("title-page.pdf")});
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);  // Hung before the fix.
    QVERIFY(added);
    QCOMPARE(c.lastBatch().imported, 2);
    QTRY_COMPARE_WITH_TIMEOUT(c.books()->rowCount(), 2, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
}

// Finding 2: files added while "Cancelling..." used to be discarded silently.
void TestLibraryController::filesAddedWhileCancellingAreImported()
{
    QTemporaryDir dir;
    LibraryController c;
    openAndWait(c, dir.path());
    QSignalSpy finished(&c, &LibraryController::importsFinished);
    bool added = false;
    connect(&c, &LibraryController::importProgressChanged, this, [&] {
        if (added || c.currentFile().isEmpty())
            return;  // onFileStarted of the first file.
        added = true;
        c.cancelImports();
        c.importFiles({fixture("contents-book.pdf")});
    });
    c.importFiles({fixture("title-page.pdf")});
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
    QVERIFY(added);
    const auto b = c.lastBatch();
    QCOMPARE(b.imported + b.cancelled, 2);  // Before the fix: 1 -- the second file vanished.
    QVERIFY(b.imported >= 1);               // The file added after the cancel was imported.
    QTRY_COMPARE_WITH_TIMEOUT(c.books()->rowCount(), b.imported, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
}

// Finding 3: files queued while opening used to stay pending forever when the
// open failed, so busy/importing never cleared.
void TestLibraryController::failedOpenReleasesQueuedFiles()
{
    QTemporaryDir dir;
    LibraryController first;
    openAndWait(first, dir.path());
    LibraryController second;
    QSignalSpy finished(&second, &LibraryController::importsFinished);
    second.open(dir.path());
    second.importFiles({fixture("title-page.pdf")});  // Accepted while opening, as --import does.
    QTRY_VERIFY_WITH_TIMEOUT(second.failed(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!second.busy(), 10000);
    QVERIFY(!second.importing());
    QCOMPARE(second.problems().size(), 1);
    QVERIFY(second.problems().first().contains(QStringLiteral("could not be opened")));
    QCOMPARE(finished.size(), 1);
    QCOMPARE(second.lastBatch().failed, 1);
    QVERIFY(second.statusText().contains(QStringLiteral("already open")));  // The reason stays visible.
}

// Finding 4: refreshes must not reset the model (a reset scrolls views back
// to the top); rows change individually, keyed by book ID.
void TestLibraryController::setBooksAppliesRowChangesWithoutReset()
{
    using mbl::domain::BookId;
    using mbl::domain::BookSummary;
    const auto book = [](const BookId& id, const QString& title, qint64 revision) {
        BookSummary b;
        b.id = id;
        b.displayTitle = title;
        b.revision = revision;
        return b;
    };
    const BookId a = BookId::create(), b = BookId::create(), c = BookId::create();

    BookListModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy moved(&model, &QAbstractItemModel::rowsMoved);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy counts(&model, &BookListModel::countChanged);

    model.setBooks({book(a, QStringLiteral("A"), 1), book(b, QStringLiteral("B"), 1)});
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(inserted.size(), 2);

    // One more book (what each imported file causes): one insertion, nothing else.
    inserted.clear();
    model.setBooks({book(a, QStringLiteral("A"), 1), book(b, QStringLiteral("B"), 1), book(c, QStringLiteral("C"), 1)});
    QCOMPARE(inserted.size(), 1);
    QCOMPARE(inserted.first().at(1).toInt(), 2);
    QCOMPARE(changed.size(), 0);  // Unchanged revisions: no dataChanged.

    // A removed, a changed revision.
    model.setBooks({book(a, QStringLiteral("A2"), 2), book(c, QStringLiteral("C"), 1)});
    QCOMPARE(removed.size(), 1);
    QCOMPARE(changed.size(), 1);
    QCOMPARE(model.data(model.index(0), BookListModel::TitleRole).toString(), QStringLiteral("A2"));

    // Reordering moves rows.
    model.setBooks({book(c, QStringLiteral("C"), 1), book(a, QStringLiteral("A2"), 2)});
    QCOMPARE(moved.size(), 1);
    QCOMPARE(model.bookIdAt(0), c.toString());
    QCOMPARE(model.bookIdAt(1), a.toString());

    model.setBooks({});
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(resets.size(), 0);  // Never a reset.
    QCOMPARE(counts.size(), 4);  // 0->2, 2->3, 3->2, 2->0; the reorder kept the count.
}

void TestLibraryController::importedBookGetsItsExtractedTitle()
{
    QTemporaryDir dir;
    auto fake = std::make_shared<FakeExtractor>();
    auto analyzer = std::make_shared<FakeAnalyzer>(fake);
    LibraryController c;
    c.setProcessors(fake, analyzer, false);
    QVERIFY(c.processingAvailable());
    QVERIFY(!c.ocrAvailable());  // Shown as a capability note.
    QAbstractItemModelTester booksTester(c.books());
    QAbstractItemModelTester jobsTester(c.jobs());
    QThread* jobModelThread = nullptr;
    connect(c.jobs(), &QAbstractItemModel::dataChanged, this,
            [&] { jobModelThread = QThread::currentThread(); }, Qt::DirectConnection);
    openAndWait(c, dir.path());
    c.importFiles({fixture("title-page.pdf")});
    QTRY_COMPARE_WITH_TIMEOUT(titles(c.books()), QStringList{QStringLiteral("Extracted Title")}, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(stateOf(c.books(), 0), QStringLiteral("Metadata ready \u00b7 2 contents entries"), 5000);
    QVERIFY(!c.books()->data(c.books()->index(0), BookListModel::TitleFromFileNameRole).toBool());
    QCOMPARE(c.jobs()->rowCount(), 2);  // Title and authors, and contents.
    for (int row = 0; row < 2; ++row) {
        QCOMPARE(jobData(c.jobs(), row, JobListModel::StateRole).toString(), QStringLiteral("succeeded"));
        QCOMPARE(jobData(c.jobs(), row, JobListModel::BookTitleRole).toString(), QStringLiteral("Extracted Title"));
        QVERIFY(!jobData(c.jobs(), row, JobListModel::CanRetryRole).toBool());
    }
    QCOMPARE(c.jobs()->pendingCount(), 0);
    QCOMPARE(analyzer->bookCalls.load(), 1);  // Both jobs served by one run.
    QCOMPARE(fake->calls.load(), 1);
    QCOMPARE(jobModelThread, QThread::currentThread());
}

// Closing is not cancelling: the running jobs are interrupted and requeued,
// the waiting ones stay queued, and the next session runs them all.
void TestLibraryController::closingDuringExtractionResumesNextSession()
{
    QTemporaryDir dir;
    auto fake = std::make_shared<FakeExtractor>();
    auto analyzer = std::make_shared<FakeAnalyzer>(fake);
    fake->block = true;
    {
        LibraryController c;
        c.setProcessors(fake, analyzer, true);
        openAndWait(c, dir.path());
        c.importFiles({fixture("title-page.pdf"), fixture("contents-book.pdf")});
        QTRY_VERIFY_WITH_TIMEOUT(fake->started.load(), 20000);
        QTRY_COMPARE_WITH_TIMEOUT(c.books()->rowCount(), 2, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(c.jobs()->pendingCount(), 4, 10000);
        QVERIFY(c.busy());
        QElapsedTimer closing;
        closing.start();
        c.prepareToClose();
        QVERIFY(c.closing());
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
        QVERIFY2(closing.elapsed() < 5000, "closing waited too long");
        QCOMPARE(fake->calls.load(), 1);
    }
    fake->block = false;
    LibraryController again;
    again.setProcessors(fake, analyzer, true);
    openAndWait(again, dir.path());
    QTRY_COMPARE_WITH_TIMEOUT(titles(again.books()),
                              (QStringList{QStringLiteral("Extracted Title"), QStringLiteral("Extracted Title")}), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!again.busy(), 10000);
    QStringList states;
    for (int row = 0; row < again.jobs()->rowCount(); ++row)
        states << jobData(again.jobs(), row, JobListModel::StateRole).toString();
    states.sort();
    QCOMPARE(states, (QStringList{QStringLiteral("interrupted"), QStringLiteral("interrupted"),
                                  QStringLiteral("succeeded"), QStringLiteral("succeeded"),
                                  QStringLiteral("succeeded"), QStringLiteral("succeeded")}));
}

// Cancelling only "Title and authors" while the shared run reads the first
// pages: the run continues for the contents, the metadata result is dropped,
// and Retry then extracts the metadata on its own.
void TestLibraryController::cancelAndRetryFromTheActivityList()
{
    QTemporaryDir dir;
    auto fake = std::make_shared<FakeExtractor>();
    auto analyzer = std::make_shared<FakeAnalyzer>(fake);
    fake->block = true;
    LibraryController c;
    c.setProcessors(fake, analyzer, true);
    openAndWait(c, dir.path());
    c.importFiles({fixture("title-page.pdf")});
    QTRY_VERIFY_WITH_TIMEOUT(fake->started.load(), 20000);
    const QString metadataKind = QStringLiteral("Title and authors");
    QTRY_COMPARE_WITH_TIMEOUT(
        jobData(c.jobs(), rowOfKind(c.jobs(), metadataKind), JobListModel::StateRole).toString(),
        QStringLiteral("running"), 10000);
    QVERIFY(jobData(c.jobs(), rowOfKind(c.jobs(), metadataKind), JobListModel::CanCancelRole).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(stateOf(c.books(), 0), QStringLiteral("Reading title and authors\u2026 \u00b7 analyzing contents\u2026"),
                              5000);
    const QString metadataJob = jobData(c.jobs(), rowOfKind(c.jobs(), metadataKind), JobListModel::JobIdRole).toString();

    c.cancelJob(metadataJob);
    QTRY_COMPARE_WITH_TIMEOUT(
        jobData(c.jobs(), rowOfKind(c.jobs(), metadataKind), JobListModel::StateRole).toString(),
        QStringLiteral("cancel_requested"), 10000);
    fake->block = false;  // The shared run goes on for the contents.
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    QCOMPARE(jobData(c.jobs(), rowOfKind(c.jobs(), metadataKind), JobListModel::StateRole).toString(),
             QStringLiteral("cancelled"));
    QVERIFY(jobData(c.jobs(), rowOfKind(c.jobs(), metadataKind), JobListModel::CanRetryRole).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(stateOf(c.books(), 0), QStringLiteral("Metadata extraction cancelled \u00b7 2 contents entries"),
                              5000);
    QCOMPARE(titles(c.books()), QStringList{QStringLiteral("title-page")});

    c.retryJob(metadataJob);
    QTRY_COMPARE_WITH_TIMEOUT(titles(c.books()), QStringList{QStringLiteral("Extracted Title")}, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    QCOMPARE(jobData(c.jobs(), rowOfKind(c.jobs(), metadataKind), JobListModel::StateRole).toString(),
             QStringLiteral("succeeded"));
    QVERIFY(jobData(c.jobs(), rowOfKind(c.jobs(), metadataKind), JobListModel::JobIdRole).toString() != metadataJob);
    for (int row = 0; row < c.jobs()->rowCount(); ++row) {  // The cancelled row lost Retry: a newer job exists.
        if (jobData(c.jobs(), row, JobListModel::JobIdRole).toString() == metadataJob)
            QVERIFY(!jobData(c.jobs(), row, JobListModel::CanRetryRole).toBool());
    }
    QCOMPARE(analyzer->bookCalls.load(), 1);  // The retry ran metadata alone.
    QCOMPARE(fake->calls.load(), 2);
}

// A job left running by a crash is requeued at open and completed.
void TestLibraryController::startupRequeuesACrashedJob()
{
    QTemporaryDir dir;
    {
        auto library = mbl::catalog::Library::open(dir.path());
        QVERIFY(library);
        mbl::storage::ImportService service(*library.value());
        QCOMPARE(service.importFile(fixture("title-page.pdf")).outcome, mbl::storage::ImportResult::Outcome::Imported);
        const auto claimed = library.value()->run([](QSqlDatabase& db) { return mbl::catalog::claimNextJob(db); }).result();
        QVERIFY(claimed && claimed.value());  // The metadata job, now "running" when the process dies.
    }
    auto fake = std::make_shared<FakeExtractor>();
    auto analyzer = std::make_shared<FakeAnalyzer>(fake);
    LibraryController c;
    c.setProcessors(fake, analyzer, true);
    QStringList seen;
    connect(&c, &LibraryController::statusTextChanged, this, [&] { seen << c.statusText(); });
    openAndWait(c, dir.path());
    QTRY_COMPARE_WITH_TIMEOUT(titles(c.books()), QStringList{QStringLiteral("Extracted Title")}, 20000);
    QVERIFY2(seen.join(u'\n').contains(QStringLiteral("1 interrupted job(s) queued again.")),
             qPrintable(seen.join(QStringLiteral(" | "))));
    // The interrupted job, its replacement, and the contents job.
    QTRY_COMPARE_WITH_TIMEOUT(c.jobs()->rowCount(), 3, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    QCOMPARE(analyzer->bookCalls.load(), 1);  // The replacement paired with the contents job.
}

// Without an extractor (e.g. a build without the SDK), imports still queue
// their jobs; they wait and are shown as waiting.
void TestLibraryController::withoutAnExtractorJobsWait()
{
    QTemporaryDir dir;
    LibraryController c;
    QVERIFY(!c.processingAvailable());
    openAndWait(c, dir.path());
    c.importFiles({fixture("title-page.pdf")});
    QTRY_COMPARE_WITH_TIMEOUT(c.books()->rowCount(), 1, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    QCOMPARE(stateOf(c.books(), 0), QStringLiteral("Waiting to read title and authors \u00b7 contents waiting"));
    QCOMPARE(c.jobs()->pendingCount(), 2);
}

// A backlog larger than the "recent jobs" window is counted and listed in
// full (the snapshot used to hold only the 100 newest jobs).
void TestLibraryController::pendingCountIncludesTheWholeBacklog()
{
    constexpr int kBooks = 150;
    QTemporaryDir dir;
    {
        auto library = mbl::catalog::Library::open(dir.path());
        QVERIFY(library);
        const bool seeded = library.value()->run([](QSqlDatabase& db) {
            for (int i = 0; i < kBooks; ++i) {
                mbl::domain::NewBook book;
                book.asset.id = mbl::domain::AssetId::create();
                book.asset.sha256 = QStringLiteral("%1").arg(i, 64, 10, QLatin1Char('0'));
                book.asset.byteSize = 1;
                book.asset.managedPath = QStringLiteral("files/%1/source.pdf").arg(book.asset.id.toString());
                book.originalFileName = QStringLiteral("book %1.pdf").arg(i);
                book.originalPath = book.originalFileName;
                auto id = mbl::catalog::registerBook(db, book);
                if (!id || !mbl::catalog::enqueueJob(db, id.value(), mbl::domain::JobKind::Metadata))
                    return false;
            }
            return true;
        }).result();
        QVERIFY(seeded);
    }
    LibraryController c;  // No extractor: the jobs wait.
    openAndWait(c, dir.path());
    QCOMPARE(c.books()->rowCount(), kBooks);
    QCOMPARE(c.jobs()->pendingCount(), kBooks);
    QCOMPARE(c.jobs()->rowCount(), kBooks);
    QVERIFY2(c.jobs()->summary().contains(QStringLiteral("150")), qPrintable(c.jobs()->summary()));
}

void TestLibraryController::refreshesAreCoalesced()
{
    QTemporaryDir dir;
    LibraryController c;
    openAndWait(c, dir.path());
    QSignalSpy refreshed(&c, &LibraryController::booksRefreshed);
    for (int i = 0; i < 20; ++i)
        c.refresh();
    QVERIFY(c.busy());
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    QCOMPARE(refreshed.size(), 2);  // The first, and one for all the calls made meanwhile.
    c.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    QCOMPARE(refreshed.size(), 3);
}

QTEST_GUILESS_MAIN(TestLibraryController)
#include "tst_librarycontroller.moc"
