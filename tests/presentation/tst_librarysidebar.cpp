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
    controller.showCollection(id);
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

    // Delete, after confirming.
    QVERIFY(QMetaObject::invokeMethod(sidebar, "askDelete", Q_ARG(QVariant, id), Q_ARG(QVariant, QStringLiteral("Study"))));
    auto* deleteDialog = sidebar->findChild<QObject*>(QStringLiteral("deleteCollectionDialog"));
    QVERIFY(deleteDialog);
    QTRY_VERIFY_WITH_TIMEOUT(deleteDialog->property("opened").toBool(), 5000);
    click(findItem(root, QStringLiteral("confirmDeleteCollectionButton")));
    QTRY_COMPARE_WITH_TIMEOUT(controller.collections()->rowCount(), 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 10000);
}

int main(int argc, char* argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TestLibrarySidebar test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_librarysidebar.moc"
