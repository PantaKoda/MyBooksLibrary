// Presentation: the real BookInspectorPane.qml, loaded in an offscreen window
// with a LibraryController. Guards what only the view can get wrong, such as
// details of a previous book's entry staying on screen.
#include "catalog/catalog.h"
#include "catalog/library.h"
#include "presentation/bookinspector.h"
#include "presentation/librarycontroller.h"

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
using mbl::presentation::BookInspector;
using mbl::presentation::LibraryController;

namespace {

// Registers a book with one resolved contents entry `title` on page index 0.
BookId bookWithOneEntry(mbl::catalog::Library& library, int n, const QString& title)
{
    return library
        .run([n, title](QSqlDatabase& db) {
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
            toc.outcome = QStringLiteral("plan_ready");
            toc.planReady = true;
            TocEntry e;
            e.sdkEntryId = QStringLiteral("e0");
            e.title = title;
            e.hierarchy = HierarchyState::Root;
            e.destinationState = DestinationState::Resolved;
            e.destinationPage = 0;
            e.inExportPlan = true;
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

QString headingText(QQuickItem* pane)
{
    auto* heading = pane->findChild<QQuickItem*>(QStringLiteral("entryHeading"));
    return heading ? heading->property("text").toString() : QStringLiteral("<no heading>");
}

} // namespace

class TestInspectorPane : public QObject {
    Q_OBJECT

private slots:
    void entryDetailsFollowTheSelectedBook();
};

void TestInspectorPane::entryDetailsFollowTheSelectedBook()
{
    QTemporaryDir dir;
    BookId a;
    BookId b;
    {
        auto library = mbl::catalog::Library::open(dir.path());
        QVERIFY(library);
        a = bookWithOneEntry(*library.value(), 1, QStringLiteral("Alpha chapter"));
        b = bookWithOneEntry(*library.value(), 2, QStringLiteral("Beta chapter"));
    }
    LibraryController controller;
    controller.open(dir.path());
    QTRY_VERIFY_WITH_TIMEOUT(controller.ready() && !controller.busy(), 10000);

    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(MBL_SOURCE_DIR "/qml/inspector/BookInspectorPane.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(640, 640);
    std::unique_ptr<QObject> object(component.createWithInitialProperties(
        {{QStringLiteral("inspector"), QVariant::fromValue<QObject*>(controller.inspector())},
         {QStringLiteral("selectFirstEntry"), true}}));  // Contents tab; first entry made current.
    auto* pane = qobject_cast<QQuickItem*>(object.get());
    QVERIFY2(pane, qPrintable(component.errorString()));
    pane->setParentItem(window.contentItem());
    pane->setSize(QSizeF(640, 640));
    window.show();

    BookInspector* inspector = controller.inspector();
    QSignalSpy loaded(inspector, &BookInspector::loaded);
    inspector->select(a.toString());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(headingText(pane), QStringLiteral("Alpha chapter · Page 1"), 5000);

    pane->setProperty("selectFirstEntry", false);  // Book B's tree gets no current entry.
    inspector->select(b.toString());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 2, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(headingText(pane), QStringLiteral("Select an entry to see where it points and why."), 5000);
    QCOMPARE(inspector->contents()->entryCount(), 1);
}

int main(int argc, char* argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TestInspectorPane test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_inspectorpane.moc"
