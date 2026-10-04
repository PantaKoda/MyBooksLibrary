// Updates: the download of a release (src/update/updatedownloader.h), from
// local files (file:// URLs, allowed in tests only) with real zips made by
// Windows' tar.exe: a good package is unpacked and checked; a checksum
// mismatch, a package of another version and a non-GitHub address are
// refused, and nothing is left behind.
//
// MBL_ONLINE_TESTS=1 also runs one check against github.com itself: the real
// release list, and the download of the published 0.3.0 package (about
// 150 MB) through GitHub's redirects, its checksum and unpacking. 0.3.0
// predates the release marker, so the last check must refuse it.
#include "update/installinfo.h"
#include "update/releases.h"
#include "update/updatedownloader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>

using namespace mbl::update;

namespace {

QString tar()
{
    return QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows"))).filePath(QStringLiteral("System32/tar.exe"));
}

void write(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

} // namespace

class TestUpdateDownloader : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void downloadsVerifiesAndUnpacks();
    void checksumMismatchInstallsNothing();
    void packageOfAnotherVersionIsRefused();
    void onlyGitHubAddresses();
    void cancelRemovesTheFolder();
    void onlineGitHubRelease();

private:
    // A release of `marker` packed as release `version`, with its checksum
    // (or `sha` instead, if given).
    Release makeRelease(const Version& version, const Version& marker, const QByteArray& sha = {});

    QTemporaryDir m_dir;
    QNetworkAccessManager m_network;
};

void TestUpdateDownloader::initTestCase()
{
    if (!QFileInfo(tar()).isFile())
        QSKIP("Windows' tar.exe is needed");
}

Release TestUpdateDownloader::makeRelease(const Version& version, const Version& marker, const QByteArray& sha)
{
    const QString source = m_dir.filePath(QStringLiteral("src-%1-%2").arg(version.toString(), marker.toString()));
    const QString app = QDir(source).filePath(QStringLiteral("MyBooksLibrary"));
    write(QDir(app).filePath(QStringLiteral("appMyBooksLibrary.exe")), "exe");
    write(QDir(app).filePath(QStringLiteral("release.json")), releaseMarker(marker));
    write(QDir(app).filePath(QStringLiteral("models/det.onnx")), QByteArray(200000, 'm'));
    const QString published = m_dir.filePath(QStringLiteral("published-%1-%2").arg(version.toString(), marker.toString()));
    QDir().mkpath(published);
    const QString zip = QDir(published).filePath(zipAssetName(version));
    QProcess pack;
    pack.start(tar(), {QStringLiteral("-a"), QStringLiteral("-c"), QStringLiteral("-f"), QDir::toNativeSeparators(zip),
                       QStringLiteral("-C"), QDir::toNativeSeparators(source), QStringLiteral("MyBooksLibrary")});
    if (!pack.waitForFinished(60000) || pack.exitCode() != 0)
        qFatal("tar could not make the zip: %s", pack.readAll().constData());
    QFile file(zip);
    file.open(QIODevice::ReadOnly);
    const QByteArray hash = sha.isEmpty() ? QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex() : sha;
    write(zip + QStringLiteral(".sha256"), hash + "  " + zipAssetName(version).toUtf8() + "\n");

    Release release;
    release.version = version;
    release.tag = QStringLiteral("v") + version.toString();
    release.zip = QUrl::fromLocalFile(zip);
    release.sha256 = QUrl::fromLocalFile(zip + QStringLiteral(".sha256"));
    return release;
}

void TestUpdateDownloader::downloadsVerifiesAndUnpacks()
{
    const Release release = makeRelease(Version{0, 4, 0}, Version{0, 4, 0});
    const QString staging = m_dir.filePath(QStringLiteral("updates-ok"));
    UpdateDownloader downloader(&m_network);
    downloader.setAllowLocalUrls(true);
    QSignalSpy finished(&downloader, &UpdateDownloader::finished);
    QSignalSpy failed(&downloader, &UpdateDownloader::failed);
    QSignalSpy stages(&downloader, &UpdateDownloader::stageChanged);
    downloader.start(release, staging);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty() || !failed.isEmpty(), 60000);
    QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
    const QString app = finished.first().first().toString();
    QCOMPARE(QDir::cleanPath(app), QDir::cleanPath(staging + QStringLiteral("/0.4.0/unpacked/MyBooksLibrary")));
    QCOMPARE(readReleaseMarker(app), (Version{0, 4, 0}));
    QCOMPARE(QFileInfo(QDir(app).filePath(QStringLiteral("models/det.onnx"))).size(), 200000);
    QStringList seen;
    for (const QList<QVariant>& stage : stages)
        seen << stage.first().toString();
    QCOMPARE(seen, (QStringList{QStringLiteral("checksum"), QStringLiteral("download"), QStringLiteral("verify"),
                                QStringLiteral("unpack")}));
    QVERIFY(!downloader.running());
}

void TestUpdateDownloader::checksumMismatchInstallsNothing()
{
    const Release release = makeRelease(Version{0, 4, 1}, Version{0, 4, 1}, QByteArray(64, 'a'));
    const QString staging = m_dir.filePath(QStringLiteral("updates-bad-sha"));
    UpdateDownloader downloader(&m_network);
    downloader.setAllowLocalUrls(true);
    QSignalSpy finished(&downloader, &UpdateDownloader::finished);
    QSignalSpy failed(&downloader, &UpdateDownloader::failed);
    downloader.start(release, staging);
    QTRY_VERIFY_WITH_TIMEOUT(!failed.isEmpty(), 60000);
    QVERIFY(finished.isEmpty());
    QVERIFY(failed.first().at(0).toString().contains(QLatin1String("checksum")));
    QVERIFY(!failed.first().at(1).toBool());
    QVERIFY(!QFileInfo::exists(QDir(staging).filePath(QStringLiteral("0.4.1"))));
}

void TestUpdateDownloader::packageOfAnotherVersionIsRefused()
{
    // Published as 0.4.2, but the package inside says 0.3.0.
    const Release release = makeRelease(Version{0, 4, 2}, Version{0, 3, 0});
    const QString staging = m_dir.filePath(QStringLiteral("updates-other"));
    UpdateDownloader downloader(&m_network);
    downloader.setAllowLocalUrls(true);
    QSignalSpy failed(&downloader, &UpdateDownloader::failed);
    downloader.start(release, staging);
    QTRY_VERIFY_WITH_TIMEOUT(!failed.isEmpty(), 60000);
    QVERIFY2(failed.first().at(0).toString().contains(QLatin1String("does not hold MyBooksLibrary 0.4.2")),
             qPrintable(failed.first().at(0).toString()));
    QVERIFY(!QFileInfo::exists(QDir(staging).filePath(QStringLiteral("0.4.2"))));
}

void TestUpdateDownloader::onlyGitHubAddresses()
{
    Release release = makeRelease(Version{0, 4, 3}, Version{0, 4, 3});
    UpdateDownloader downloader(&m_network);  // Local files not allowed: as in the app.
    QSignalSpy failed(&downloader, &UpdateDownloader::failed);
    downloader.start(release, m_dir.filePath(QStringLiteral("updates-local")));
    QCOMPARE(failed.size(), 1);
    QVERIFY(!downloader.running());
    release.zip = QUrl(QStringLiteral("https://evil.example/MyBooksLibrary-0.4.3-win64.zip"));
    release.sha256 = QUrl(QStringLiteral("https://github.com/x.sha256"));
    downloader.start(release, m_dir.filePath(QStringLiteral("updates-local")));
    QCOMPARE(failed.size(), 2);
    QVERIFY(!QFileInfo::exists(m_dir.filePath(QStringLiteral("updates-local/0.4.3"))));
}

void TestUpdateDownloader::cancelRemovesTheFolder()
{
    const Release release = makeRelease(Version{0, 4, 4}, Version{0, 4, 4});
    const QString staging = m_dir.filePath(QStringLiteral("updates-cancel"));
    UpdateDownloader downloader(&m_network);
    downloader.setAllowLocalUrls(true);
    QSignalSpy failed(&downloader, &UpdateDownloader::failed);
    QSignalSpy finished(&downloader, &UpdateDownloader::finished);
    downloader.start(release, staging);
    QVERIFY(downloader.running());
    downloader.cancel();
    QCOMPARE(failed.size(), 1);
    QVERIFY(failed.first().at(1).toBool());  // Cancelled.
    QVERIFY(!downloader.running());
    QVERIFY(!QFileInfo::exists(QDir(staging).filePath(QStringLiteral("0.4.4"))));
    QTest::qWait(300);  // Nothing arrives late.
    QVERIFY(finished.isEmpty());
    QCOMPARE(failed.size(), 1);
}

void TestUpdateDownloader::onlineGitHubRelease()
{
    if (qEnvironmentVariable("MBL_ONLINE_TESTS") != QLatin1String("1"))
        QSKIP("set MBL_ONLINE_TESTS=1 to download from github.com");
    const QString repo = QStringLiteral("PantaKoda/MyBooksLibrary");
    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases?per_page=30").arg(repo)));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("MyBooksLibrary-tests"));
    std::unique_ptr<QNetworkReply> reply(m_network.get(request));
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 30000);
    QCOMPARE(reply->error(), QNetworkReply::NoError);
    const QList<Release> releases = parseReleases(reply->readAll(), repo);
    const auto it = std::find_if(releases.cbegin(), releases.cend(),
                                 [](const Release& r) { return r.version == Version{0, 3, 0}; });
    QVERIFY2(it != releases.cend(), "v0.3.0 is not among the usable releases");
    qInfo("releases: %lld usable, newest %s", static_cast<long long>(releases.size()),
          qPrintable(releases.first().version.toString()));

    UpdateDownloader downloader(&m_network);
    QSignalSpy failed(&downloader, &UpdateDownloader::failed);
    QSignalSpy finished(&downloader, &UpdateDownloader::finished);
    QSignalSpy stages(&downloader, &UpdateDownloader::stageChanged);
    const QString staging = m_dir.filePath(QStringLiteral("updates-online"));
    downloader.start(*it, staging);
    QTRY_VERIFY_WITH_TIMEOUT(!failed.isEmpty() || !finished.isEmpty(), 600000);
    QVERIFY(finished.isEmpty());
    QStringList seen;
    for (const QList<QVariant>& stage : stages)
        seen << stage.first().toString();
    // Downloaded, the checksum matched, unpacked, and refused for the missing marker.
    QCOMPARE(seen.last(), QStringLiteral("unpack"));
    QVERIFY2(failed.first().at(0).toString().contains(QLatin1String("does not hold MyBooksLibrary 0.3.0")),
             qPrintable(failed.first().at(0).toString()));
    QVERIFY(!QFileInfo::exists(QDir(staging).filePath(QStringLiteral("0.3.0"))));
}

QTEST_GUILESS_MAIN(TestUpdateDownloader)
#include "tst_updatedownloader.moc"
