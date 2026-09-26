// Presentation: the library session opens, recovers and imports off the GUI
// thread, and updates its model only on the GUI thread.
#include "app/libraryroot.h"
#include "catalog/library.h"
#include "presentation/booklistmodel.h"
#include "presentation/librarycontroller.h"
#include "storage/importservice.h"

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
    QThread* modelThreadAtReset = nullptr;
    connect(c.books(), &QAbstractItemModel::modelReset, this,
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

QTEST_GUILESS_MAIN(TestLibraryController)
#include "tst_librarycontroller.moc"
