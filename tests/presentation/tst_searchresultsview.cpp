// Presentation: the real SearchResultsView.qml, offscreen, with a
// LibraryController. The highlighted result must always be the book the
// inspector shows, or nothing, when new results replace the old ones.
#include "catalog/catalog.h"
#include "catalog/library.h"
#include "presentation/librarycontroller.h"
#include "presentation/searchcontroller.h"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

Q_IMPORT_QML_PLUGIN(MyBooksLibrary_PresentationPlugin)

using namespace mbl::domain;
using mbl::presentation::LibraryController;
using mbl::presentation::SearchController;

namespace {

// A book whose only contents entry is `entry` (no metadata: found by contents).
BookId bookWithEntry(mbl::catalog::Library& library, int n, const QString& entry)
{
    return library
        .run([n, entry](QSqlDatabase& db) {
            NewBook book;
            book.asset.id = AssetId::create();
            book.asset.sha256 = QStringLiteral("%1").arg(n, 64, 10, QLatin1Char('0'));
            book.asset.byteSize = 1;
            book.asset.pageCount = 3;
            book.asset.managedPath = QStringLiteral("files/%1/source.pdf").arg(book.asset.id.toString());
            book.originalFileName = QStringLiteral("book %1.pdf").arg(n);
            book.originalPath = book.originalFileName;
            const BookId id = mbl::catalog::registerBook(db, book).value();
            const PublishTicket ticket = mbl::catalog::requestTocRun(db, id).value();
            TocAnalysis toc;
            toc.outcome = QStringLiteral("analysis_partial");
            TocEntry e;
            e.sdkEntryId = QStringLiteral("e0");
            e.title = entry;
            toc.entries << e;
            RunIdentity run;
            run.sourceSha256 = book.asset.sha256;
            run.sdkVersion = QStringLiteral("test");
            run.optionsJson = QStringLiteral("{}");
            run.outcome = toc.outcome;
            mbl::catalog::publishToc(db, ticket, run, toc).value();
            return id;
        })
        .result();
}

} // namespace

class TestSearchResultsView : public QObject {
    Q_OBJECT

private slots:
    void highlightFollowsTheChosenBook();
};

void TestSearchResultsView::highlightFollowsTheChosenBook()
{
    QTemporaryDir dir;
    BookId a;
    BookId b;
    {
        auto library = mbl::catalog::Library::open(dir.path());
        QVERIFY(library);
        a = bookWithEntry(*library.value(), 1, QStringLiteral("Alpha networking"));
        b = bookWithEntry(*library.value(), 2, QStringLiteral("Beta networking"));
    }
    LibraryController controller;
    controller.open(dir.path());
    QTRY_VERIFY_WITH_TIMEOUT(controller.ready() && !controller.busy(), 10000);
    SearchController* search = controller.search();

    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(MBL_SOURCE_DIR "/qml/search/SearchResultsView.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(480, 480);
    std::unique_ptr<QObject> object(
        component.createWithInitialProperties({{QStringLiteral("search"), QVariant::fromValue<QObject*>(search)}}));
    auto* view = qobject_cast<QQuickItem*>(object.get());
    QVERIFY2(view, qPrintable(component.errorString()));
    view->setParentItem(window.contentItem());
    view->setSize(QSizeF(480, 480));
    window.show();
    QSignalSpy chosen(view, SIGNAL(bookChosen(QString)));
    QSignalSpy applied(search, &SearchController::responseApplied);
    const auto query = [&](const QString& text) {
        const qsizetype before = applied.size();
        search->setText(text);
        search->refresh();
        QTRY_VERIFY_WITH_TIMEOUT(applied.size() > before, 5000);
        QCoreApplication::processEvents();
    };
    const auto current = [&] { return view->property("currentIndex").toInt(); };

    query(QStringLiteral("networking"));
    QCOMPARE(search->results()->rowCount(), 2);
    view->setProperty("currentIndex", search->results()->rowOfBook(a.toString()));  // The user picks A.
    QCOMPARE(chosen.size(), 1);
    QCOMPARE(chosen.first().first().toString(), a.toString());

    query(QStringLiteral("alpha"));  // A is still a result: it stays highlighted.
    QCOMPARE(current(), search->results()->rowOfBook(a.toString()));

    query(QStringLiteral("beta"));  // A is gone: nothing is highlighted, B is not "chosen".
    QCOMPARE(search->results()->rowCount(), 1);
    QCOMPARE(current(), -1);
    QCOMPARE(chosen.size(), 1);

    query(QStringLiteral("networking"));  // A re-run that brings A back highlights it where it now is.
    QCOMPARE(current(), search->results()->rowOfBook(a.toString()));
    QCOMPARE(chosen.last().first().toString(), a.toString());
    QVERIFY(b != a);
}

int main(int argc, char* argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TestSearchResultsView test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_searchresultsview.moc"
