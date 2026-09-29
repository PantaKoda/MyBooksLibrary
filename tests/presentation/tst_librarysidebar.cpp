// Presentation: the real LibrarySidebar.qml, offscreen, with a
// LibraryController: a collection created through its dialog, the views
// switched from its rows, and a collection deleted after confirming.
#include "presentation/collectionlistmodel.h"
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

using mbl::presentation::LibraryController;

namespace {

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

void click(QQuickItem* item)
{
    QVERIFY(item);
    QVERIFY(QMetaObject::invokeMethod(item, "clicked"));
}

} // namespace

class TestLibrarySidebar : public QObject {
    Q_OBJECT

private slots:
    void createSwitchAndDelete();
};

void TestLibrarySidebar::createSwitchAndDelete()
{
    QTemporaryDir dir;
    LibraryController controller;
    controller.open(dir.path());
    QTRY_VERIFY_WITH_TIMEOUT(controller.ready() && !controller.busy(), 10000);

    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(MBL_SOURCE_DIR "/qml/library/LibrarySidebar.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(300, 500);
    std::unique_ptr<QObject> object(component.createWithInitialProperties(
        {{QStringLiteral("library"), QVariant::fromValue<QObject*>(&controller)}}));
    auto* sidebar = qobject_cast<QQuickItem*>(object.get());
    QVERIFY2(sidebar, qPrintable(component.errorString()));
    sidebar->setParentItem(window.contentItem());
    sidebar->setSize(QSizeF(300, 500));
    window.show();
    QQuickItem* root = window.contentItem()->parentItem() ? window.contentItem()->parentItem() : window.contentItem();

    // New collection through the dialog.
    auto* nameDialog = sidebar->findChild<QObject*>(QStringLiteral("collectionNameDialog"));
    QVERIFY(nameDialog);
    click(findItem(root, QStringLiteral("newCollectionButton")));
    QTRY_VERIFY_WITH_TIMEOUT(nameDialog->property("opened").toBool(), 5000);
    findItem(root, QStringLiteral("collectionNameField"))->setProperty("text", QStringLiteral("Study"));
    click(findItem(root, QStringLiteral("collectionNameSaveButton")));
    QTRY_COMPARE_WITH_TIMEOUT(controller.collections()->rowCount(), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!nameDialog->property("visible").toBool(), 5000);

    // Its row shows the collection; clicking it shows it.
    auto* list = findItem(root, QStringLiteral("collectionList"));
    QVERIFY(list);
    QTRY_COMPARE_WITH_TIMEOUT(list->property("count").toInt(), 1, 5000);
    const QString id = controller.collections()->data(controller.collections()->index(0),
                                                      mbl::presentation::CollectionListModel::CollectionIdRole).toString();
    QTRY_VERIFY_WITH_TIMEOUT(findItem(root, QStringLiteral("collectionRow_0")), 5000);
    click(findItem(root, QStringLiteral("collectionRow_0")));  // The row itself.
    QCOMPARE(controller.view(), LibraryController::View::Collection);
    QCOMPARE(controller.viewTitle(), QStringLiteral("Study"));
    click(findItem(root, QStringLiteral("trashViewItem")));
    QCOMPARE(controller.view(), LibraryController::View::Trash);
    click(findItem(root, QStringLiteral("libraryViewItem")));
    QCOMPARE(controller.view(), LibraryController::View::Library);

    // A duplicate name is refused with a visible reason.
    click(findItem(root, QStringLiteral("newCollectionButton")));
    QTRY_VERIFY_WITH_TIMEOUT(nameDialog->property("opened").toBool(), 5000);
    findItem(root, QStringLiteral("collectionNameField"))->setProperty("text", QStringLiteral("STUDY"));
    click(findItem(root, QStringLiteral("collectionNameSaveButton")));
    QTRY_VERIFY_WITH_TIMEOUT(findItem(root, QStringLiteral("organizeErrorLabel"))->isVisible(), 5000);
    QCOMPARE(controller.collections()->rowCount(), 1);

    QMetaObject::invokeMethod(nameDialog, "close");
    QTRY_VERIFY_WITH_TIMEOUT(!nameDialog->property("visible").toBool(), 5000);

    // Keyboard: F2 renames, Delete asks to delete (no mouse needed).
    QQuickItem* row = findItem(root, QStringLiteral("collectionRow_0"));
    row->forceActiveFocus();
    QTRY_VERIFY_WITH_TIMEOUT(row->hasActiveFocus(), 5000);
    QTest::keyClick(&window, Qt::Key_F2);
    QTRY_VERIFY_WITH_TIMEOUT(nameDialog->property("opened").toBool(), 5000);
    QCOMPARE(findItem(root, QStringLiteral("collectionNameField"))->property("text").toString(), QStringLiteral("Study"));
    // A name with markup stays plain text in the sidebar.
    findItem(root, QStringLiteral("collectionNameField"))->setProperty("text", QStringLiteral("<b>Bold</b> study"));
    click(findItem(root, QStringLiteral("collectionNameSaveButton")));
    QTRY_COMPARE_WITH_TIMEOUT(controller.collections()->data(controller.collections()->index(0), Qt::DisplayRole).toString(),
                              QStringLiteral("<b>Bold</b> study"), 5000);
    row = findItem(root, QStringLiteral("collectionRow_0"));
    auto* rowLabel = qvariant_cast<QQuickItem*>(row->property("contentItem"));
    QVERIFY(rowLabel);
    QCOMPARE(rowLabel->property("textFormat").toInt(), 0);  // Text.PlainText
    QVERIFY(rowLabel->property("text").toString().startsWith(QStringLiteral("<b>Bold</b> study")));

    auto* deleteDialog = sidebar->findChild<QObject*>(QStringLiteral("deleteCollectionDialog"));
    QVERIFY(deleteDialog);
    row->forceActiveFocus();
    QTRY_VERIFY_WITH_TIMEOUT(row->hasActiveFocus(), 5000);
    QTest::keyClick(&window, Qt::Key_Delete);
    QTRY_VERIFY_WITH_TIMEOUT(deleteDialog->property("opened").toBool(), 5000);
    QCOMPARE(findItem(root, QStringLiteral("deleteCollectionName"))->property("text").toString(), QStringLiteral("<b>Bold</b> study"));
    click(findItem(root, QStringLiteral("confirmDeleteCollectionButton")));
    QTRY_COMPARE_WITH_TIMEOUT(controller.collections()->rowCount(), 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 10000);
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
    TestLibrarySidebar test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_librarysidebar.moc"
