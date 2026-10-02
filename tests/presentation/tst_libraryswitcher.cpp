// Presentation (issue #30): Open library… through LibrarySwitcher, on real
// folders, with a launcher that records instead of starting a process. A
// chosen folder is checked off the GUI thread and never created; refusals
// say why and start nothing; an accepted library is started with
// --existing-library, the default library without it. openExisting() fails
// visibly for a missing library and creates nothing. The real
// OpenLibraryDialog.qml starts the library, and asks the window to close for
// "Instead of this library".
#include "catalog/library.h"
#include "presentation/librarycontroller.h"
#include "presentation/libraryswitcher.h"

#include <QCoreApplication>
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

using mbl::catalog::Library;
using mbl::presentation::LibraryController;
using mbl::presentation::LibrarySwitcher;

namespace {

struct Launch {
    QString program;
    QStringList arguments;
};

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

QStringList entriesOf(const QString& folder)
{
    return QDir(folder).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
}

} // namespace

class TestLibrarySwitcher : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void anotherLibraryStartsInANewProcess();
    void refusalsSayWhyAndStartNothing();
    void theDefaultLibrary();
    void whatTheWindowShows();
    void openExistingFailsForAMissingLibrary();
    void theDialogStartsTheLibraryAndClosesTheWindow();

private:
    LibrarySwitcher* switcher() { return m_controller->switcher(); }
    // Asks for `folder` and waits for the answer: the error, empty if started.
    QString openFolder(const QString& folder, bool newWindow = true);

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<LibraryController> m_controller;
    QString m_current;  // The library this window holds.
    QString m_other;    // Another library.
    QList<Launch> m_launches;
    bool m_launchSucceeds = true;
};

void TestLibrarySwitcher::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_current = QDir::cleanPath(m_dir->filePath(QStringLiteral("Current")));
    m_other = QDir::cleanPath(m_dir->filePath(QStringLiteral("MyBooksLibrary restored 2026-09-29")));
    QVERIFY(Library::open(m_other));  // Made, then closed again.
    m_launches.clear();
    m_launchSucceeds = true;
    m_controller = std::make_unique<LibraryController>();
    switcher()->setLauncher([this](const QString& program, const QStringList& arguments) {
        m_launches << Launch{program, arguments};
        return m_launchSucceeds;
    });
    switcher()->setStartup(QDir::cleanPath(m_dir->filePath(QStringLiteral("Default"))), QStringLiteral("command line"));
    m_controller->open(m_current);
    QTRY_VERIFY_WITH_TIMEOUT(m_controller->ready() && !m_controller->busy(), 10000);
}

void TestLibrarySwitcher::cleanup()
{
    m_controller.reset();
    m_dir.reset();
}

QString TestLibrarySwitcher::openFolder(const QString& folder, bool newWindow)
{
    QSignalSpy started(switcher(), &LibrarySwitcher::started);
    QSignalSpy answered(switcher(), &LibrarySwitcher::requestChanged);
    switcher()->openFolder(folder, newWindow);
    if (!QTest::qWaitFor([&] { return !switcher()->checking() && (!started.isEmpty() || !switcher()->error().isEmpty()); },
                         10000))
        return QStringLiteral("<no answer>");
    return switcher()->error();
}

void TestLibrarySwitcher::anotherLibraryStartsInANewProcess()
{
    QSignalSpy started(switcher(), &LibrarySwitcher::started);
    // As the folder dialog gives it: a URL.
    QCOMPARE(openFolder(QUrl::fromLocalFile(m_other).toString(), true), QString());
    QCOMPARE(started.size(), 1);
    QCOMPARE(started.at(0).at(0).toBool(), true);  // A new window: this one stays.
    QCOMPARE(started.at(0).at(1).toString(), QDir::toNativeSeparators(m_other));
    QCOMPARE(m_launches.size(), 1);
    QCOMPARE(m_launches.at(0).program, QCoreApplication::applicationFilePath());
    // Only an existing library, and remembered for the next start once it opens.
    QCOMPARE(m_launches.at(0).arguments,
             (QStringList{QStringLiteral("--library"), QDir::toNativeSeparators(m_other), QStringLiteral("--existing-library"),
                          QStringLiteral("--remember")}));

    // Instead of this library: the same start, then this window closes.
    QCOMPARE(openFolder(QDir::toNativeSeparators(m_other), false), QString());
    QCOMPARE(started.size(), 2);
    QCOMPARE(started.at(1).at(0).toBool(), false);
    QCOMPARE(m_launches.size(), 2);
    QVERIFY(m_controller->ready());  // This session is untouched until the window closes it.
}

void TestLibrarySwitcher::refusalsSayWhyAndStartNothing()
{
    const QString missing = m_dir->filePath(QStringLiteral("Moved library"));
    const QString empty = m_dir->filePath(QStringLiteral("Documents"));
    QVERIFY(QDir().mkpath(empty));
    const QString backup = m_dir->filePath(QStringLiteral("MyBooksLibrary backup 2026-09-29 120000"));
    QVERIFY(QDir().mkpath(backup));
    for (const char* name : {Library::kCatalogFileName, Library::kBackupManifestFileName}) {
        QFile f(QDir(backup).filePath(QLatin1StringView(name)));
        QVERIFY(f.open(QIODevice::WriteOnly) && f.write("x") == 1);
    }
    const QStringList backupBefore = entriesOf(backup);

    QVERIFY(openFolder(QString()).contains(QStringLiteral("Choose the folder")));
    QVERIFY(openFolder(QStringLiteral("Library")).contains(QStringLiteral("full path")));
    QVERIFY(openFolder(missing).contains(QStringLiteral("does not exist")));
    QVERIFY(!QFileInfo::exists(missing));  // Never created.
    QVERIFY(openFolder(empty).contains(QStringLiteral("is not a MyBooksLibrary library")));
    QVERIFY(entriesOf(empty).isEmpty());
    QVERIFY(QDir().mkpath(m_current + QStringLiteral("/files")));  // A library's own subfolder.
    QVERIFY(openFolder(m_current + QStringLiteral("/files")).contains(QStringLiteral("is not a MyBooksLibrary library")));
    QVERIFY(openFolder(backup).contains(QStringLiteral("is a MyBooksLibrary backup, not a library")));
    QCOMPARE(entriesOf(backup), backupBefore);
    // The library this window holds, however it is written.
    QString sameAsCurrent = QDir::toNativeSeparators(m_current + QStringLiteral("/files/.."));
#ifdef Q_OS_WIN
    sameAsCurrent = sameAsCurrent.toUpper();
#endif
    QVERIFY(openFolder(sameAsCurrent).contains(QStringLiteral("already open in this window")));
    QVERIFY(m_launches.isEmpty());

    // A process that cannot be started is said, too.
    m_launchSucceeds = false;
    QVERIFY(openFolder(m_other).contains(QStringLiteral("could not be started")));
    QCOMPARE(m_launches.size(), 1);
    switcher()->clearError();
    QVERIFY(switcher()->error().isEmpty());
}

void TestLibrarySwitcher::theDefaultLibrary()
{
    const QString defaultRoot = QDir::cleanPath(m_dir->filePath(QStringLiteral("Default")));
    QVERIFY(!switcher()->currentIsDefault());
    QCOMPARE(switcher()->defaultPath(), QDir::toNativeSeparators(defaultRoot));
    QSignalSpy started(switcher(), &LibrarySwitcher::started);
    switcher()->openDefault(false);
    QCOMPARE(started.size(), 1);
    QCOMPARE(started.at(0).at(0).toBool(), false);
    // Created on first use, as on a first start: no --existing-library.
    // Remembered, so the next start opens it rather than a library chosen before.
    QCOMPARE(m_launches.size(), 1);
    QCOMPARE(m_launches.at(0).arguments, (QStringList{QStringLiteral("--library"), QDir::toNativeSeparators(defaultRoot),
                                                      QStringLiteral("--remember")}));

    // A window on the default library does not start it again.
    switcher()->setStartup(m_current, QStringLiteral("default"));
    QVERIFY(switcher()->currentIsDefault());
    switcher()->openDefault(true);
    QVERIFY(switcher()->error().contains(QStringLiteral("already open in this window")));
    QCOMPARE(m_launches.size(), 1);
}

void TestLibrarySwitcher::whatTheWindowShows()
{
    QCOMPARE(switcher()->currentPath(), QDir::toNativeSeparators(m_current));
    QCOMPARE(switcher()->currentName(), QStringLiteral("Current"));  // The window title uses it.
    QCOMPARE(switcher()->sourceText(), QStringLiteral("opened with --library"));
    QCOMPARE(m_controller->libraryPath(), switcher()->currentPath());
    switcher()->setStartup(switcher()->defaultPath(), QStringLiteral("default"));
    QCOMPARE(switcher()->sourceText(), QStringLiteral("your default library"));
    switcher()->setStartup(switcher()->defaultPath(), QStringLiteral("environment"));
    QCOMPARE(switcher()->sourceText(), QStringLiteral("set by MYBOOKSLIBRARY_ROOT"));
    switcher()->setStartup(switcher()->defaultPath(), QStringLiteral("remembered"));
    QCOMPARE(switcher()->sourceText(), QStringLiteral("the library you opened last"));
}

void TestLibrarySwitcher::openExistingFailsForAMissingLibrary()
{
    const QString moved = m_dir->filePath(QStringLiteral("Moved library"));
    LibraryController c;
    c.openExisting(moved);
    QTRY_VERIFY_WITH_TIMEOUT(c.failed() && !c.busy(), 10000);
    QVERIFY2(c.statusText().contains(QStringLiteral("does not exist")), qPrintable(c.statusText()));
    QVERIFY2(c.openError().startsWith(QStringLiteral("The folder ")), qPrintable(c.openError()));  // The reason alone.
    QVERIFY(!QFileInfo::exists(moved));  // Not silently made into a new, empty library.
    // The window still shows which library, and another one can be opened.
    QCOMPARE(c.switcher()->currentName(), QStringLiteral("Moved library"));
    QList<Launch> launched;
    c.switcher()->setLauncher([&launched](const QString& program, const QStringList& arguments) {
        launched << Launch{program, arguments};
        return true;
    });
    c.switcher()->openFolder(m_other, false);
    QTRY_COMPARE_WITH_TIMEOUT(launched.size(), 1, 10000);

    // An existing library opens as usual.
    LibraryController existing;
    existing.openExisting(m_other);
    QTRY_VERIFY_WITH_TIMEOUT(existing.ready() && !existing.busy(), 10000);
}

void TestLibrarySwitcher::theDialogStartsTheLibraryAndClosesTheWindow()
{
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(MBL_SOURCE_DIR "/qml/library/OpenLibraryDialog.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(480, 360);  // Main.qml's minimum size.
    std::unique_ptr<QObject> dialog(component.createWithInitialProperties(
        {{QStringLiteral("switcher"), QVariant::fromValue<QObject*>(switcher())},
         {QStringLiteral("parent"), QVariant::fromValue<QObject*>(window.contentItem())}}));
    QVERIFY2(dialog, qPrintable(component.errorString()));
    window.show();
    QSignalSpy switchRequested(dialog.get(), SIGNAL(switchRequested()));
    const auto contentItem = [&] { return qvariant_cast<QQuickItem*>(dialog->property("contentItem")); };
    const auto click = [&](const char* name) {
        QQuickItem* button = findItem(contentItem(), QString::fromLatin1(name));
        QVERIFY(button && button->isVisible() && button->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    };

    // A folder that is not a library: the reason shows, and the dialog stays.
    const QString empty = m_dir->filePath(QStringLiteral("Documents"));
    QVERIFY(QDir().mkpath(empty));
    QVERIFY(QMetaObject::invokeMethod(dialog.get(), "openFolder", Q_ARG(QVariant, QUrl::fromLocalFile(empty))));
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 5000);
    QCOMPARE(findItem(contentItem(), QStringLiteral("openLibraryFolder"))->property("text").toString(),
             QDir::toNativeSeparators(empty));
    click("openInNewWindowButton");
    QTRY_VERIFY_WITH_TIMEOUT(findItem(contentItem(), QStringLiteral("openLibraryError"))->isVisible(), 5000);
    QVERIFY(findItem(contentItem(), QStringLiteral("openLibraryError"))->property("text").toString().contains(
        QStringLiteral("is not a MyBooksLibrary library")));
    QVERIFY(dialog->property("opened").toBool());
    QVERIFY(m_launches.isEmpty());

    // A new window: started, the dialog closes, this window stays.
    QVERIFY(QMetaObject::invokeMethod(dialog.get(), "openFolder", Q_ARG(QVariant, QUrl::fromLocalFile(m_other))));
    QTRY_VERIFY_WITH_TIMEOUT(!findItem(contentItem(), QStringLiteral("openLibraryError"))->isVisible(), 5000);
    // For the PR: MBL_SCREENSHOT_DIR=<folder> saves the dialog, with a neutral
    // example path (the test's own folder names the machine's user).
    if (const QString shots = qEnvironmentVariable("MBL_SCREENSHOT_DIR"); !shots.isEmpty()) {
        dialog->setProperty("folder", QStringLiteral("D:\\Books\\MyBooksLibrary restored 2026-09-29"));
        QTest::qWait(200);
        window.grabWindow().save(QDir(shots).filePath(QStringLiteral("open-library-dialog.png")));
        dialog->setProperty("folder", QDir::toNativeSeparators(m_other));
    }
    click("openInNewWindowButton");
    QTRY_COMPARE_WITH_TIMEOUT(m_launches.size(), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("opened").toBool(), 5000);
    QCOMPARE(switchRequested.size(), 0);

    // Instead of this library: started, and the window is asked to close.
    QVERIFY(QMetaObject::invokeMethod(dialog.get(), "openFolder", Q_ARG(QVariant, QUrl::fromLocalFile(m_other))));
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 5000);
    click("openInsteadButton");
    QTRY_COMPARE_WITH_TIMEOUT(switchRequested.size(), 1, 5000);
    QCOMPARE(m_launches.size(), 2);
    QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("opened").toBool(), 5000);
}

int main(int argc, char* argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    // The Basic style needs no platform theme (see tst_librarysidebar).
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    TestLibrarySwitcher test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_libraryswitcher.moc"
