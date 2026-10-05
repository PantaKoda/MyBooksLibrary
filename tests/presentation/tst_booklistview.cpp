// Presentation: the real BookListView.qml, offscreen, with a LibraryController
// on imported fixtures. When the selected book leaves the list (moved to
// Trash, another view), the list's selection and the inspector must still
// show the same book (or none): rows change without a model reset. Each row
// shows its book's activity: a spinner while processed, a ring while waiting.
#include "presentation/bookinspector.h"
#include "presentation/booklistmodel.h"
#include "presentation/librarycontroller.h"

#include <QDir>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>

Q_IMPORT_QML_PLUGIN(MyBooksLibrary_PresentationPlugin)

using mbl::presentation::LibraryController;

namespace {

QString fixture(const char* name)
{
    return QDir(QStringLiteral(MBL_FIXTURES_DIR)).filePath(QLatin1StringView(name));
}

} // namespace

class TestBookListView : public QObject {
    Q_OBJECT

private slots:
    void selectionAndInspectorStayTogether();
    void rowsShowTheirActivity();
};

void TestBookListView::selectionAndInspectorStayTogether()
{
    QTemporaryDir dir;
    LibraryController controller;  // No processors: jobs wait, titles come from file names.
    controller.open(dir.path());
    QTRY_VERIFY_WITH_TIMEOUT(controller.ready() && !controller.busy(), 10000);
    controller.importFiles({fixture("title-page.pdf"), fixture("contents-book.pdf"), fixture("image-only.pdf")});
    QTRY_COMPARE_WITH_TIMEOUT(controller.libraryCount(), 3, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);

    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(MBL_SOURCE_DIR "/qml/library/BookListView.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(400, 500);
    std::unique_ptr<QObject> object(component.createWithInitialProperties(
        {{QStringLiteral("library"), QVariant::fromValue<QObject*>(&controller)}}));
    auto* list = qobject_cast<QQuickItem*>(object.get());
    QVERIFY2(list, qPrintable(component.errorString()));
    list->setParentItem(window.contentItem());
    list->setSize(QSizeF(400, 500));
    window.show();

    auto* books = controller.books();
    auto* inspector = controller.inspector();
    const auto consistent = [&] {
        const QString selected = list->property("selectedBookId").toString();
        return list->property("currentIndex").toInt() == books->rowOfBook(selected) && inspector->bookId() == selected;
    };

    // The middle book, selected.
    list->setProperty("currentIndex", 1);
    const QString middle = books->bookIdAt(1);
    QTRY_COMPARE_WITH_TIMEOUT(inspector->bookId(), middle, 5000);

    // Moved to Trash: its row goes; the list and the inspector agree, and
    // neither still points at it while the list shows another book.
    controller.moveToTrash(middle);
    QTRY_COMPARE_WITH_TIMEOUT(books->rowCount(), 2, 5000);
    QTRY_VERIFY2_WITH_TIMEOUT(consistent(),
                              qPrintable(QStringLiteral("index %1, selected %2, inspector %3")
                                             .arg(list->property("currentIndex").toInt())
                                             .arg(list->property("selectedBookId").toString(), inspector->bookId())),
                              5000);
    QVERIFY(list->property("selectedBookId").toString() != middle);

    // Another view without the selected book.
    list->setProperty("currentIndex", 0);
    QTRY_VERIFY_WITH_TIMEOUT(!inspector->bookId().isEmpty(), 5000);
    controller.showTrash();
    QTRY_COMPARE_WITH_TIMEOUT(books->rowCount(), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(consistent(), 5000);
    controller.showLibrary();
    QTRY_COMPARE_WITH_TIMEOUT(books->rowCount(), 2, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(consistent(), 5000);

    // While search results drive the inspector, the list leaves it alone.
    list->setProperty("drivesInspector", false);
    inspector->select(middle);
    list->setProperty("currentIndex", 1);
    QTest::qWait(100);
    QCOMPARE(inspector->bookId(), middle);
}

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

void TestBookListView::rowsShowTheirActivity()
{
    QTemporaryDir dir;
    LibraryController controller;  // No processors: every job waits.
    controller.open(dir.path());
    QTRY_VERIFY_WITH_TIMEOUT(controller.ready() && !controller.busy(), 10000);
    controller.importFiles({fixture("title-page.pdf"), fixture("contents-book.pdf")});
    QTRY_COMPARE_WITH_TIMEOUT(controller.libraryCount(), 2, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);

    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(MBL_SOURCE_DIR "/qml/library/BookListView.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(400, 200);
    std::unique_ptr<QObject> object(component.createWithInitialProperties(
        {{QStringLiteral("library"), QVariant::fromValue<QObject*>(&controller)}}));
    auto* list = qobject_cast<QQuickItem*>(object.get());
    QVERIFY(list);
    list->setParentItem(window.contentItem());
    list->setSize(QSizeF(400, 200));
    window.show();
    auto* books = controller.books();

    // Imported, jobs queued: both rows wait.
    QTRY_VERIFY_WITH_TIMEOUT(findItem(list, QStringLiteral("bookActivity_0")), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(books->data(books->index(0), mbl::presentation::BookListModel::ActivityRole).toString(),
                              QStringLiteral("waiting"), 5000);
    QTRY_VERIFY(findItem(list, QStringLiteral("bookActivity_0"))->isVisible());
    QTRY_VERIFY(findItem(list, QStringLiteral("bookActivity_1"))->isVisible());

    // The first book's metadata job starts: its row spins.
    mbl::domain::JobRecord running;
    running.id = mbl::domain::JobId::create();
    running.book = mbl::domain::BookId::fromString(books->bookIdAt(0));
    running.kind = mbl::domain::JobKind::Metadata;
    running.state = mbl::domain::JobState::Running;
    running.createdAt = QDateTime::currentDateTimeUtc().addSecs(60);
    running.updatedAt = running.createdAt;
    books->updateJob(running);
    QCOMPARE(books->activityOf(running.book), QStringLiteral("running"));
    QTRY_VERIFY(findItem(list, QStringLiteral("bookActivity_0"))->isVisible());

    // For the PR: MBL_SCREENSHOT_DIR=<folder> saves the list.
    if (const QString shots = qEnvironmentVariable("MBL_SCREENSHOT_DIR"); !shots.isEmpty()) {
        QTest::qWait(400);
        window.grabWindow().save(QDir(shots).filePath(QStringLiteral("book-activity.png")));
    }

    // Both jobs done: no indicator.
    running.state = mbl::domain::JobState::Succeeded;
    running.updatedAt = running.updatedAt.addSecs(5);
    books->updateJob(running);
    mbl::domain::JobRecord contents = running;
    contents.id = mbl::domain::JobId::create();
    contents.kind = mbl::domain::JobKind::Toc;
    contents.state = mbl::domain::JobState::Succeeded;
    books->updateJob(contents);
    QTRY_COMPARE_WITH_TIMEOUT(books->activityOf(running.book), QString(), 5000);
    QTRY_VERIFY(!findItem(list, QStringLiteral("bookActivity_0"))->isVisible());
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
    TestBookListView test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_booklistview.moc"
