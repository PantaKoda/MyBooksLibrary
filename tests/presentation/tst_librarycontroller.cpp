// Presentation: the library session opens, recovers and imports off the GUI
// thread, runs metadata jobs, and updates its models only on the GUI thread.
#include "app/libraryroot.h"
#include "catalog/catalog.h"
#include "catalog/jobs.h"
#include "catalog/library.h"
#include "catalog/tocedits.h"
#include "presentation/backupcontroller.h"
#include "presentation/bookinspector.h"
#include "presentation/booklistmodel.h"
#include "presentation/collectionlistmodel.h"
#include "presentation/joblistmodel.h"
#include "presentation/librarycontroller.h"
#include "presentation/searchcontroller.h"
#include "presentation/toctreemodel.h"
#include "processing/contentsanalyzer.h"
#include "processing/metadataextractor.h"
#include "storage/backup.h"
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

using mbl::presentation::BookInspector;
using mbl::presentation::BookListModel;
using mbl::presentation::SearchController;
using mbl::presentation::SearchResultsModel;
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
    std::optional<QString> edition;  // Reported as resolved when set.
    int alternatives = 0;  // Competing title candidates to report.

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
        if (edition) {
            r.metadata.editionStatus = mbl::domain::FieldStatus::Resolved;
            r.metadata.editionStatement = edition;
        }
        mbl::domain::MetadataFieldDetail detail;
        detail.field = mbl::domain::MetadataField::Title;
        detail.evidence << mbl::domain::MetadataEvidence{0, title, QStringLiteral("largest text")};
        for (int i = 0; i < alternatives; ++i)
            detail.alternatives << mbl::domain::MetadataCandidate{QStringLiteral("Candidate %1").arg(i), 0.1, {}, {}};
        r.details << detail;
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

// One field of the inspector, by its code ("title", "contributors", ...).
QVariantMap fieldOf(BookInspector* inspector, const QString& code)
{
    for (const QVariant& f : inspector->metadataFields()) {
        if (f.toMap().value(QStringLiteral("field")).toString() == code)
            return f.toMap();
    }
    return {};
}

QString valueOf(BookInspector* inspector, const QString& code)
{
    return fieldOf(inspector, code).value(QStringLiteral("value")).toString();
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
    void aBackupFolderIsNotOpened();
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
    void inspectorShowsWhatWasPublished();
    void inspectorFollowsProcessingAndSelection();
    void searchUpdatesWhenContentsArePublished();
    void correctionsFromTheInspector();
    void correctionsSurviveARerun();
    void inspectorFollowsEditedContents();
    void contentsEditsFromTheInspector();
    void collectionsAndViews();
    void trashStopsTheRunningJobAndRestoreResumesIt();
    void aDuplicateInTrashCanBeRestored();

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

// Issue #30: `--library "<backup folder>"` (the user guide's shortcut, pointed
// at the wrong folder) is refused with the reason, and the backup still
// verifies, so it can still be restored.
void TestLibraryController::aBackupFolderIsNotOpened()
{
    QTemporaryDir dir;
    LibraryController first;
    openAndWait(first, dir.filePath(QStringLiteral("Library")));
    first.importFiles({fixture("title-page.pdf")});
    QTRY_VERIFY_WITH_TIMEOUT(first.books()->rowCount() == 1 && !first.busy(), 10000);
    const QString backups = dir.filePath(QStringLiteral("Backups"));
    QVERIFY(QDir().mkpath(backups));
    mbl::presentation::BackupController* backup = first.backup();
    QSignalSpy finished(backup, &mbl::presentation::BackupController::finished);
    backup->backUp(backups);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 30000);
    QVERIFY2(backup->succeeded(), qPrintable(backup->statusText()));
    const QString folder = QDir::fromNativeSeparators(backup->resultFolder());

    LibraryController second;
    openAndWait(second, folder);
    QVERIFY(second.failed());
    QVERIFY2(second.statusText().contains(QStringLiteral("is a MyBooksLibrary backup, not a library")),
             qPrintable(second.statusText()));
    auto verified = mbl::storage::verifyBackup(folder);
    QVERIFY2(verified, verified ? "" : qPrintable(verified.error().message));
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
        // One book is being read (both of its jobs run in one call), one waits.
        QTRY_VERIFY_WITH_TIMEOUT(c.jobs()->summary().startsWith(QStringLiteral("Reading title and authors: ")), 5000);
        QVERIFY2(c.jobs()->summary().endsWith(QStringLiteral(" · 1 book(s) waiting")), qPrintable(c.jobs()->summary()));
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
// their jobs; they wait and are shown as waiting. The footer counts books,
// not their two jobs each.
void TestLibraryController::withoutAnExtractorJobsWait()
{
    QTemporaryDir dir;
    LibraryController c;
    QVERIFY(!c.processingAvailable());
    openAndWait(c, dir.path());
    c.importFiles({fixture("title-page.pdf"), fixture("contents-book.pdf"), fixture("image-only.pdf")});
    QTRY_COMPARE_WITH_TIMEOUT(c.books()->rowCount(), 3, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    QCOMPARE(stateOf(c.books(), 0), QStringLiteral("Waiting to read title and authors \u00b7 contents waiting"));
    QCOMPARE(c.jobs()->pendingCount(), 6);  // Jobs: drives the busy indicator and Cancel all.
    QCOMPARE(c.jobs()->summary(), QStringLiteral("3 book(s) waiting"));
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

void TestLibraryController::inspectorShowsWhatWasPublished()
{
    QTemporaryDir dir;
    auto fake = std::make_shared<FakeExtractor>();
    fake->alternatives = 7;
    auto analyzer = std::make_shared<FakeAnalyzer>(fake);
    LibraryController c;
    c.setProcessors(fake, analyzer, true);
    openAndWait(c, dir.path());
    c.importFiles({fixture("title-page.pdf")});
    QTRY_COMPARE_WITH_TIMEOUT(titles(c.books()), QStringList{QStringLiteral("Extracted Title")}, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);

    BookInspector* inspector = c.inspector();
    QSignalSpy loaded(inspector, &BookInspector::loaded);
    QSignalSpy resets(inspector->contents(), &QAbstractItemModel::modelReset);
    QAbstractItemModelTester treeTester(inspector->contents());
    inspector->select(c.books()->bookIdAt(0));
    QVERIFY(inspector->hasBook());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 5000);
    QCOMPARE(inspector->title(), QStringLiteral("Extracted Title"));
    QCOMPARE(inspector->fileText(), QStringLiteral("title-page.pdf \u00b7 3 pages"));

    const QVariantMap title = inspector->metadataFields().at(0).toMap();
    QCOMPARE(title.value(QStringLiteral("value")).toString(), QStringLiteral("Extracted Title"));
    QCOMPARE(title.value(QStringLiteral("sourceText")).toString(), QStringLiteral("From the document"));
    QCOMPARE(title.value(QStringLiteral("evidence")).toStringList(),
             QStringList{QStringLiteral("Page 1: \u201cExtracted Title\u201d \u2014 largest text")});
    const QStringList alternatives = title.value(QStringLiteral("alternatives")).toStringList();
    QCOMPARE(alternatives.size(), 6);  // The best five, then a count.
    QCOMPARE(alternatives.last(), QStringLiteral("and 2 more"));
    const QVariantMap authors = fieldOf(inspector, QStringLiteral("contributors"));
    QCOMPARE(authors.value(QStringLiteral("value")).toString(), QStringLiteral("\u2014"));
    QCOMPARE(authors.value(QStringLiteral("sourceText")).toString(), QStringLiteral("Not found in the pages searched"));

    QCOMPARE(inspector->contentsSummary(), QStringLiteral("2 contents entries, 1 with a confirmed page."));
    QVERIFY(inspector->contentsNotes().contains(QStringLiteral("No page found: 1 of 2.")));
    QVERIFY(inspector->contentsReasons().isEmpty());  // This analysis reported none.
    for (const QString& note : inspector->contentsNotes()) {  // Only short counts; export readiness is the Export dialog's.
        QVERIFY2(!note.startsWith(QStringLiteral("Why:")), qPrintable(note));
        QVERIFY2(!note.contains(QStringLiteral("bookmarked copy")), qPrintable(note));
    }
    QCOMPARE(inspector->contents()->entryCount(), 2);
    QCOMPARE(inspector->contents()->data(inspector->contents()->index(0, 0), mbl::presentation::TocTreeModel::PageTextRole)
                 .toString(),
             QStringLiteral("Page 1"));  // Page index 0.
    QCOMPARE(resets.size(), 1);

    // A refresh with the same contents run keeps the tree (expansion, current entry).
    c.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 2, 5000);
    QCOMPARE(resets.size(), 1);

    // A book that is gone.
    inspector->select(mbl::domain::BookId::create().toString());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 3, 5000);
    QCOMPARE(inspector->error(), QStringLiteral("This book is no longer in the library."));
    QCOMPARE(inspector->contents()->entryCount(), 0);
}

// Selected while processing: the inspector updates when the results are
// published. And only the newest selection is ever shown.
void TestLibraryController::inspectorFollowsProcessingAndSelection()
{
    QTemporaryDir dir;
    auto fake = std::make_shared<FakeExtractor>();
    fake->block = true;
    auto analyzer = std::make_shared<FakeAnalyzer>(fake);
    LibraryController c;
    c.setProcessors(fake, analyzer, true);
    openAndWait(c, dir.path());
    c.importFiles({fixture("title-page.pdf"), fixture("contents-book.pdf")});
    QTRY_VERIFY_WITH_TIMEOUT(fake->started.load(), 20000);
    QTRY_COMPARE_WITH_TIMEOUT(c.books()->rowCount(), 2, 10000);

    BookInspector* inspector = c.inspector();
    QSignalSpy loaded(inspector, &BookInspector::loaded);
    const QString first = c.books()->bookIdAt(0);
    const QString second = c.books()->bookIdAt(1);
    inspector->select(first);
    inspector->select(second);  // Before the first load arrives.
    QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 5000);
    QTest::qWait(100);
    QCOMPARE(inspector->bookId(), second);
    QCOMPARE(loaded.size(), 1);  // The first selection's load was dropped.
    QCOMPARE(inspector->contentsSummary(), QStringLiteral("Contents not analyzed yet."));

    inspector->select(first);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 2, 5000);
    QCOMPARE(inspector->contents()->entryCount(), 0);
    fake->block = false;  // Let processing publish.
    QTRY_COMPARE_WITH_TIMEOUT(inspector->contents()->entryCount(), 2, 20000);
    QCOMPARE(inspector->title(), QStringLiteral("Extracted Title"));
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 20000);

    inspector->select(QString());
    QVERIFY(!inspector->hasBook());
    QCOMPARE(inspector->title(), QString());
}

// A search entered before processing finishes shows the chapter once the
// contents are published, with the book's processing state beside it.
void TestLibraryController::searchUpdatesWhenContentsArePublished()
{
    QTemporaryDir dir;
    auto fake = std::make_shared<FakeExtractor>();
    fake->block = true;
    auto analyzer = std::make_shared<FakeAnalyzer>(fake);
    LibraryController c;
    c.setProcessors(fake, analyzer, true);
    openAndWait(c, dir.path());
    SearchController* search = c.search();
    QSignalSpy applied(search, &SearchController::responseApplied);
    search->setText(QStringLiteral("introduction"));
    search->refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!applied.isEmpty(), 5000);
    QCOMPARE(search->results()->rowCount(), 0);
    QCOMPARE(search->statusText(), QStringLiteral("No matches in indexed titles and contents."));

    c.importFiles({fixture("title-page.pdf")});
    QTRY_VERIFY_WITH_TIMEOUT(fake->started.load(), 20000);
    fake->block = false;
    QTRY_COMPARE_WITH_TIMEOUT(search->results()->rowCount(), 1, 20000);
    const QModelIndex row = search->results()->index(0);
    QCOMPARE(search->results()->data(row, SearchResultsModel::TitleRole).toString(), QStringLiteral("Extracted Title"));
    const QVariantMap hit = search->results()->data(row, SearchResultsModel::ChaptersRole).toList().first().toMap();
    QCOMPARE(hit.value(QStringLiteral("title")).toString(), QStringLiteral("1 Introduction"));
    QCOMPARE(hit.value(QStringLiteral("pageText")).toString(), QStringLiteral("Page 1"));
    QTRY_COMPARE_WITH_TIMEOUT(search->results()->data(search->results()->index(0), SearchResultsModel::ProcessingStateRole)
                                  .toString(),
                              QStringLiteral("Metadata ready · 2 contents entries"), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
}

// Value, Leave empty (Cleared) and the document's value (Auto) from the
// inspector: saved with the search index, shown with their source, refused
// when invalid, and kept after a restart.
void TestLibraryController::correctionsFromTheInspector()
{
    QTemporaryDir dir;
    auto fake = std::make_shared<FakeExtractor>();
    fake->edition = QStringLiteral("2nd edition");
    QString book;
    {
        LibraryController c;
        c.setProcessors(fake, std::make_shared<FakeAnalyzer>(fake), true);
        openAndWait(c, dir.path());
        c.importFiles({fixture("title-page.pdf")});
        QTRY_COMPARE_WITH_TIMEOUT(titles(c.books()), QStringList{QStringLiteral("Extracted Title")}, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
        book = c.books()->bookIdAt(0);
        BookInspector* inspector = c.inspector();
        QSignalSpy corrected(inspector, &BookInspector::corrected);
        inspector->select(book);
        QTRY_COMPARE_WITH_TIMEOUT(valueOf(inspector, QStringLiteral("edition")), QStringLiteral("2nd edition"), 5000);
        QCOMPARE(fieldOf(inspector, QStringLiteral("title")).value(QStringLiteral("mode")).toString(), QStringLiteral("auto"));
        QCOMPARE(fieldOf(inspector, QStringLiteral("title")).value(QStringLiteral("documentValue")).toString(), QString());

        // A value: shown as the user's, with the document's value beside it;
        // the book list and search follow.
        inspector->setText(book, QStringLiteral("title"), QStringLiteral("  Cooking Basics "));
        QVERIFY(inspector->saving());
        QTRY_COMPARE_WITH_TIMEOUT(corrected.size(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(valueOf(inspector, QStringLiteral("title")), QStringLiteral("Cooking Basics"), 5000);
        QVERIFY(!inspector->saving());
        QVariantMap title = fieldOf(inspector, QStringLiteral("title"));
        QCOMPARE(title.value(QStringLiteral("sourceText")).toString(), QStringLiteral("Your correction"));
        QCOMPARE(title.value(QStringLiteral("mode")).toString(), QStringLiteral("value"));
        QCOMPARE(title.value(QStringLiteral("documentValue")).toString(), QStringLiteral("Extracted Title"));
        QCOMPARE(title.value(QStringLiteral("editText")).toString(), QStringLiteral("Cooking Basics"));
        QTRY_COMPARE_WITH_TIMEOUT(titles(c.books()), QStringList{QStringLiteral("Cooking Basics")}, 5000);
        SearchController* search = c.search();
        search->setScope(int(mbl::domain::SearchScope::Titles));
        search->setText(QStringLiteral("cooking"));
        search->refresh();
        QTRY_COMPARE_WITH_TIMEOUT(search->results()->rowCount(), 1, 5000);
        search->setText(QStringLiteral("extracted"));
        search->refresh();
        QTRY_COMPARE_WITH_TIMEOUT(search->results()->rowCount(), 0, 5000);  // Was 1: the new query applied.
        search->clear();

        // Leave empty: no fallback to the document's edition.
        inspector->clearField(book, QStringLiteral("edition"));
        QTRY_COMPARE_WITH_TIMEOUT(corrected.size(), 2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(valueOf(inspector, QStringLiteral("edition")), QStringLiteral("—"), 5000);
        QCOMPARE(fieldOf(inspector, QStringLiteral("edition")).value(QStringLiteral("sourceText")).toString(),
                 QStringLiteral("Cleared by you"));
        QCOMPARE(fieldOf(inspector, QStringLiteral("edition")).value(QStringLiteral("documentValue")).toString(),
                 QStringLiteral("2nd edition"));

        // Ordered people; a row left empty is ignored.
        inspector->setContributors(book, QVariantList{
            QVariantMap{{QStringLiteral("name"), QStringLiteral("Ada Lovelace")}, {QStringLiteral("role"), QStringLiteral("author")}},
            QVariantMap{{QStringLiteral("name"), QStringLiteral("  ")}, {QStringLiteral("role"), QStringLiteral("author")}},
            QVariantMap{{QStringLiteral("name"), QStringLiteral("Charles Babbage")}, {QStringLiteral("role"), QStringLiteral("editor")}},
        });
        QTRY_COMPARE_WITH_TIMEOUT(corrected.size(), 3, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(valueOf(inspector, QStringLiteral("contributors")),
                                  QStringLiteral("Ada Lovelace (author); Charles Babbage (editor)"), 5000);
        const QVariantList people = fieldOf(inspector, QStringLiteral("contributors")).value(QStringLiteral("editContributors")).toList();
        QCOMPARE(people.size(), 2);
        QCOMPARE(people.at(1).toMap().value(QStringLiteral("role")).toString(), QStringLiteral("editor"));

        // Refused before anything is saved, with a reason.
        const QStringList refusedYears{QStringLiteral("abc"), QStringLiteral("0"), QStringLiteral("10000"), QString()};
        for (const QString& year : refusedYears) {
            inspector->setYear(book, QStringLiteral("publication_year"), year);
            QVERIFY2(!inspector->correctionError().isEmpty(), qPrintable(year));
            QVERIFY(!inspector->saving());
        }
        inspector->setText(book, QStringLiteral("subtitle"), QStringLiteral("   "));
        QVERIFY(!inspector->correctionError().isEmpty());
        inspector->setText(book, QStringLiteral("publication_year"), QStringLiteral("1999"));  // Not a text field.
        QVERIFY(!inspector->correctionError().isEmpty());
        inspector->setContributors(book, QVariantList{QVariantMap{{QStringLiteral("name"), QString()}}});
        QVERIFY(!inspector->correctionError().isEmpty());
        inspector->setContributors(book, QVariantList{
            QVariantMap{{QStringLiteral("name"), QStringLiteral("X")}, {QStringLiteral("role"), QStringLiteral("pilot")}}});
        QVERIFY(!inspector->correctionError().isEmpty());
        QTest::qWait(100);
        QCOMPARE(corrected.size(), 3);
        QCOMPARE(fieldOf(inspector, QStringLiteral("publication_year")).value(QStringLiteral("mode")).toString(),
                 QStringLiteral("auto"));

        inspector->setYear(book, QStringLiteral("publication_year"), QStringLiteral(" 1843 "));
        QVERIFY(inspector->correctionError().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(valueOf(inspector, QStringLiteral("publication_year")), QStringLiteral("1843"), 5000);
        inspector->setText(book, QStringLiteral("subtitle"), QStringLiteral("Notes"));
        QTRY_COMPARE_WITH_TIMEOUT(valueOf(inspector, QStringLiteral("subtitle")), QStringLiteral("Notes"), 5000);
        // Back to the document's value (it has no subtitle).
        inspector->useDocumentValue(book, QStringLiteral("subtitle"));
        QTRY_COMPARE_WITH_TIMEOUT(valueOf(inspector, QStringLiteral("subtitle")), QStringLiteral("—"), 5000);
        QCOMPARE(fieldOf(inspector, QStringLiteral("subtitle")).value(QStringLiteral("mode")).toString(), QStringLiteral("auto"));
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    }

    // After a restart.
    LibraryController c;
    c.setProcessors(fake, std::make_shared<FakeAnalyzer>(fake), true);
    openAndWait(c, dir.path());
    QCOMPARE(titles(c.books()), QStringList{QStringLiteral("Cooking Basics")});
    BookInspector* inspector = c.inspector();
    QSignalSpy loaded(inspector, &BookInspector::loaded);
    inspector->select(book);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 5000);
    QCOMPARE(valueOf(inspector, QStringLiteral("title")), QStringLiteral("Cooking Basics"));
    QCOMPARE(valueOf(inspector, QStringLiteral("edition")), QStringLiteral("—"));
    QCOMPARE(fieldOf(inspector, QStringLiteral("edition")).value(QStringLiteral("mode")).toString(), QStringLiteral("cleared"));
    QCOMPARE(valueOf(inspector, QStringLiteral("contributors")), QStringLiteral("Ada Lovelace (author); Charles Babbage (editor)"));
    QCOMPARE(valueOf(inspector, QStringLiteral("publication_year")), QStringLiteral("1843"));
}

// "Read title and authors again": a correction made while the rerun runs,
// and one made before, both survive its publication; the cleared edition
// does not come back; the document's new value is shown beside them.
void TestLibraryController::correctionsSurviveARerun()
{
    QTemporaryDir dir;
    auto fake = std::make_shared<FakeExtractor>();
    fake->edition = QStringLiteral("2nd edition");
    LibraryController c;
    c.setProcessors(fake, std::make_shared<FakeAnalyzer>(fake), true);
    openAndWait(c, dir.path());
    c.importFiles({fixture("title-page.pdf")});
    QTRY_COMPARE_WITH_TIMEOUT(titles(c.books()), QStringList{QStringLiteral("Extracted Title")}, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    const QString book = c.books()->bookIdAt(0);
    BookInspector* inspector = c.inspector();
    QSignalSpy corrected(inspector, &BookInspector::corrected);
    inspector->select(book);
    QTRY_COMPARE_WITH_TIMEOUT(valueOf(inspector, QStringLiteral("edition")), QStringLiteral("2nd edition"), 5000);
    inspector->clearField(book, QStringLiteral("edition"));
    QTRY_COMPARE_WITH_TIMEOUT(corrected.size(), 1, 5000);

    const int callsBefore = fake->calls.load();
    fake->title = QStringLiteral("Second Reading");
    fake->edition = QStringLiteral("3rd edition");
    fake->started = false;
    fake->block = true;
    c.rerunMetadata(book);
    QTRY_VERIFY_WITH_TIMEOUT(fake->started.load(), 10000);
    // While it runs, the earlier results stay shown and a correction is saved.
    QCOMPARE(valueOf(inspector, QStringLiteral("title")), QStringLiteral("Extracted Title"));
    inspector->setText(book, QStringLiteral("title"), QStringLiteral("My Title"));
    QTRY_COMPARE_WITH_TIMEOUT(corrected.size(), 2, 5000);
    fake->block = false;
    QTRY_COMPARE_WITH_TIMEOUT(fieldOf(inspector, QStringLiteral("title")).value(QStringLiteral("documentValue")).toString(),
                              QStringLiteral("Second Reading"), 10000);
    QCOMPARE(fake->calls.load(), callsBefore + 1);
    QCOMPARE(valueOf(inspector, QStringLiteral("title")), QStringLiteral("My Title"));
    QCOMPARE(valueOf(inspector, QStringLiteral("edition")), QStringLiteral("—"));
    QCOMPARE(fieldOf(inspector, QStringLiteral("edition")).value(QStringLiteral("documentValue")).toString(),
             QStringLiteral("3rd edition"));
    QTRY_COMPARE_WITH_TIMEOUT(titles(c.books()), QStringList{QStringLiteral("My Title")}, 5000);

    // Returning the title to Auto shows the new reading, not the old one.
    inspector->useDocumentValue(book, QStringLiteral("title"));
    QTRY_COMPARE_WITH_TIMEOUT(valueOf(inspector, QStringLiteral("title")), QStringLiteral("Second Reading"), 5000);

    // Contents can be analyzed again too; the metadata corrections stay.
    const int bookCallsBefore = int(c.jobs()->rowCount());
    c.rerunContents(book);
    QTRY_VERIFY_WITH_TIMEOUT(c.jobs()->rowCount() > bookCallsBefore, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    QCOMPARE(valueOf(inspector, QStringLiteral("edition")), QStringLiteral("—"));
    QCOMPARE(inspector->contents()->entryCount(), 2);
}

// The inspector's contents tree follows an edit, keeping the edits after a
// changed rerun, and returning to the analysis, although the run is the same.
void TestLibraryController::inspectorFollowsEditedContents()
{
    QTemporaryDir dir;
    auto opened = mbl::catalog::Library::open(dir.path());
    QVERIFY(opened);
    std::shared_ptr<mbl::catalog::Library> library(std::move(opened.value()));
    using namespace mbl::domain;
    const auto registered = library
                                ->run([](QSqlDatabase& db) {
                                    NewBook b;
                                    b.asset.id = AssetId::create();
                                    b.asset.sha256 = QString(64, u'7');
                                    b.asset.byteSize = 1;
                                    b.asset.pageCount = 20;
                                    b.asset.managedPath = QStringLiteral("files/x/source.pdf");
                                    b.originalFileName = QStringLiteral("x.pdf");
                                    b.originalPath = b.originalFileName;
                                    return mbl::catalog::registerBook(db, b);
                                })
                                .result();
    QVERIFY2(registered, registered ? "" : qPrintable(registered.error().message));
    const BookId book = registered.value();
    const auto publish = [&](const QString& title) {
        return library
            ->run([book, title](QSqlDatabase& db) -> Result<RunId> {
                auto t = mbl::catalog::requestTocRun(db, book);
                if (!t)
                    return t.error();
                TocEntry e;
                e.sdkEntryId = QStringLiteral("e1");
                e.title = title;
                e.hierarchy = HierarchyState::Root;
                TocAnalysis toc{QStringLiteral("plan_ready"), false, {e}};
                const RunIdentity run{t.value().sourceSha256, QStringLiteral("t"), QString(), QStringLiteral("{}"),
                                      toc.outcome, std::nullopt};
                return mbl::catalog::publishToc(db, t.value(), run, toc);
            })
            .result();
    };
    const auto first = publish(QStringLiteral("Chapter one"));
    QVERIFY2(first, first ? "" : qPrintable(first.error().message));
    const RunId run = first.value();

    BookInspector inspector;
    inspector.setLibrary(library);
    QSignalSpy loaded(&inspector, &BookInspector::loaded);
    const auto shownTitle = [&inspector] {
        return inspector.contents()->data(inspector.contents()->index(0, 0), Qt::DisplayRole).toString();
    };
    inspector.select(book.toString());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 5000);
    QCOMPARE(shownTitle(), QStringLiteral("Chapter one"));

    const auto change = [&](auto task) {
        const qsizetype before = loaded.size();
        QVERIFY(library->run(task).result());
        inspector.reload();  // As LibraryController::refresh does.
        QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), before + 1, 5000);
    };
    // An edit: same run, new revision.
    change([book, run](QSqlDatabase& db) {
        TocEdit rename;
        rename.entryKey = QStringLiteral("e1");
        rename.title = QStringLiteral("Chapter 1: Beginnings");
        return mbl::catalog::editToc(db, book, TocEditBase{run, std::nullopt}, {rename}).ok();
    });
    QCOMPARE(shownTitle(), QStringLiteral("Chapter 1: Beginnings"));

    // A changed rerun keeps showing the edits; returning to the analysis shows it.
    const auto second = publish(QStringLiteral("Chapter One"));
    QVERIFY2(second, second ? "" : qPrintable(second.error().message));
    const RunId rerun = second.value();
    inspector.reload();
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 3, 5000);
    QCOMPARE(shownTitle(), QStringLiteral("Chapter 1: Beginnings"));
    change([book, rerun](QSqlDatabase& db) {
        const auto details = mbl::catalog::bookDetails(db, book);
        if (!details || !details.value().tocRevision)
            return false;
        return mbl::catalog::useAnalyzedToc(db, book, TocEditBase{rerun, details.value().tocRevision->id}).ok();
    });
    QCOMPARE(shownTitle(), QStringLiteral("Chapter One"));
}

// Contents edits through the inspector: each command, what the tree then
// says, refusals before saving, a stale view, and reconciliation.
void TestLibraryController::contentsEditsFromTheInspector()
{
    using namespace mbl::domain;
    using mbl::presentation::TocTreeModel;
    QTemporaryDir dir;
    auto opened = mbl::catalog::Library::open(dir.path());
    QVERIFY(opened);
    std::shared_ptr<mbl::catalog::Library> library(std::move(opened.value()));
    const auto registered = library
                                ->run([](QSqlDatabase& db) {
                                    NewBook b;
                                    b.asset.id = AssetId::create();
                                    b.asset.sha256 = QString(64, u'8');
                                    b.asset.byteSize = 1;
                                    b.asset.pageCount = 20;
                                    b.asset.managedPath = QStringLiteral("files/y/source.pdf");
                                    b.originalFileName = QStringLiteral("y.pdf");
                                    b.originalPath = b.originalFileName;
                                    return mbl::catalog::registerBook(db, b);
                                })
                                .result();
    QVERIFY(registered);
    const BookId book = registered.value();
    const auto publish = [&](const QList<TocEntry>& entries) {
        return library
            ->run([book, entries](QSqlDatabase& db) -> Result<RunId> {
                auto t = mbl::catalog::requestTocRun(db, book);
                if (!t)
                    return t.error();
                TocAnalysis toc{QStringLiteral("plan_ready"), false, entries};
                const RunIdentity run{t.value().sourceSha256, QStringLiteral("t"), QString(), QStringLiteral("{}"),
                                      toc.outcome, std::nullopt};
                return mbl::catalog::publishToc(db, t.value(), run, toc);
            })
            .result();
    };
    const auto entry = [](const QString& id, int order, const QString& title, std::optional<int> page) {
        TocEntry e;
        e.sdkEntryId = id;
        e.order = order;
        e.title = title;
        e.hierarchy = HierarchyState::Root;
        if (page) {
            e.destinationState = DestinationState::Resolved;
            e.destinationPage = page;
        } else {
            e.evidence.destinationReasons << QStringLiteral("No printed page number matched.");
        }
        return e;
    };
    QVERIFY(publish({entry(QStringLiteral("e1"), 0, QStringLiteral("Intro"), 0),
                     entry(QStringLiteral("e2"), 1, QStringLiteral("Netwrking"), std::nullopt),
                     entry(QStringLiteral("e3"), 2, QStringLiteral("Index"), 10)}));

    BookInspector inspector;
    inspector.setLibrary(library);
    QSignalSpy loaded(&inspector, &BookInspector::loaded);
    QSignalSpy corrected(&inspector, &BookInspector::corrected);
    TocTreeModel* tree = inspector.contents();
    const auto role = [tree](const QString& key, int r) { return tree->data(tree->indexOfEntry(key), r); };
    inspector.select(book.toString());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 5000);
    QVERIFY(!inspector.contentsEdited());
    QCOMPARE(inspector.contentsEditText(), QString());
    const auto waitSaved = [&](qsizetype count) {
        QTRY_COMPARE_WITH_TIMEOUT(corrected.size(), count, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!inspector.saving() && !inspector.loading(), 5000);
    };

    // Rename and set a page: the analysis's reasons for "no page" are no longer shown.
    const QString id = book.toString();
    inspector.renameEntry(id, QStringLiteral("e2"), QStringLiteral("  Networking "));
    waitSaved(1);
    QCOMPARE(role(QStringLiteral("e2"), TocTreeModel::TitleRole).toString(), QStringLiteral("Networking"));
    QCOMPARE(role(QStringLiteral("e2"), TocTreeModel::EditedTextRole).toString(), QStringLiteral("Changed by you: title"));
    QVERIFY(inspector.contentsEdited());
    QCOMPARE(inspector.contentsEditText(), QStringLiteral("You edited these contents (version 1). Search uses your version."));
    QVERIFY(role(QStringLiteral("e2"), TocTreeModel::DetailRole).toString().contains(QStringLiteral("No printed page number matched.")));
    inspector.setEntryPage(id, QStringLiteral("e2"), QStringLiteral("5"));
    waitSaved(2);
    QCOMPARE(role(QStringLiteral("e2"), TocTreeModel::PageRole).toInt(), 5);
    QVERIFY(role(QStringLiteral("e2"), TocTreeModel::StateTextRole).toString().startsWith(QStringLiteral("page set by you")));
    const QString detail = role(QStringLiteral("e2"), TocTreeModel::DetailRole).toString();
    QVERIFY2(detail.contains(QStringLiteral("You set its page to 5.")), qPrintable(detail));
    QVERIFY2(!detail.contains(QStringLiteral("No printed page number matched.")), qPrintable(detail));
    QVERIFY(!role(QStringLiteral("e2"), TocTreeModel::UncertainRole).toBool());

    // Refused before saving.
    for (const char* page : {"0", "21", "x", ""}) {
        inspector.setEntryPage(id, QStringLiteral("e2"), QLatin1String(page));
        QVERIFY2(!inspector.contentsError().isEmpty(), page);
        QVERIFY(!inspector.saving());
    }
    QCOMPARE(inspector.contentsError(), QStringLiteral("Enter a page number from 1 to 20."));
    inspector.renameEntry(id, QStringLiteral("e2"), QStringLiteral("  "));
    QVERIFY(!inspector.contentsError().isEmpty());
    inspector.renameEntry(BookId::create().toString(), QStringLiteral("e2"), QStringLiteral("X"));
    QCOMPARE(inspector.contentsError(), QStringLiteral("Select the book again to change its contents."));
    QTest::qWait(100);
    QCOMPARE(corrected.size(), 2);

    // Levels: under the entry above it, and back.
    QVERIFY(!inspector.canIndent(QStringLiteral("e1")));  // Nothing above it.
    QVERIFY(inspector.canIndent(QStringLiteral("e2")));
    QVERIFY(!inspector.canOutdent(QStringLiteral("e2")));
    inspector.indentEntry(id, QStringLiteral("e2"));
    waitSaved(3);
    QCOMPARE(tree->parent(tree->indexOfEntry(QStringLiteral("e2"))), tree->indexOfEntry(QStringLiteral("e1")));
    QVERIFY(inspector.canOutdent(QStringLiteral("e2")));
    QVERIFY(!inspector.canIndent(QStringLiteral("e2")));  // First under "Intro".
    inspector.outdentEntry(id, QStringLiteral("e2"));
    waitSaved(4);
    QVERIFY(!tree->parent(tree->indexOfEntry(QStringLiteral("e2"))).isValid());
    QCOMPARE(role(QStringLiteral("e2"), TocTreeModel::EditedTextRole).toString(),
             QStringLiteral("Changed by you: title, page, level"));

    // Add, remove and restore.
    inspector.addEntryAfter(id, QStringLiteral("e3"), QStringLiteral("Glossary"), QString());
    waitSaved(5);
    QCOMPARE(tree->entryCount(), 4);
    QCOMPARE(inspector.contentsSummary(), QStringLiteral("4 contents entries, 3 with a confirmed page."));
    inspector.removeEntry(id, QStringLiteral("e3"));
    waitSaved(6);
    QVERIFY(role(QStringLiteral("e3"), TocTreeModel::RemovedRole).toBool());
    QCOMPARE(role(QStringLiteral("e3"), TocTreeModel::StateTextRole).toString(), QStringLiteral("removed by you · not searched"));
    QCOMPARE(inspector.contentsSummary(), QStringLiteral("3 contents entries, 2 with a confirmed page."));
    QVERIFY(inspector.contentsNotes().contains(QStringLiteral("Removed by you: 1 (kept, not searched).")));
    inspector.restoreEntry(id, QStringLiteral("e3"));
    waitSaved(7);
    QVERIFY(!role(QStringLiteral("e3"), TocTreeModel::RemovedRole).toBool());

    // A change made elsewhere first: nothing saved, a message, and the new contents shown.
    QVERIFY(library
                ->run([book](QSqlDatabase& db) {
                    const auto d = mbl::catalog::bookDetails(db, book).value();
                    TocEdit rename;
                    rename.entryKey = QStringLiteral("e1");
                    rename.title = QStringLiteral("Introduction");
                    return mbl::catalog::editToc(db, book, TocEditBase{d.tocRun, d.tocRevision->id}, {rename}).ok();
                })
                .result());
    const qsizetype before = loaded.size();
    inspector.renameEntry(id, QStringLiteral("e3"), QStringLiteral("Index of terms"));
    QTRY_VERIFY_WITH_TIMEOUT(inspector.contentsError().startsWith(QStringLiteral("The contents changed while you were editing")), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(loaded.size() > before && !inspector.loading(), 5000);
    QCOMPARE(role(QStringLiteral("e1"), TocTreeModel::TitleRole).toString(), QStringLiteral("Introduction"));
    QCOMPARE(role(QStringLiteral("e3"), TocTreeModel::TitleRole).toString(), QStringLiteral("Index"));
    QCOMPARE(corrected.size(), 7);

    // A newer, different analysis: the edits stay until the user chooses.
    QVERIFY(publish({entry(QStringLiteral("f1"), 0, QStringLiteral("Intro"), 0),
                     entry(QStringLiteral("f2"), 1, QStringLiteral("Networking basics"), 4)}));
    inspector.reload();
    QTRY_VERIFY_WITH_TIMEOUT(inspector.contentsNeedReconciliation(), 5000);
    QCOMPARE(inspector.contentsEditText(),
             QStringLiteral("A newer analysis found different contents (2 entries). Your edited contents are still shown "
                            "and searched until you choose."));
    QCOMPARE(role(QStringLiteral("e1"), TocTreeModel::TitleRole).toString(), QStringLiteral("Introduction"));
    inspector.keepContentsEdits(id);
    waitSaved(8);
    QVERIFY(!inspector.contentsNeedReconciliation());
    QVERIFY(inspector.contentsEdited());
    inspector.useAnalyzedContents(id);
    waitSaved(9);
    QVERIFY(!inspector.contentsEdited());
    QCOMPARE(tree->entryCount(), 2);
    QCOMPARE(role(QStringLiteral("f2"), TocTreeModel::TitleRole).toString(), QStringLiteral("Networking basics"));
}

// Collections and the list's views: the library, one collection, Trash.
// Search follows a collection; the activity list still knows every title.
void TestLibraryController::collectionsAndViews()
{
    QTemporaryDir dir;
    auto fake = std::make_shared<FakeExtractor>();
    LibraryController c;
    c.setProcessors(fake, std::make_shared<FakeAnalyzer>(fake), true);
    openAndWait(c, dir.path());
    c.importFiles({fixture("title-page.pdf"), fixture("contents-book.pdf")});
    QTRY_COMPARE_WITH_TIMEOUT(c.libraryCount(), 2, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 20000);
    const QString first = c.books()->bookIdAt(0);
    QSignalSpy organized(&c, &LibraryController::organized);
    QCOMPARE(c.view(), LibraryController::View::Library);
    QCOMPARE(c.viewTitle(), QStringLiteral("Library"));

    c.createCollection(QStringLiteral(" Study "));
    QTRY_COMPARE_WITH_TIMEOUT(organized.size(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(c.collections()->rowCount(), 1, 5000);
    const QString study = c.collections()->data(c.collections()->index(0), mbl::presentation::CollectionListModel::CollectionIdRole).toString();
    c.createCollection(QStringLiteral("study"));
    QTRY_COMPARE_WITH_TIMEOUT(c.organizeError(), QStringLiteral("A collection with that name already exists."), 5000);
    c.dismissOrganizeError();

    c.addToCollection(study, first);
    QTRY_COMPARE_WITH_TIMEOUT(c.collections()->data(c.collections()->index(0), mbl::presentation::CollectionListModel::BookCountRole).toInt(), 1, 5000);
    c.showCollection(study);
    QCOMPARE(c.view(), LibraryController::View::Collection);
    QCOMPARE(c.viewTitle(), QStringLiteral("Study"));
    QTRY_COMPARE_WITH_TIMEOUT(c.books()->rowCount(), 1, 5000);
    QCOMPARE(c.books()->bookIdAt(0), first);

    // Search follows the collection (both books have the same fake title).
    SearchController* search = c.search();
    search->setText(QStringLiteral("extracted"));
    search->refresh();
    QTRY_COMPARE_WITH_TIMEOUT(search->results()->rowCount(), 1, 5000);
    c.showLibrary();
    QTRY_COMPARE_WITH_TIMEOUT(search->results()->rowCount(), 2, 5000);
    search->clear();

    // Trash: gone from the library and the collection, listed in Trash.
    c.moveToTrash(first);
    QTRY_COMPARE_WITH_TIMEOUT(c.trashCount(), 1, 5000);
    QCOMPARE(c.libraryCount(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(c.books()->rowCount(), 1, 5000);
    QVERIFY(c.books()->bookIdAt(0) != first);
    QVERIFY(!c.books()->titleOf(mbl::domain::BookId::fromString(first)).isEmpty());  // Activity still knows it.
    c.showCollection(study);
    QTRY_COMPARE_WITH_TIMEOUT(c.books()->rowCount(), 0, 5000);
    c.showTrash();
    QTRY_COMPARE_WITH_TIMEOUT(c.books()->rowCount(), 1, 5000);
    QCOMPARE(c.books()->bookIdAt(0), first);
    QVERIFY(c.books()->data(c.books()->index(0), BookListModel::ProcessingStateRole).toString().startsWith(QStringLiteral("In Trash")));
    c.addToCollection(study, first);
    QTRY_COMPARE_WITH_TIMEOUT(c.organizeError(), QStringLiteral("A book in Trash cannot be added to a collection. Restore it first."), 5000);

    // Restore: back in the library and its collection.
    c.restoreFromTrash(first);
    QTRY_COMPARE_WITH_TIMEOUT(c.trashCount(), 0, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(c.books()->rowCount(), 0, 5000);  // Trash view, now empty.
    c.showCollection(study);
    QTRY_COMPARE_WITH_TIMEOUT(c.books()->rowCount(), 1, 5000);

    // Rename shows in the heading; deleting the shown collection returns to the library.
    c.renameCollection(study, QStringLiteral("Reading list"));
    QTRY_COMPARE_WITH_TIMEOUT(c.viewTitle(), QStringLiteral("Reading list"), 5000);
    c.deleteCollection(study);
    QTRY_COMPARE_WITH_TIMEOUT(c.view(), LibraryController::View::Library, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(c.collections()->rowCount(), 0, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(c.books()->rowCount(), 2, 5000);  // The books stay.
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
}

// Moving a book to Trash while its extraction runs stops the SDK call at
// once (not after it finishes); restoring resumes it, and it publishes.
void TestLibraryController::trashStopsTheRunningJobAndRestoreResumesIt()
{
    QTemporaryDir dir;
    auto fake = std::make_shared<FakeExtractor>();
    fake->block = true;  // Runs until cancelled.
    LibraryController c;
    c.setProcessors(fake, std::make_shared<FakeAnalyzer>(fake), true);
    openAndWait(c, dir.path());
    c.importFiles({fixture("title-page.pdf")});
    QTRY_VERIFY_WITH_TIMEOUT(fake->started.load(), 20000);
    QTRY_COMPARE_WITH_TIMEOUT(c.books()->rowCount(), 1, 5000);
    const QString book = c.books()->bookIdAt(0);

    c.moveToTrash(book);
    // The blocked call returns only because its cancel flag was raised.
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(c.trashCount(), 1, 5000);
    const int row = rowOfKind(c.jobs(), QStringLiteral("Title and authors"));
    QVERIFY(row >= 0);
    QTRY_COMPARE_WITH_TIMEOUT(jobData(c.jobs(), row, JobListModel::StateTextRole).toString(),
                              QStringLiteral("Book moved to Trash"), 5000);
    QVERIFY(!jobData(c.jobs(), row, JobListModel::BookTitleRole).toString().isEmpty());  // Title still known.

    fake->block = false;
    fake->started = false;
    c.restoreFromTrash(book);
    QTRY_VERIFY_WITH_TIMEOUT(fake->started.load(), 10000);  // Resumed without a restart.
    QTRY_COMPARE_WITH_TIMEOUT(titles(c.books()), QStringList{QStringLiteral("Extracted Title")}, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
}

// Importing a file whose book is in Trash offers to restore it.
void TestLibraryController::aDuplicateInTrashCanBeRestored()
{
    QTemporaryDir dir;
    auto fake = std::make_shared<FakeExtractor>();
    LibraryController c;
    c.setProcessors(fake, std::make_shared<FakeAnalyzer>(fake), true);
    openAndWait(c, dir.path());
    c.importFiles({fixture("title-page.pdf")});
    QTRY_COMPARE_WITH_TIMEOUT(c.libraryCount(), 1, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 20000);
    c.moveToTrash(c.books()->bookIdAt(0));
    QTRY_COMPARE_WITH_TIMEOUT(c.trashCount(), 1, 5000);

    c.importFiles({fixture("title-page.pdf")});
    QTRY_COMPARE_WITH_TIMEOUT(c.trashedDuplicateCount(), 1, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
    QCOMPARE(c.libraryCount(), 0);
    c.restoreTrashedDuplicates();
    QTRY_COMPARE_WITH_TIMEOUT(c.trashCount(), 0, 5000);
    QCOMPARE(c.libraryCount(), 1);
    QCOMPARE(c.trashedDuplicateCount(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 10000);
}

QTEST_GUILESS_MAIN(TestLibraryController)
#include "tst_librarycontroller.moc"
