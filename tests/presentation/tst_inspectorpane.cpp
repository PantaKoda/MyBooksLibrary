// Presentation: the real BookInspectorPane.qml, loaded in an offscreen window
// with a LibraryController. Guards what only the view can get wrong, such as
// details of a previous book's entry staying on screen.
#include "catalog/catalog.h"
#include "catalog/library.h"
#include "presentation/bookinspector.h"
#include "presentation/collectionlistmodel.h"
#include "presentation/librarycontroller.h"

#include <QFont>
#include <QGuiApplication>
#include <QItemSelectionModel>
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

// The item with that object name whose text starts with `text`.
QQuickItem* findItemWithText(QQuickItem* root, const QString& name, const QString& text)
{
    if (!root)
        return nullptr;
    if (root->objectName() == name && root->property("text").toString().startsWith(text))
        return root;
    for (QQuickItem* child : root->childItems()) {
        if (QQuickItem* found = findItemWithText(child, name, text))
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
    void contentsEditingInThePane();
    void addToCollectionMenuFollowsTheCollections();
    void manyReasonsLeaveTheTreeInView();
    void unreadableContentsSayWhatHappened();
    void uncertainPagesLeaveTheTitleInView();
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
    // The layout settles after Add (as it does late on a slow machine: the CI
    // failure on PR #25): the rows' visible height changes, and the new row
    // stays in view.
    QQuickItem* editor = findItem(root, QStringLiteral("contributorRows"))->parentItem()->parentItem();
    QVERIFY(editor && editor->setProperty("maximumRowsHeight", 150));
    QTRY_VERIFY_WITH_TIMEOUT(sceneRect(findItem(root, QStringLiteral("contributorRows"))).height() < 200, 5000);
    QTRY_VERIFY2_WITH_TIMEOUT(
        sceneRect(findItem(root, QStringLiteral("contributorRows"))).contains(sceneRect(findItem(root, last))),
        qPrintable(QStringLiteral("rows %1, last row %2")
                       .arg(QDebug::toString(sceneRect(findItem(root, QStringLiteral("contributorRows")))),
                            QDebug::toString(sceneRect(findItem(root, last))))),
        5000);
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

// Contents editing in the real pane: the entry dialog renames the current
// entry, which stays current after the tree is rebuilt; a refused page keeps
// the dialog open; the banner offers Discard, which shows the analysis again.
void TestInspectorPane::contentsEditingInThePane()
{
    QTemporaryDir dir;
    BookId a;
    {
        auto library = mbl::catalog::Library::open(dir.path());
        QVERIFY(library);
        a = bookWithOneEntry(*library.value(), 1, QStringLiteral("Alpha chapter"));
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
         {QStringLiteral("selectFirstEntry"), true}}));  // Contents tab; first entry current.
    auto* pane = qobject_cast<QQuickItem*>(object.get());
    QVERIFY2(pane, qPrintable(component.errorString()));
    pane->setParentItem(window.contentItem());
    pane->setSize(QSizeF(640, 640));
    window.show();
    QQuickItem* root = window.contentItem()->parentItem() ? window.contentItem()->parentItem() : window.contentItem();

    BookInspector* inspector = controller.inspector();
    QSignalSpy corrected(inspector, &BookInspector::corrected);
    inspector->select(a.toString());
    QTRY_COMPARE_WITH_TIMEOUT(headingText(pane), QStringLiteral("Alpha chapter · Page 1"), 5000);
    auto* banner = findItem(root, QStringLiteral("contentsEditBanner"));
    QVERIFY(banner);
    QVERIFY(!banner->isVisible());

    // A second entry, made current: the one edited below.
    inspector->addEntryAfter(a.toString(), QStringLiteral("e0"), QStringLiteral("Beta chapter"), QStringLiteral("2"));
    QTRY_COMPARE_WITH_TIMEOUT(corrected.size(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(inspector->contents()->entryCount(), 2, 5000);
    auto* tree = findItem(root, QStringLiteral("contentsTree"));
    QVERIFY(tree);
    auto* selection = qvariant_cast<QItemSelectionModel*>(tree->property("selectionModel"));
    QVERIFY(selection);
    QTRY_COMPARE_WITH_TIMEOUT(headingText(pane), QStringLiteral("Alpha chapter · Page 1"), 5000);
    selection->setCurrentIndex(inspector->contents()->index(1, 0), QItemSelectionModel::NoUpdate);
    QTRY_COMPARE_WITH_TIMEOUT(headingText(pane), QStringLiteral("Beta chapter · Page 2"), 5000);

    // Rename through the dialog.
    auto* entryDialog = pane->findChild<QObject*>(QStringLiteral("entryDialog"));
    QVERIFY(entryDialog);
    click(findItem(root, QStringLiteral("renameEntryButton")));
    QTRY_VERIFY_WITH_TIMEOUT(entryDialog->property("opened").toBool(), 5000);
    QCOMPARE(findItem(root, QStringLiteral("entryTitleField"))->property("text").toString(), QStringLiteral("Beta chapter"));
    findItem(root, QStringLiteral("entryTitleField"))->setProperty("text", QStringLiteral("Beta, revised"));
    click(findItem(root, QStringLiteral("entrySaveButton")));
    QTRY_COMPARE_WITH_TIMEOUT(corrected.size(), 2, 5000);
    // The same entry is current again in the rebuilt tree, not the first one.
    QTRY_COMPARE_WITH_TIMEOUT(headingText(pane), QStringLiteral("Beta, revised · Page 2"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(banner->isVisible(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!entryDialog->property("visible").toBool(), 5000);

    // A page outside the book: the dialog stays open with the reason.
    click(findItem(root, QStringLiteral("setPageButton")));
    QTRY_VERIFY_WITH_TIMEOUT(entryDialog->property("opened").toBool(), 5000);
    QCOMPARE(findItem(root, QStringLiteral("entryPageField"))->property("text").toString(), QStringLiteral("2"));
    findItem(root, QStringLiteral("entryPageField"))->setProperty("text", QStringLiteral("99"));
    click(findItem(root, QStringLiteral("entrySaveButton")));
    QTest::qWait(200);
    QVERIFY(entryDialog->property("visible").toBool());
    QCOMPARE(inspector->contentsError(), QStringLiteral("Enter a page number from 1 to 3."));
    QCOMPARE(corrected.size(), 2);
    QMetaObject::invokeMethod(entryDialog, "close");
    QTRY_VERIFY_WITH_TIMEOUT(!entryDialog->property("visible").toBool(), 5000);

    // Discard, after confirming.
    auto* discardDialog = pane->findChild<QObject*>(QStringLiteral("discardDialog"));
    QVERIFY(discardDialog);
    click(findItem(root, QStringLiteral("discardEditsButton")));
    QTRY_VERIFY_WITH_TIMEOUT(discardDialog->property("opened").toBool(), 5000);
    click(findItem(root, QStringLiteral("confirmDiscardButton")));
    QTRY_COMPARE_WITH_TIMEOUT(corrected.size(), 3, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!banner->isVisible(), 5000);
    QVERIFY(!inspector->contentsEdited());
    QCOMPARE(inspector->contents()->entryCount(), 1);
    QCOMPARE(inspector->contents()->data(inspector->contents()->index(0, 0), Qt::DisplayRole).toString(),
             QStringLiteral("Alpha chapter"));
}

// The More menu's "Add to collection" submenu lists the library's
// collections as they change, and a choice asks for that collection.
// A long list of analysis reasons (one per entry without a page, as for a
// 746-page book with 17 of them) never pushes the contents tree out of the
// pane: the reasons are shown on request, in a bounded area.
void TestInspectorPane::manyReasonsLeaveTheTreeInView()
{
    QTemporaryDir dir;
    BookId book;
    {
        auto library = mbl::catalog::Library::open(dir.path());
        QVERIFY(library);
        book = library.value()
                   ->run([](QSqlDatabase& db) {
                       NewBook b;
                       b.asset.id = AssetId::create();
                       b.asset.sha256 = QString(64, u'd');
                       b.asset.byteSize = 1;
                       b.asset.pageCount = 300;
                       b.asset.managedPath = QStringLiteral("files/%1/source.pdf").arg(b.asset.id.toString());
                       b.originalFileName = QStringLiteral("long.pdf");
                       b.originalPath = b.originalFileName;
                       const BookId id = mbl::catalog::registerBook(db, b).value();
                       const PublishTicket ticket = mbl::catalog::requestTocRun(db, id).value();
                       TocAnalysis toc;
                       toc.outcome = QStringLiteral("analysis_partial");
                       for (int i = 0; i < 80; ++i) {
                           TocEntry e;
                           e.sdkEntryId = QStringLiteral("e%1").arg(i);
                           e.order = i;
                           e.title = QStringLiteral("%1 Chapter title number %1").arg(i + 1);
                           e.hierarchy = HierarchyState::Root;
                           if (i % 2 == 0) {
                               e.destinationState = DestinationState::Resolved;
                               e.destinationPage = i;
                           } else {
                               toc.planBlockers << QStringLiteral("Entry '%1' is unresolved: No justified destination").arg(e.title);
                           }
                           toc.entries << e;
                       }
                       RunIdentity run;
                       run.sourceSha256 = b.asset.sha256;
                       run.sdkVersion = QStringLiteral("test");
                       run.optionsJson = QStringLiteral("{}");
                       run.outcome = toc.outcome;
                       mbl::catalog::publishToc(db, ticket, run, toc).value();
                       return id;
                   })
                   .result();
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
         {QStringLiteral("selectFirstEntry"), true}}));  // Contents tab.
    auto* pane = qobject_cast<QQuickItem*>(object.get());
    QVERIFY2(pane, qPrintable(component.errorString()));
    pane->setParentItem(window.contentItem());
    pane->setSize(QSizeF(640, 640));
    window.show();

    controller.inspector()->select(book.toString());
    QTRY_COMPARE_WITH_TIMEOUT(controller.inspector()->contents()->entryCount(), 80, 5000);
    QCOMPARE(controller.inspector()->contentsReasons().size(), 40);
    const auto sceneRect = [](QQuickItem* item) {
        return item ? item->mapRectToScene(QRectF(0, 0, item->width(), item->height())) : QRectF();
    };
    QQuickItem* tree = findItem(pane, QStringLiteral("contentsTree"));
    QVERIFY(tree);
    const auto treeInView = [&] {
        const QRectF r = sceneRect(tree);
        return tree->isVisible() && r.height() >= 120 && r.bottom() <= 640 + 0.5;
    };
    QTRY_VERIFY2_WITH_TIMEOUT(treeInView(), qPrintable(QDebug::toString(sceneRect(tree))), 5000);
    // The reasons, shown on request, stay bounded: the tree keeps its room.
    QQuickItem* why = findItem(pane, QStringLiteral("contentsReasonsButton"));
    QVERIFY(why && why->isVisible());
    QVERIFY(why->property("text").toString().contains(QStringLiteral("40")));
    auto* popup = pane->findChild<QObject*>(QStringLiteral("contentsReasonsPopup"));
    QVERIFY(popup);
    // Opening the reasons never moves the tree, even in a small pane (about
    // the app's minimum window).
    for (const QSizeF size : {QSizeF(640, 640), QSizeF(300, 300)}) {
        pane->setSize(size);
        window.resize(size.toSize());
        QTest::qWait(100);
        const QRectF before = sceneRect(tree);
        QVERIFY(QMetaObject::invokeMethod(why, "clicked"));
        QTRY_VERIFY_WITH_TIMEOUT(popup->property("opened").toBool(), 5000);
        QTest::qWait(100);
        QCOMPARE(sceneRect(tree), before);
        QVERIFY(QMetaObject::invokeMethod(why, "clicked"));  // Closes it again.
        QTRY_VERIFY_WITH_TIMEOUT(!popup->property("visible").toBool(), 5000);
    }
    QTRY_VERIFY2_WITH_TIMEOUT(treeInView(), qPrintable(QDebug::toString(sceneRect(tree))), 5000);
}

// Contents pages were found but none could be read (as for a scanned book
// whose table of contents gave two equally likely candidates): the summary
// says so in plain words, and the analysis's own note is one click away
// under a name that says what it is.
void TestInspectorPane::unreadableContentsSayWhatHappened()
{
    QTemporaryDir dir;
    BookId book;
    {
        auto library = mbl::catalog::Library::open(dir.path());
        QVERIFY(library);
        book = library.value()
                   ->run([](QSqlDatabase& db) {
                       NewBook b;
                       b.asset.id = AssetId::create();
                       b.asset.sha256 = QString(64, u'e');
                       b.asset.byteSize = 1;
                       b.asset.pageCount = 660;
                       b.asset.managedPath = QStringLiteral("files/%1/source.pdf").arg(b.asset.id.toString());
                       b.originalFileName = QStringLiteral("scan.pdf");
                       b.originalPath = b.originalFileName;
                       const BookId id = mbl::catalog::registerBook(db, b).value();
                       const PublishTicket ticket = mbl::catalog::requestTocRun(db, id).value();
                       TocAnalysis toc;
                       toc.outcome = QStringLiteral("analysis_partial");
                       toc.planBlockers << QStringLiteral("Candidates toc-p10-r11 (score 59.7) and toc-p13-r14 "
                                                          "(score 58.5) are too close; explicit selection required");
                       RunIdentity run;
                       run.sourceSha256 = b.asset.sha256;
                       run.sdkVersion = QStringLiteral("test");
                       run.optionsJson = QStringLiteral("{}");
                       run.outcome = toc.outcome;
                       mbl::catalog::publishToc(db, ticket, run, toc).value();
                       return id;
                   })
                   .result();
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
         {QStringLiteral("selectFirstEntry"), true}}));  // Contents tab.
    auto* pane = qobject_cast<QQuickItem*>(object.get());
    QVERIFY2(pane, qPrintable(component.errorString()));
    pane->setParentItem(window.contentItem());
    pane->setSize(QSizeF(640, 640));
    window.show();

    QSignalSpy loaded(controller.inspector(), &BookInspector::loaded);
    controller.inspector()->select(book.toString());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 5000);
    QCOMPARE(controller.inspector()->contents()->entryCount(), 0);
    QCOMPARE(controller.inspector()->contentsSummary(),
             QStringLiteral("Possible contents pages were found, but none could be read reliably."));
    QQuickItem* notes = findItem(pane, QStringLiteral("contentsReasonsButton"));
    QVERIFY(notes);
    QTRY_VERIFY_WITH_TIMEOUT(notes->isVisible(), 5000);
    QCOMPARE(notes->property("text").toString(), QStringLiteral("Analysis notes (1)"));
}

void TestInspectorPane::uncertainPagesLeaveTheTitleInView()
{
    // An entry with 40 possible pages, as the analysis gave for a book whose
    // printed page numbers skip pages: its row used to list them all, and the
    // list pushed the title out of view.
    QTemporaryDir dir;
    BookId book;
    {
        auto library = mbl::catalog::Library::open(dir.path());
        QVERIFY(library);
        book = library.value()
                   ->run([](QSqlDatabase& db) {
                       NewBook b;
                       b.asset.id = AssetId::create();
                       b.asset.sha256 = QString(64, u'e');
                       b.asset.byteSize = 1;
                       b.asset.pageCount = 640;
                       b.asset.managedPath = QStringLiteral("files/%1/source.pdf").arg(b.asset.id.toString());
                       b.originalFileName = QStringLiteral("skips.pdf");
                       b.originalPath = b.originalFileName;
                       const BookId id = mbl::catalog::registerBook(db, b).value();
                       const PublishTicket ticket = mbl::catalog::requestTocRun(db, id).value();
                       TocAnalysis toc;
                       toc.outcome = QStringLiteral("analysis_partial");
                       TocEntry uncertain;
                       uncertain.sdkEntryId = QStringLiteral("e0");
                       uncertain.title = QStringLiteral("1 Error Analysis");
                       uncertain.hierarchy = HierarchyState::Root;
                       uncertain.destinationState = DestinationState::Ambiguous;
                       for (int p = 0; p < 40; ++p)
                           uncertain.evidence.alternativePages << 24 + 12 * p;
                       TocEntry placed;
                       placed.sdkEntryId = QStringLiteral("e1");
                       placed.order = 1;
                       placed.title = QStringLiteral("2 Interpolation");
                       placed.hierarchy = HierarchyState::Root;
                       placed.destinationState = DestinationState::Resolved;
                       placed.destinationPage = 38;
                       toc.entries << uncertain << placed;
                       RunIdentity run;
                       run.sourceSha256 = b.asset.sha256;
                       run.sdkVersion = QStringLiteral("test");
                       run.optionsJson = QStringLiteral("{}");
                       run.outcome = toc.outcome;
                       mbl::catalog::publishToc(db, ticket, run, toc).value();
                       return id;
                   })
                   .result();
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
         {QStringLiteral("selectFirstEntry"), true}}));  // Contents tab.
    auto* pane = qobject_cast<QQuickItem*>(object.get());
    QVERIFY2(pane, qPrintable(component.errorString()));
    pane->setParentItem(window.contentItem());
    pane->setSize(QSizeF(640, 640));
    window.show();

    controller.inspector()->select(book.toString());
    QTRY_COMPARE_WITH_TIMEOUT(controller.inspector()->contents()->entryCount(), 2, 5000);
    QQuickItem* title = nullptr;
    QQuickItem* page = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((title = findItemWithText(pane, QStringLiteral("entryTitle"), QStringLiteral("1 Error"))), 5000);
    QTRY_VERIFY_WITH_TIMEOUT((page = findItemWithText(pane, QStringLiteral("entryPage"), QStringLiteral("Page uncertain"))),
                             5000);
    QCOMPARE(page->property("text").toString(), QStringLiteral("Page uncertain (40 possible)"));
    QQuickItem* row = title->parentItem();
    QVERIFY(row && row == page->parentItem());
    // In a normal and in a narrow pane (about the app's minimum window), the
    // page text takes at most half the row and the title keeps its room.
    for (const QSizeF size : {QSizeF(640, 640), QSizeF(300, 300)}) {
        pane->setSize(size);
        window.resize(size.toSize());
        QTRY_VERIFY2_WITH_TIMEOUT(row->width() < size.width() && page->width() <= row->width() / 2 + 0.5,
                                  qPrintable(QStringLiteral("row %1, page %2").arg(row->width()).arg(page->width())),
                                  5000);
        QVERIFY2(title->width() >= 60, qPrintable(QStringLiteral("title %1").arg(title->width())));
        QVERIFY(title->isVisible());
    }
    QVERIFY(headingText(pane).startsWith(QStringLiteral("1 Error Analysis · Page uncertain (40 possible)")));
}

void TestInspectorPane::addToCollectionMenuFollowsTheCollections()
{
    QTemporaryDir dir;
    BookId a;
    {
        auto library = mbl::catalog::Library::open(dir.path());
        QVERIFY(library);
        a = bookWithOneEntry(*library.value(), 1, QStringLiteral("Alpha chapter"));
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
         {QStringLiteral("collections"), QVariant::fromValue<QObject*>(controller.collections())}}));
    auto* pane = qobject_cast<QQuickItem*>(object.get());
    QVERIFY2(pane, qPrintable(component.errorString()));
    pane->setParentItem(window.contentItem());
    pane->setSize(QSizeF(640, 640));
    window.show();
    controller.inspector()->select(a.toString());

    auto* menu = pane->findChild<QObject*>(QStringLiteral("addToCollectionMenu"));
    QVERIFY(menu);
    QCOMPARE(menu->property("count").toInt(), 0);
    controller.createCollection(QStringLiteral("Study"));
    controller.createCollection(QStringLiteral("Work"));
    QTRY_COMPARE_WITH_TIMEOUT(menu->property("count").toInt(), 2, 5000);

    QSignalSpy requested(pane, SIGNAL(addToCollectionRequested(QString)));
    QQuickItem* first = nullptr;
    QVERIFY(QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, first), Q_ARG(int, 0)));
    QVERIFY(first);
    QCOMPARE(first->property("text").toString(), QStringLiteral("Study"));  // By name.
    QVERIFY(QMetaObject::invokeMethod(first, "triggered"));
    QCOMPARE(requested.size(), 1);
    QCOMPARE(requested.first().first().toString(),
             controller.collections()->data(controller.collections()->index(0),
                                            mbl::presentation::CollectionListModel::CollectionIdRole).toString());
    controller.deleteCollection(requested.first().first().toString());
    QTRY_COMPARE_WITH_TIMEOUT(menu->property("count").toInt(), 1, 5000);
}

int main(int argc, char* argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    // The native Windows style asks the Windows theme API for each control,
    // which fails without a real window and floods the log; Qt Test fails a
    // run at 2,000 warnings. The Basic style needs no platform theme.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    TestInspectorPane test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_inspectorpane.moc"
