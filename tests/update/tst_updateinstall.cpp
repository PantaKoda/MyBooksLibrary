// Updates: may this copy replace itself (checkInstall), and putting a new
// copy in place of the app's folder (applyUpdate), on real folders: the
// previous version kept as <folder>.previous, an older .previous replaced
// only once the new copy is in place, and, when a file is in use, nothing
// changed or everything put back. A library is never moved or deleted: not
// inside the app's folder (open or not), not inside .previous.
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
    void librariesAreNeverMovedOrDeleted();
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
    makeApp(install + QStringLiteral(".previous"), Version{0, 2, 0}, "older");  // Left by the update before.
    QStringList log;

    const ApplyResult result = applyUpdate(install, staged, [&log](const QString& line) { log << line; });
    QVERIFY2(result.ok, qPrintable(result.error + u'\n' + log.join(u'\n')));
    QCOMPARE(readReleaseMarker(install), (Version{0, 4, 0}));
    QCOMPARE(read(QDir(install).filePath(QStringLiteral("appMyBooksLibrary.exe"))), QByteArray("exe new"));
    QCOMPARE(read(QDir(install).filePath(QStringLiteral("models/det.onnx"))), QByteArray("model new"));
    QCOMPARE(read(QDir(install).filePath(QStringLiteral("qml/QtQuick/qmldir"))), QByteArray("qmldir new"));
    // The version replaced is kept for rolling back; the older one is gone,
    // with no set-aside folder left behind.
    QCOMPARE(readReleaseMarker(install + QStringLiteral(".previous")), (Version{0, 3, 0}));
    QCOMPARE(read(install + QStringLiteral(".previous/appMyBooksLibrary.exe")), QByteArray("exe old"));
    QCOMPARE(QDir(dir.filePath(QStringLiteral("Apps"))).entryList(QDir::Dirs | QDir::NoDotAndDotDot),
             (QStringList{QStringLiteral("MyBooksLibrary"), QStringLiteral("MyBooksLibrary.previous")}));
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
    makeApp(install + QStringLiteral(".previous"), Version{0, 2, 0}, "older");
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
    // Nothing changed: the older version kept for rolling back is still there.
    QCOMPARE(readReleaseMarker(install + QStringLiteral(".previous")), (Version{0, 2, 0}));
    QCOMPARE(QDir(dir.path()).entryList({QStringLiteral("MyBooksLibrary.previous-*")}, QDir::Dirs), QStringList());
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
    makeApp(install + QStringLiteral(".previous"), Version{0, 2, 0}, "older");
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
    // The older version kept for rolling back is back in its place.
    QCOMPARE(readReleaseMarker(install + QStringLiteral(".previous")), (Version{0, 2, 0}));
    QCOMPARE(QDir(dir.path()).entryList({QStringLiteral("MyBooksLibrary.previous-*")}, QDir::Dirs), QStringList());
    QVERIFY(log.join(u'\n').contains(QLatin1String("restoring the previous version")));
#endif
}

// The review of PR #43: a library A kept inside the app's folder, while
// library B elsewhere is the one open. One update would have moved A into
// .previous and the next deleted it.
void TestUpdateInstall::librariesAreNeverMovedOrDeleted()
{
    QTemporaryDir dir;
    const QString install = dir.filePath(QStringLiteral("MyBooksLibrary"));
    const QString staged = dir.filePath(QStringLiteral("staged/MyBooksLibrary"));
    const QString openLibrary = dir.filePath(QStringLiteral("B"));
    const Version running{0, 3, 0};
    makeApp(install, running, "old");
    makeApp(staged, Version{0, 4, 0}, "new");
    QDir().mkpath(openLibrary);
    const auto noLog = [](const QString&) {};

    // Library A inside the app's folder, not open: refused before any download.
    const QString libraryA = QDir(install).filePath(QStringLiteral("Books"));
    write(QDir(libraryA).filePath(QStringLiteral("library.sqlite")), "catalog A");
    write(QDir(libraryA).filePath(QStringLiteral("files/1/source.pdf")), "book");
    QVERIFY(containsLibrary(install));
    InstallCheck check = checkInstall(install, running, {openLibrary});
    QVERIFY(!check.canInstall);
    QVERIFY2(check.reason.contains(QLatin1String("A library is inside the app's folder")), qPrintable(check.reason));
    // And the hand-over itself refuses it: nothing moves.
    QVERIFY(!applyUpdate(install, staged, noLog).ok);
    QCOMPARE(read(QDir(libraryA).filePath(QStringLiteral("library.sqlite"))), QByteArray("catalog A"));
    QVERIFY(!QFileInfo::exists(install + QStringLiteral(".previous")));

    // A .previous that holds a library (moved there by an earlier version of
    // this code, or by hand): never deleted, and the update is refused.
    QDir(libraryA).removeRecursively();
    const QString previous = install + QStringLiteral(".previous");
    makeApp(previous, Version{0, 2, 0}, "older");
    write(QDir(previous).filePath(QStringLiteral("Books/library.sqlite")), "catalog A");
    QVERIFY(!previousMayBeReplaced(previous));
    check = checkInstall(install, running, {openLibrary});
    QVERIFY(!check.canInstall);
    QVERIFY(check.reason.contains(QDir::toNativeSeparators(previous)));
    const ApplyResult result = applyUpdate(install, staged, noLog);
    QVERIFY(!result.ok);
    QCOMPARE(read(QDir(previous).filePath(QStringLiteral("Books/library.sqlite"))), QByteArray("catalog A"));
    QCOMPARE(readReleaseMarker(install), running);

    // A .previous that is not a release at all (the user's own folder of that name).
    QDir(previous).removeRecursively();
    write(QDir(previous).filePath(QStringLiteral("notes.txt")), "mine");
    QVERIFY(!previousMayBeReplaced(previous));
    QVERIFY(!checkInstall(install, running, {openLibrary}).canInstall);
    QVERIFY(!applyUpdate(install, staged, noLog).ok);
    QCOMPARE(read(QDir(previous).filePath(QStringLiteral("notes.txt"))), QByteArray("mine"));

    // With those gone, the same update goes through.
    QDir(previous).removeRecursively();
    QVERIFY2(checkInstall(install, running, {openLibrary}).canInstall,
             qPrintable(checkInstall(install, running, {openLibrary}).reason));
    QVERIFY(applyUpdate(install, staged, noLog).ok);
}

QTEST_GUILESS_MAIN(TestUpdateInstall)
#include "tst_updateinstall.moc"
