// Presentation (M10): the Back up… and Restore… session on a real library.
// The work runs off the GUI thread and reports back; refusals are explained;
// closing the window waits for a running backup, which leaves nothing
// half-made; and the real BackupDialog.qml backs up through the same session.
#include "presentation/backupcontroller.h"
#include "presentation/booklistmodel.h"
#include "presentation/librarycontroller.h"

#include <QDir>
#include <QFile>
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

using mbl::presentation::BackupController;
using mbl::presentation::LibraryController;

namespace {

QString fixture(const char* name)
{
    return QDir(QStringLiteral(MBL_FIXTURES_DIR)).filePath(QLatin1StringView(name));
}

QStringList entriesOf(const QString& folder)
{
    return QDir(folder).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
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

} // namespace

class TestBackupController : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void backsUpAndRestoresOffTheGuiThread();
    void refusalsAreExplained();
    void closingWaitsForTheBackup();
    void theDialogBacksUpAndRestores();

private:
    // Runs `start` and waits until the session reports it finished.
    bool runToEnd(const std::function<void()>& start);

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<LibraryController> m_controller;
    QString m_root;
    QString m_out;
};

void TestBackupController::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_root = m_dir->filePath(QStringLiteral("Library"));
    m_out = m_dir->filePath(QStringLiteral("Αντίγραφα"));
    QVERIFY(QDir().mkpath(m_out));
    m_controller = std::make_unique<LibraryController>();
    m_controller->open(m_root);
    QTRY_VERIFY_WITH_TIMEOUT(m_controller->ready(), 10000);
    m_controller->importFiles({fixture("contents-book.pdf")});
    QTRY_VERIFY_WITH_TIMEOUT(m_controller->books()->rowCount() == 1 && !m_controller->busy(), 20000);
}

void TestBackupController::cleanup()
{
    if (m_controller) {
        m_controller->prepareToClose();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->busy(), 30000);
    }
    m_controller.reset();
    m_dir.reset();
}

bool TestBackupController::runToEnd(const std::function<void()>& start)
{
    QSignalSpy finished(m_controller->backup(), &BackupController::finished);
    start();
    return QTest::qWaitFor([&] { return !finished.isEmpty(); }, 60000);
}

void TestBackupController::backsUpAndRestoresOffTheGuiThread()
{
    BackupController* b = m_controller->backup();
    QSignalSpy progress(b, &BackupController::progressChanged);
    QVERIFY(runToEnd([&] {
        b->backUp(QUrl::fromLocalFile(m_out).toString());  // A folder dialog's URL works too.
        QVERIFY(b->running());
        QVERIFY(m_controller->busy());  // Closing would wait for it.
    }));
    QVERIFY2(b->succeeded(), qPrintable(b->statusText()));
    QCOMPARE(b->operation(), BackupController::Operation::Backup);
    QVERIFY(b->statusText().startsWith(QStringLiteral("Backed up 1 book")));
    QVERIFY(!b->running());
    QVERIFY(!m_controller->busy());
    QVERIFY(progress.size() >= 2);
    QVERIFY(QFileInfo(b->resultFolder()).isDir());
    QCOMPARE(entriesOf(m_out).size(), 1);  // Only the backup, nothing partial.
    const QString backup = b->resultFolder();

    const QString target = QDir(m_dir->path()).filePath(QStringLiteral("Restored"));
    QVERIFY(runToEnd([&] { b->restore(backup, target); }));
    QVERIFY2(b->succeeded(), qPrintable(b->statusText()));
    QCOMPARE(b->operation(), BackupController::Operation::Restore);
    QVERIFY(b->statusText().startsWith(QStringLiteral("Restored 1 book")));
    QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(b->resultFolder())), QDir::cleanPath(target));
    QVERIFY(QFile::exists(QDir(target).filePath(QStringLiteral("library.sqlite"))));
    QVERIFY(b->resultFolderUrl().isLocalFile());

    b->reset();
    QCOMPARE(b->operation(), BackupController::Operation::None);
    QVERIFY(b->statusText().isEmpty());
    QVERIFY(!b->openRestoredLibrary());  // Nothing restored to open.
    QVERIFY(b->restoreFolderIn(QUrl::fromLocalFile(m_out)).contains(QStringLiteral("MyBooksLibrary restored ")));
}

void TestBackupController::refusalsAreExplained()
{
    BackupController* b = m_controller->backup();
    QVERIFY(runToEnd([&] { b->backUp(QDir(m_root).filePath(QStringLiteral("files"))); }));
    QVERIFY(!b->succeeded());
    QVERIFY2(b->statusText().contains(QStringLiteral("inside the library folder")), qPrintable(b->statusText()));

    QVERIFY(runToEnd([&] { b->backUp(m_out); }));
    QVERIFY(b->succeeded());
    const QString backup = b->resultFolder();
    // Never inside the library in use: its start-up recovery would remove it.
    const QString inside = QDir(m_root).filePath(QStringLiteral("staging/restored"));
    QVERIFY(runToEnd([&] { b->restore(backup, inside); }));
    QVERIFY(!b->succeeded());
    QVERIFY2(b->statusText().contains(QStringLiteral("library in use")), qPrintable(b->statusText()));
    QVERIFY(!QFileInfo::exists(inside));
    // A folder that is not a backup.
    QVERIFY(runToEnd([&] { b->restore(m_root, m_dir->filePath(QStringLiteral("R"))); }));
    QVERIFY(!b->succeeded());
    QVERIFY2(b->statusText().contains(QStringLiteral("not a MyBooksLibrary backup")), qPrintable(b->statusText()));
}

void TestBackupController::closingWaitsForTheBackup()
{
    BackupController* b = m_controller->backup();
    QSignalSpy finished(b, &BackupController::finished);
    b->backUp(m_out);
    m_controller->prepareToClose();  // As the window's close button does.
    QVERIFY(m_controller->busy() || !finished.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!m_controller->busy(), 30000);
    QCOMPARE(finished.size(), 1);
    // Cancelled, or finished just before: either way nothing is half-made.
    for (const QString& entry : entriesOf(m_out))
        QVERIFY2(!entry.startsWith(u'.'), qPrintable(entry));
    QVERIFY(b->succeeded() || b->statusText() == QStringLiteral("Backup cancelled; nothing was saved."));
}

void TestBackupController::theDialogBacksUpAndRestores()
{
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(MBL_SOURCE_DIR "/qml/backup/BackupDialog.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(700, 600);
    std::unique_ptr<QObject> dialog(component.createWithInitialProperties(
        {{QStringLiteral("backup"), QVariant::fromValue<QObject*>(m_controller->backup())},
         {QStringLiteral("parent"), QVariant::fromValue<QObject*>(window.contentItem())}}));
    QVERIFY2(dialog, qPrintable(component.errorString()));
    window.show();

    QVERIFY(QMetaObject::invokeMethod(dialog.get(), "openForBackup"));
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 5000);
    auto* content = qvariant_cast<QQuickItem*>(dialog->property("contentItem"));
    QQuickItem* folder = findItem(content, QStringLiteral("backupFolderField"));
    QVERIFY(folder && !folder->property("text").toString().isEmpty());  // Documents, suggested.
    // For the PR: MBL_SCREENSHOT_DIR=<folder> saves the dialog with an example folder.
    if (const QString shots = qEnvironmentVariable("MBL_SCREENSHOT_DIR"); !shots.isEmpty()) {
        folder->setProperty("text", QDir::toNativeSeparators(QStringLiteral("D:/Backups")));
        QTest::qWait(200);
        window.grabWindow().save(QDir(shots).filePath(QStringLiteral("backup-dialog.png")));
    }
    folder->setProperty("text", m_out);
    QQuickItem* start = findItem(content, QStringLiteral("startBackupButton"));
    QVERIFY(start && start->isVisible() && start->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(start, "clicked"));
    QTRY_VERIFY_WITH_TIMEOUT(m_controller->backup()->succeeded(), 60000);
    QQuickItem* status = findItem(content, QStringLiteral("backupStatus"));
    QVERIFY(status && status->property("text").toString().startsWith(QStringLiteral("Backed up")));
    QVERIFY(!start->isVisible());  // Done: Show folder and Close remain.
    const QString backup = m_controller->backup()->resultFolder();

    QVERIFY(QMetaObject::invokeMethod(dialog.get(), "openForRestore"));
    QQuickItem* from = findItem(content, QStringLiteral("restoreFromField"));
    QQuickItem* to = findItem(content, QStringLiteral("restoreToField"));
    QVERIFY(from && to);
    QVERIFY(to->property("text").toString().contains(QStringLiteral("MyBooksLibrary restored ")));  // Suggested.
    QVERIFY(!start->isEnabled());  // No backup chosen yet.
    from->setProperty("text", backup);
    to->setProperty("text", m_dir->filePath(QStringLiteral("From the dialog")));
    QTRY_VERIFY(start->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(start, "clicked"));
    QTRY_VERIFY_WITH_TIMEOUT(m_controller->backup()->succeeded(), 60000);
    QVERIFY(status->property("text").toString().startsWith(QStringLiteral("Restored")));
    QQuickItem* open = findItem(content, QStringLiteral("openRestoredButton"));
    QVERIFY(open && open->isVisible());
}

int main(int argc, char** argv)
{
    // Offscreen, the native Windows style floods theme warnings (see tst_inspectorpane).
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    TestBackupController test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_backupcontroller.moc"
