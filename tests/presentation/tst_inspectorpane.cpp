// Presentation: the real BookInspectorPane.qml, loaded in an offscreen window
// with a LibraryController. Guards what only the view can get wrong, such as
// details of a previous book's entry staying on screen.
#include "catalog/catalog.h"
#include "catalog/library.h"
#include "presentation/bookinspector.h"
#include "presentation/librarycontroller.h"

#include <QGuiApplication>
#include <QPointer>
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

// Any item in the window with that object name, including popups.
QQuickItem* findItem(QQuickItem* root, const QString& name)
{
    if (!root)
        return nullptr;
    if (root->objectName() == name)
        return root;
    for (QQuickItem* child : root->childItems()) {
        if (QQuickItem* found = findItem(child, name))
            return found;
    }
    return nullptr;
}

QVariantMap fieldOf(BookInspector* inspector, const QString& code)
{
    for (const QVariant& f : inspector->metadataFields()) {
        if (f.toMap().value(QStringLiteral("field")).toString() == code)
            return f.toMap();
    }
    return {};
}

void click(QQuickItem* button)
{
    QVERIFY(button);
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
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
    void correctionDialogSavesAndSurvivesRefreshes();
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

// The correction editor: typing survives a refresh of the inspector (e.g. a
// job publishing), Save stores the value for the book it was opened for,
// contributors keep their typed names through add and reorder, and a refused
// year keeps the dialog open with the reason.
void TestInspectorPane::correctionDialogSavesAndSurvivesRefreshes()
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
        {{QStringLiteral("inspector"), QVariant::fromValue<QObject*>(controller.inspector())}}));
    auto* pane = qobject_cast<QQuickItem*>(object.get());
    QVERIFY2(pane, qPrintable(component.errorString()));
    pane->setParentItem(window.contentItem());
    pane->setSize(QSizeF(640, 640));
    window.show();
    QQuickItem* root = window.contentItem()->parentItem() ? window.contentItem()->parentItem() : window.contentItem();

    BookInspector* inspector = controller.inspector();
    QSignalSpy loaded(inspector, &BookInspector::loaded);
    QSignalSpy corrected(inspector, &BookInspector::corrected);
    inspector->select(a.toString());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 5000);
    auto* dialog = pane->findChild<QObject*>(QStringLiteral("correctionDialog"));
    QVERIFY(dialog);

    // Title: typed text survives a refresh; saved for book A even though B
    // is selected before Save.
    QVERIFY(QMetaObject::invokeMethod(pane, "correct", Q_ARG(QVariant, fieldOf(inspector, QStringLiteral("title")))));
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 5000);
    QPointer<QQuickItem> field = findItem(root, QStringLiteral("correctionValueField"));
    QVERIFY(field);
    field->setProperty("text", QStringLiteral("Typed Title"));
    controller.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 2, 5000);
    QVERIFY(field);  // Not recreated by the refresh.
    QCOMPARE(field->property("text").toString(), QStringLiteral("Typed Title"));
    inspector->select(b.toString());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 3, 5000);
    click(findItem(root, QStringLiteral("correctionSaveButton")));
    QTRY_COMPARE_WITH_TIMEOUT(corrected.size(), 1, 5000);
    QCOMPARE(corrected.first().first().toString(), a.toString());
    QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("visible").toBool(), 5000);
    QCOMPARE(fieldOf(inspector, QStringLiteral("title")).value(QStringLiteral("mode")).toString(), QStringLiteral("auto"));  // B unchanged.
    inspector->select(a.toString());
    QTRY_COMPARE_WITH_TIMEOUT(fieldOf(inspector, QStringLiteral("title")).value(QStringLiteral("value")).toString(),
                              QStringLiteral("Typed Title"), 5000);

    // Contributors: names typed before Add and Move up are kept, in the new order.
    QVERIFY(QMetaObject::invokeMethod(pane, "correct", Q_ARG(QVariant, fieldOf(inspector, QStringLiteral("contributors")))));
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(findItem(root, QStringLiteral("contributorName_0")), 5000);
    findItem(root, QStringLiteral("contributorName_0"))->setProperty("text", QStringLiteral("Grace Hopper"));
    click(findItem(root, QStringLiteral("addPersonButton")));
    QTRY_VERIFY_WITH_TIMEOUT(findItem(root, QStringLiteral("contributorName_1")), 5000);
    QCOMPARE(findItem(root, QStringLiteral("contributorName_0"))->property("text").toString(), QStringLiteral("Grace Hopper"));
    findItem(root, QStringLiteral("contributorName_1"))->setProperty("text", QStringLiteral("Alan Turing"));
    click(findItem(root, QStringLiteral("moveUp_1")));
    QTRY_COMPARE_WITH_TIMEOUT(findItem(root, QStringLiteral("contributorName_0"))->property("text").toString(),
                              QStringLiteral("Alan Turing"), 5000);
    // Typed after the last reorder: Save must read the rows as they are now.
    findItem(root, QStringLiteral("contributorName_1"))->setProperty("text", QStringLiteral("Grace B. Hopper"));
    click(findItem(root, QStringLiteral("correctionSaveButton")));
    QTRY_COMPARE_WITH_TIMEOUT(corrected.size(), 2, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(fieldOf(inspector, QStringLiteral("contributors")).value(QStringLiteral("value")).toString(),
                              QStringLiteral("Alan Turing (author); Grace B. Hopper (author)"), 5000);

    // A long list scrolls: Save stays in the window, a new row is scrolled
    // into view, and a name typed in it (far below the first rows) is saved.
    QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("visible").toBool(), 5000);
    QVERIFY(QMetaObject::invokeMethod(pane, "correct", Q_ARG(QVariant, fieldOf(inspector, QStringLiteral("contributors")))));
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 5000);
    for (int i = 0; i < 25; ++i)
        click(findItem(root, QStringLiteral("addPersonButton")));
    const QString last = QStringLiteral("contributorName_26");
    QTRY_VERIFY_WITH_TIMEOUT(findItem(root, last), 5000);
    const QRectF windowRect(0, 0, window.width(), window.height());
    const auto sceneRect = [](QQuickItem* item) {
        return item ? item->mapRectToScene(QRectF(0, 0, item->width(), item->height())) : QRectF();
    };
    // Measure only once the rows are laid out (polished), not their initial geometry.
    QTRY_VERIFY_WITH_TIMEOUT(sceneRect(findItem(root, last)).width() > 100, 5000);
    QTest::qWait(100);
    QTRY_VERIFY2_WITH_TIMEOUT(windowRect.contains(sceneRect(findItem(root, QStringLiteral("correctionSaveButton")))),
                              qPrintable(QStringLiteral("Save at y %1").arg(sceneRect(findItem(root, QStringLiteral("correctionSaveButton"))).top())),
                              5000);
    QVERIFY(windowRect.contains(sceneRect(findItem(root, QStringLiteral("addPersonButton")))));
    QVERIFY(findItem(root, QStringLiteral("contributorRows")));
    QTRY_VERIFY_WITH_TIMEOUT(sceneRect(findItem(root, QStringLiteral("contributorRows"))).contains(sceneRect(findItem(root, last))), 5000);
    QVERIFY(findItem(root, last)->hasActiveFocus());
    findItem(root, last)->setProperty("text", QStringLiteral("Last Person"));
    click(findItem(root, QStringLiteral("correctionSaveButton")));
    QTRY_COMPARE_WITH_TIMEOUT(corrected.size(), 3, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(fieldOf(inspector, QStringLiteral("contributors")).value(QStringLiteral("value")).toString(),
                              QStringLiteral("Alan Turing (author); Grace B. Hopper (author); Last Person (author)"), 5000);

    // A refused year: nothing saved, the dialog stays open with the reason.
    QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("visible").toBool(), 5000);
    QVERIFY(QMetaObject::invokeMethod(pane, "correct", Q_ARG(QVariant, fieldOf(inspector, QStringLiteral("publication_year")))));
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 5000);
    field = findItem(root, QStringLiteral("correctionValueField"));
    QVERIFY(field);
    field->setProperty("text", QStringLiteral("19x5"));
    click(findItem(root, QStringLiteral("correctionSaveButton")));
    QTest::qWait(200);
    QVERIFY(dialog->property("visible").toBool());
    QVERIFY(findItem(root, QStringLiteral("correctionErrorLabel"))->isVisible());
    QCOMPARE(corrected.size(), 3);

    // Leave empty from the same dialog.
    click(findItem(root, QStringLiteral("correctionClearButton")));
    QTRY_COMPARE_WITH_TIMEOUT(corrected.size(), 4, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(fieldOf(inspector, QStringLiteral("publication_year")).value(QStringLiteral("mode")).toString(),
                              QStringLiteral("cleared"), 5000);
    // Reopening starts without the earlier error.
    QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("visible").toBool(), 5000);
    QVERIFY(QMetaObject::invokeMethod(pane, "correct", Q_ARG(QVariant, fieldOf(inspector, QStringLiteral("edition")))));
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 5000);
    QVERIFY(inspector->correctionError().isEmpty());
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
