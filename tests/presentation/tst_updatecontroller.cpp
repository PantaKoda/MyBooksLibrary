// Presentation: UpdateController (checking, what may be installed, the
// notices after an update) with a local copy of GitHub's answer, and the
// real UpdateDialog.qml (offscreen). The install itself is covered by
// tst_updatedownloader and tst_updateinstall.
#include "presentation/updatecontroller.h"
#include "update/installinfo.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <functional>
#include <memory>

Q_IMPORT_QML_PLUGIN(MyBooksLibrary_PresentationPlugin)

using mbl::presentation::UpdateController;
using mbl::update::Version;

namespace {

const QString repo = QStringLiteral("PantaKoda/MyBooksLibrary");

QJsonObject release(const QString& version, const QString& notes)
{
    const QString tag = QStringLiteral("v") + version;
    const QString zip = QStringLiteral("MyBooksLibrary-%1-win64.zip").arg(version);
    const auto url = [&](const QString& name) {
        return QStringLiteral("https://github.com/%1/releases/download/%2/%3").arg(repo, tag, name);
    };
    return QJsonObject{
        {QStringLiteral("tag_name"), tag},
        {QStringLiteral("name"), QStringLiteral("MyBooksLibrary ") + version},
        {QStringLiteral("body"), notes},
        {QStringLiteral("draft"), false},
        {QStringLiteral("prerelease"), false},
        {QStringLiteral("published_at"), QStringLiteral("2026-10-05T10:00:00Z")},
        {QStringLiteral("html_url"), QStringLiteral("https://github.com/%1/releases/tag/%2").arg(repo, tag)},
        {QStringLiteral("assets"),
         QJsonArray{QJsonObject{{QStringLiteral("name"), zip}, {QStringLiteral("browser_download_url"), url(zip)}},
                    QJsonObject{{QStringLiteral("name"), zip + QStringLiteral(".sha256")},
                                {QStringLiteral("browser_download_url"), url(zip + QStringLiteral(".sha256"))}}}},
    };
}

void write(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
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

class TestUpdateController : public QObject {
    Q_OBJECT

private slots:
    void init();
    void findsNewerReleasesWithTheirNotes();
    void upToDate();
    void failedCheck();
    void sourceBuildsOfferThePage();
    void libraryInsideTheAppFolderIsRefused();
    void autoCheckIsRemembered();
    void noticesAfterAnUpdate();
    void handOverBeforeClosing();
    void theDialog();

private:
    std::unique_ptr<UpdateController> make(const Version& current, const QString& feed, QSettings* settings = nullptr);

    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_app;
    QString m_library;
    QNetworkAccessManager m_network;
};

void TestUpdateController::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_app = m_dir->filePath(QStringLiteral("Apps/MyBooksLibrary"));
    m_library = m_dir->filePath(QStringLiteral("Library"));
    QDir().mkpath(m_library);
    write(QDir(m_app).filePath(QStringLiteral("release.json")), mbl::update::releaseMarker(Version{0, 3, 0}));
    write(m_dir->filePath(QStringLiteral("feed.json")),
          QJsonDocument(QJsonArray{release(QStringLiteral("0.5.0"), QStringLiteral("Five <b>notes</b>")),
                                   release(QStringLiteral("0.3.0"), QStringLiteral("Three")),
                                   release(QStringLiteral("0.4.0"), QStringLiteral("Four"))})
              .toJson());
}

std::unique_ptr<UpdateController> TestUpdateController::make(const Version& current, const QString& feed, QSettings* settings)
{
    UpdateController::Config config;
    config.repository = repo;
    config.current = current;
    config.appFolder = m_app;
    config.dataFolder = m_dir->filePath(QStringLiteral("Data"));
    config.keptFolders = [this] { return QStringList{m_library}; };
    auto controller = std::make_unique<UpdateController>(config, settings, &m_network);
    controller->setReleasesUrl(QUrl::fromLocalFile(m_dir->filePath(feed)));
    return controller;
}

void TestUpdateController::findsNewerReleasesWithTheirNotes()
{
    auto updates = make(Version{0, 3, 0}, QStringLiteral("feed.json"));
    QCOMPARE(updates->currentVersion(), QStringLiteral("0.3.0"));
    updates->check();
    QCOMPARE(updates->state(), UpdateController::Checking);
    QTRY_COMPARE(updates->state(), UpdateController::Available);
    QVERIFY(updates->available());
    QCOMPARE(updates->latestVersion(), QStringLiteral("0.5.0"));
    const QVariantList releases = updates->releases();
    QCOMPARE(releases.size(), 2);
    QCOMPARE(releases.at(0).toMap().value(QStringLiteral("version")).toString(), QStringLiteral("0.5.0"));
    QCOMPARE(releases.at(0).toMap().value(QStringLiteral("notes")).toString(), QStringLiteral("Five <b>notes</b>"));
    QCOMPARE(releases.at(1).toMap().value(QStringLiteral("version")).toString(), QStringLiteral("0.4.0"));
    QVERIFY(updates->statusText().contains(QLatin1String("0.5.0")));
    QVERIFY2(updates->canInstall(), qPrintable(updates->cannotInstallReason()));
    QCOMPARE(updates->releasePage().toString(),
             QStringLiteral("https://github.com/PantaKoda/MyBooksLibrary/releases/tag/v0.5.0"));
}

void TestUpdateController::upToDate()
{
    write(QDir(m_app).filePath(QStringLiteral("release.json")), mbl::update::releaseMarker(Version{0, 5, 0}));
    auto updates = make(Version{0, 5, 0}, QStringLiteral("feed.json"));
    updates->check();
    QTRY_COMPARE(updates->state(), UpdateController::UpToDate);
    QVERIFY(!updates->available());
    QVERIFY(updates->statusText().contains(QLatin1String("latest")));
    QVERIFY(!updates->canInstall());
}

void TestUpdateController::failedCheck()
{
    write(m_dir->filePath(QStringLiteral("limit.json")), R"({"message": "API rate limit exceeded"})");
    auto updates = make(Version{0, 3, 0}, QStringLiteral("limit.json"));
    updates->check();
    QTRY_COMPARE(updates->state(), UpdateController::CheckFailed);
    QVERIFY(updates->statusText().startsWith(QLatin1String("Could not check for updates")));
    auto missing = make(Version{0, 3, 0}, QStringLiteral("missing.json"));
    missing->check();
    QTRY_COMPARE(missing->state(), UpdateController::CheckFailed);
}

void TestUpdateController::sourceBuildsOfferThePage()
{
    QFile::remove(QDir(m_app).filePath(QStringLiteral("release.json")));
    auto updates = make(Version{0, 3, 0}, QStringLiteral("feed.json"));
    updates->check();
    QTRY_COMPARE(updates->state(), UpdateController::Available);
    QVERIFY(!updates->canInstall());
    QVERIFY(updates->cannotInstallReason().contains(QLatin1String("not installed from a release")));
    // Install refuses with the same reason; nothing is downloaded.
    updates->install();
    QCOMPARE(updates->state(), UpdateController::InstallFailed);
    QVERIFY(updates->statusText().contains(QLatin1String("not installed from a release")));
    QVERIFY(!QFileInfo::exists(m_dir->filePath(QStringLiteral("Data/updates"))));
}

void TestUpdateController::libraryInsideTheAppFolderIsRefused()
{
    m_library = QDir(m_app).filePath(QStringLiteral("Library"));
    QDir().mkpath(m_library);
    auto updates = make(Version{0, 3, 0}, QStringLiteral("feed.json"));
    updates->check();
    QTRY_COMPARE(updates->state(), UpdateController::Available);
    QVERIFY(!updates->canInstall());
    QVERIFY(updates->cannotInstallReason().contains(QDir::toNativeSeparators(m_library)));
}

void TestUpdateController::autoCheckIsRemembered()
{
    QSettings settings(m_dir->filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    {
        auto updates = make(Version{0, 3, 0}, QStringLiteral("feed.json"), &settings);
        QVERIFY(updates->autoCheck());  // On by default.
        QSignalSpy changed(updates.get(), &UpdateController::autoCheckChanged);
        updates->setAutoCheck(false);
        QCOMPARE(changed.size(), 1);
        updates->check();
        QTRY_COMPARE(updates->state(), UpdateController::Available);
    }
    QCOMPARE(settings.value(QStringLiteral("updates/autoCheck")).toBool(), false);
    QVERIFY(settings.value(QStringLiteral("updates/lastCheck")).toDateTime().isValid());
    auto again = make(Version{0, 3, 0}, QStringLiteral("feed.json"), &settings);
    QVERIFY(!again->autoCheck());
}

void TestUpdateController::noticesAfterAnUpdate()
{
    auto updated = make(Version{0, 4, 0}, QStringLiteral("feed.json"));
    updated->handleStartArguments({QStringLiteral("app"), QStringLiteral("--updated-from"), QStringLiteral("0.3.0")});
    QCOMPARE(updated->notice(), QStringLiteral("Updated from 0.3.0 to 0.4.0."));
    QVERIFY(!updated->noticeIsError());
    updated->dismissNotice();
    QVERIFY(updated->notice().isEmpty());

    auto failed = make(Version{0, 3, 0}, QStringLiteral("feed.json"));
    failed->handleStartArguments({QStringLiteral("app"), QStringLiteral("--update-failed"),
                                  QStringLiteral("the app's folder is in use")});
    QVERIFY(failed->noticeIsError());
    QVERIFY(failed->notice().contains(QLatin1String("the app's folder is in use")));
    QVERIFY(failed->notice().contains(QLatin1String("update.log")));

    auto plain = make(Version{0, 3, 0}, QStringLiteral("feed.json"));
    plain->handleStartArguments({QStringLiteral("app")});
    QVERIFY(plain->notice().isEmpty());
}

// The verified copy is started while the window is still open; only then is
// the window asked to close. A copy that cannot start is reported, and the
// window stays (review of PR #43: it used to start after the window closed,
// so a failure left no trace).
void TestUpdateController::handOverBeforeClosing()
{
    auto updates = make(Version{0, 3, 0}, QStringLiteral("feed.json"));
    QSignalSpy restart(updates.get(), &UpdateController::restartRequested);
    const QString staged = m_dir->filePath(QStringLiteral("Data/updates/0.5.0/unpacked/MyBooksLibrary"));

    QString program;
    QStringList args;
    updates->setStarter([&](const QString& p, const QStringList& a, const QString&) {
        program = p;
        args = a;
        return false;  // For example quarantined by an antivirus.
    });
    QVERIFY(QMetaObject::invokeMethod(updates.get(), "onDownloaded", Q_ARG(QString, staged)));
    QCOMPARE(updates->state(), UpdateController::InstallFailed);
    QVERIFY(updates->statusText().contains(QLatin1String("could not be started")));
    QCOMPARE(restart.size(), 0);

    updates->setStarter([&](const QString& p, const QStringList& a, const QString&) {
        program = p;
        args = a;
        return true;
    });
    QVERIFY(QMetaObject::invokeMethod(updates.get(), "onDownloaded", Q_ARG(QString, staged)));
    QCOMPARE(updates->state(), UpdateController::ReadyToRestart);
    QCOMPARE(restart.size(), 1);
    QCOMPARE(QDir::cleanPath(program), QDir::cleanPath(staged + QStringLiteral("/appMyBooksLibrary.exe")));
    QCOMPARE(args.at(0), QStringLiteral("--apply-update"));
    QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(args.at(1))), QDir::cleanPath(m_app));
    QCOMPARE(args.at(args.indexOf(QStringLiteral("--wait-pid")) + 1), QString::number(QCoreApplication::applicationPid()));
    QCOMPARE(args.at(args.indexOf(QStringLiteral("--from-version")) + 1), QStringLiteral("0.3.0"));
    QVERIFY(!args.contains(QStringLiteral("--restart-library")));  // No currentLibrary in this config.
}

void TestUpdateController::theDialog()
{
    auto updates = make(Version{0, 3, 0}, QStringLiteral("feed.json"));
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(MBL_SOURCE_DIR "/qml/update/UpdateDialog.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(640, 520);
    std::unique_ptr<QObject> dialog(component.createWithInitialProperties(
        {{QStringLiteral("updates"), QVariant::fromValue<QObject*>(updates.get())},
         {QStringLiteral("parent"), QVariant::fromValue<QObject*>(window.contentItem())}}));
    QVERIFY2(dialog, qPrintable(component.errorString()));
    window.show();
    QVERIFY(QMetaObject::invokeMethod(dialog.get(), "openAndCheck"));
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 5000);
    QTRY_COMPARE(updates->state(), UpdateController::Available);
    QQuickItem* root = window.contentItem();
    const auto item = [&](const char* name) { return findItem(root, QString::fromLatin1(name)); };
    QTRY_VERIFY(item("installUpdateButton") && item("installUpdateButton")->isVisible());
    QVERIFY(!item("releasePageButton")->isVisible());
    QVERIFY(item("updateStatus")->property("text").toString().contains(QLatin1String("0.5.0")));
    // The notes are plain text: markup is shown, not rendered.
    bool notesShown = false;
    std::function<void(QQuickItem*)> visit = [&](QQuickItem* item) {
        if (item->property("text").toString() == QLatin1String("Five <b>notes</b>")
            && item->property("textFormat").toInt() == 0 /* PlainText */)
            notesShown = true;
        for (QQuickItem* child : item->childItems())
            visit(child);
    };
    visit(window.contentItem()->parentItem() ? window.contentItem()->parentItem() : window.contentItem());
    QVERIFY(notesShown);
    QVERIFY(item("autoCheckBox")->property("checked").toBool());

    // For the PR: MBL_SCREENSHOT_DIR=<folder> saves the dialog.
    if (const QString shots = qEnvironmentVariable("MBL_SCREENSHOT_DIR"); !shots.isEmpty()) {
        QTest::qWait(200);
        window.grabWindow().save(QDir(shots).filePath(QStringLiteral("update-dialog.png")));
    }

    // A copy built from source: no Install, the release page instead.
    QFile::remove(QDir(m_app).filePath(QStringLiteral("release.json")));
    QVERIFY(QMetaObject::invokeMethod(item("checkUpdatesButton"), "clicked"));
    QTRY_COMPARE(updates->state(), UpdateController::Available);
    QTRY_VERIFY(item("releasePageButton")->isVisible());
    QVERIFY(!item("installUpdateButton")->isVisible());
    QVERIFY(item("cannotInstallReason")->isVisible());
}

int main(int argc, char* argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication app(argc, argv);
    TestUpdateController test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_updatecontroller.moc"
