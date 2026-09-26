// Presentation: the library session opens, recovers and imports off the GUI
// thread, and updates its model only on the GUI thread.
#include "app/libraryroot.h"
#include "catalog/library.h"
#include "presentation/booklistmodel.h"
#include "presentation/librarycontroller.h"
#include "storage/importservice.h"

#include <QAbstractItemModelTester>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

using mbl::presentation::BookListModel;
using mbl::presentation::LibraryController;

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

QTEST_GUILESS_MAIN(TestLibraryController)
#include "tst_librarycontroller.moc"
