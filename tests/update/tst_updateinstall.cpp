// Updates: may this copy replace itself (checkInstall), and putting a new
// copy in place of the app's folder (applyUpdate), on real folders: the
// previous version kept as <folder>.previous, an older .previous replaced,
// and, when a file is in use, nothing changed or everything put back.
#include "update/installinfo.h"
#include "update/updateapplier.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

using namespace mbl::update;

namespace {

void write(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// A folder that looks like an unpacked release of `version`.
void makeApp(const QString& folder, const Version& version, const QByteArray& payload)
{
    write(QDir(folder).filePath(QStringLiteral("appMyBooksLibrary.exe")), "exe " + payload);
    write(QDir(folder).filePath(QStringLiteral("release.json")), releaseMarker(version));
    write(QDir(folder).filePath(QStringLiteral("models/det.onnx")), "model " + payload);
    write(QDir(folder).filePath(QStringLiteral("qml/QtQuick/qmldir")), "qmldir " + payload);
}

#ifdef Q_OS_WIN
// Opens `path` with no sharing, as a running program holds its files.
HANDLE lockFile(const QString& path)
{
    return CreateFileW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(path).utf16()), GENERIC_READ, 0,
                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}
#endif

} // namespace

class TestUpdateInstall : public QObject {
    Q_OBJECT

private slots:
    void releaseMarker();
    void installChecks();
    void replacesTheAppAndKeepsThePrevious();
    void refusesFoldersThatAreNotReleases();
    void fileInUseChangesNothing();
    void failedCopyPutsThePreviousBack();
};

void TestUpdateInstall::releaseMarker()
{
    QTemporaryDir dir;
    QVERIFY(!readReleaseMarker(dir.path()));
    write(dir.filePath(QStringLiteral("release.json")), mbl::update::releaseMarker(Version{0, 4, 0}));
    QCOMPARE(readReleaseMarker(dir.path()), (Version{0, 4, 0}));
    write(dir.filePath(QStringLiteral("release.json")), R"({"product": "Other", "version": "0.4.0"})");
    QVERIFY(!readReleaseMarker(dir.path()));
    write(dir.filePath(QStringLiteral("release.json")), "garbage");
    QVERIFY(!readReleaseMarker(dir.path()));
}

void TestUpdateInstall::installChecks()
{
    QTemporaryDir dir;
    const QString app = dir.filePath(QStringLiteral("MyBooksLibrary"));
    const Version running{0, 4, 0};
    QDir().mkpath(app);

    // Built from source: no marker.
    InstallCheck check = checkInstall(app, running, {});
    QVERIFY(!check.canInstall);
    QVERIFY(check.reason.contains(QLatin1String("not installed from a release")));

    // A marker for another version.
    write(QDir(app).filePath(QStringLiteral("release.json")), mbl::update::releaseMarker(Version{0, 3, 0}));
    check = checkInstall(app, running, {});
    QVERIFY(!check.canInstall);
    QVERIFY(check.reason.contains(QLatin1String("0.3.0")));

    // A release copy whose kept folders are elsewhere: yes.
    write(QDir(app).filePath(QStringLiteral("release.json")), mbl::update::releaseMarker(running));
    const QString library = dir.filePath(QStringLiteral("Library"));
    QDir().mkpath(library);
    check = checkInstall(app, running, {library, dir.filePath(QStringLiteral("data/updates"))});
    QVERIFY2(check.canInstall, qPrintable(check.reason));

    // The library inside the app's folder: no, it would be swapped away.
    const QString inside = QDir(app).filePath(QStringLiteral("Library"));
    QDir().mkpath(inside);
    check = checkInstall(app, running, {inside});
    QVERIFY(!check.canInstall);
    QVERIFY(check.reason.contains(QDir::toNativeSeparators(inside)));
    // Also when it does not exist yet (the staging folder before a first update).
    check = checkInstall(app, running, {QDir(app).filePath(QStringLiteral("data/updates"))});
    QVERIFY(!check.canInstall);
}

void TestUpdateInstall::replacesTheAppAndKeepsThePrevious()
{
    QTemporaryDir dir;
    const QString install = dir.filePath(QStringLiteral("Apps/MyBooksLibrary"));
    const QString staged = dir.filePath(QStringLiteral("data/updates/0.4.0/unpacked/MyBooksLibrary"));
    makeApp(install, Version{0, 3, 0}, "old");
    makeApp(staged, Version{0, 4, 0}, "new");
    write(install + QStringLiteral(".previous/appMyBooksLibrary.exe"), "older");  // Left by the update before.
    QStringList log;

    const ApplyResult result = applyUpdate(install, staged, [&log](const QString& line) { log << line; });
    QVERIFY2(result.ok, qPrintable(result.error + u'\n' + log.join(u'\n')));
    QCOMPARE(readReleaseMarker(install), (Version{0, 4, 0}));
    QCOMPARE(read(QDir(install).filePath(QStringLiteral("appMyBooksLibrary.exe"))), QByteArray("exe new"));
    QCOMPARE(read(QDir(install).filePath(QStringLiteral("models/det.onnx"))), QByteArray("model new"));
    QCOMPARE(read(QDir(install).filePath(QStringLiteral("qml/QtQuick/qmldir"))), QByteArray("qmldir new"));
    // The version replaced is kept for rolling back; the older one is gone.
    QCOMPARE(readReleaseMarker(install + QStringLiteral(".previous")), (Version{0, 3, 0}));
    QCOMPARE(read(install + QStringLiteral(".previous/appMyBooksLibrary.exe")), QByteArray("exe old"));
    // The staged copy is left for the new version to tidy.
    QVERIFY(QFileInfo::exists(QDir(staged).filePath(QStringLiteral("appMyBooksLibrary.exe"))));
    QVERIFY(log.size() >= 3);
}

void TestUpdateInstall::refusesFoldersThatAreNotReleases()
{
    QTemporaryDir dir;
    const QString install = dir.filePath(QStringLiteral("MyBooksLibrary"));
    const QString staged = dir.filePath(QStringLiteral("staged/MyBooksLibrary"));
    const auto noLog = [](const QString&) {};

    // Wrong arguments must never swap an arbitrary folder.
    QDir().mkpath(install);
    write(QDir(install).filePath(QStringLiteral("notes.txt")), "user's file");
    makeApp(staged, Version{0, 4, 0}, "new");
    QVERIFY(!applyUpdate(install, staged, noLog).ok);
    QCOMPARE(read(QDir(install).filePath(QStringLiteral("notes.txt"))), QByteArray("user's file"));
    QVERIFY(!QFileInfo::exists(install + QStringLiteral(".previous")));

    // A staged folder that is not a release.
    QDir(install).removeRecursively();
    makeApp(install, Version{0, 3, 0}, "old");
    QFile::remove(QDir(staged).filePath(QStringLiteral("release.json")));
    QVERIFY(!applyUpdate(install, staged, noLog).ok);
    QCOMPARE(readReleaseMarker(install), (Version{0, 3, 0}));

    // The new copy inside the app's folder.
    const QString nested = QDir(install).filePath(QStringLiteral("updates/MyBooksLibrary"));
    makeApp(nested, Version{0, 4, 0}, "new");
    QVERIFY(!applyUpdate(install, nested, noLog).ok);
    QCOMPARE(readReleaseMarker(install), (Version{0, 3, 0}));
}

void TestUpdateInstall::fileInUseChangesNothing()
{
#ifndef Q_OS_WIN
    QSKIP("Windows file locks");
#else
    QTemporaryDir dir;
    const QString install = dir.filePath(QStringLiteral("MyBooksLibrary"));
    const QString staged = dir.filePath(QStringLiteral("staged/MyBooksLibrary"));
    makeApp(install, Version{0, 3, 0}, "old");
    makeApp(staged, Version{0, 4, 0}, "new");
    // Another MyBooksLibrary window still running from the folder.
    HANDLE lock = lockFile(QDir(install).filePath(QStringLiteral("models/det.onnx")));
    QVERIFY(lock != INVALID_HANDLE_VALUE);
    const ApplyResult result = applyUpdate(install, staged, [](const QString&) {});
    CloseHandle(lock);
    QVERIFY(!result.ok);
    QVERIFY(result.restored);
    QVERIFY2(result.error.contains(QLatin1String("close every MyBooksLibrary window")), qPrintable(result.error));
    QCOMPARE(readReleaseMarker(install), (Version{0, 3, 0}));
    QCOMPARE(read(QDir(install).filePath(QStringLiteral("appMyBooksLibrary.exe"))), QByteArray("exe old"));
    QVERIFY(!QFileInfo::exists(install + QStringLiteral(".previous")));
#endif
}

void TestUpdateInstall::failedCopyPutsThePreviousBack()
{
#ifndef Q_OS_WIN
    QSKIP("Windows file locks");
#else
    QTemporaryDir dir;
    const QString install = dir.filePath(QStringLiteral("MyBooksLibrary"));
    const QString staged = dir.filePath(QStringLiteral("staged/MyBooksLibrary"));
    makeApp(install, Version{0, 3, 0}, "old");
    makeApp(staged, Version{0, 4, 0}, "new");
    // A staged file that cannot be read: the copy fails part-way.
    HANDLE lock = lockFile(QDir(staged).filePath(QStringLiteral("models/det.onnx")));
    QVERIFY(lock != INVALID_HANDLE_VALUE);
    QStringList log;
    const ApplyResult result = applyUpdate(install, staged, [&log](const QString& line) { log << line; });
    CloseHandle(lock);
    QVERIFY(!result.ok);
    QVERIFY2(result.restored, qPrintable(log.join(u'\n')));
    QCOMPARE(readReleaseMarker(install), (Version{0, 3, 0}));
    QCOMPARE(read(QDir(install).filePath(QStringLiteral("models/det.onnx"))), QByteArray("model old"));
    QVERIFY(!QFileInfo::exists(install + QStringLiteral(".previous")));
    QVERIFY(log.join(u'\n').contains(QLatin1String("restoring the previous version")));
#endif
}

QTEST_GUILESS_MAIN(TestUpdateInstall)
#include "tst_updateinstall.moc"
