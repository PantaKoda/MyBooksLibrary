// Updates: which GitHub releases an update may use (src/update/releases.h).
// Versions, the release list (drafts, pre-releases, other tags, assets at
// other addresses are ignored), the .sha256 format and the trusted download
// hosts.
#include "update/releases.h"
#include "update/updatedownloader.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace mbl::update;

namespace {

const QString repo = QStringLiteral("PantaKoda/MyBooksLibrary");

QJsonObject asset(const QString& tag, const QString& name, const QString& url = {})
{
    return QJsonObject{
        {QStringLiteral("name"), name},
        {QStringLiteral("size"), 1234},
        {QStringLiteral("browser_download_url"),
         url.isEmpty() ? QStringLiteral("https://github.com/%1/releases/download/%2/%3").arg(repo, tag, name) : url},
    };
}

QJsonObject release(const QString& tag, bool draft = false, bool prerelease = false, QJsonArray assets = {})
{
    const QString version = tag.mid(1);
    if (assets.isEmpty()) {
        const QString zip = QStringLiteral("MyBooksLibrary-%1-win64.zip").arg(version);
        assets = {asset(tag, zip), asset(tag, zip + QStringLiteral(".sha256"))};
    }
    return QJsonObject{
        {QStringLiteral("tag_name"), tag},
        {QStringLiteral("name"), QStringLiteral("MyBooksLibrary %1").arg(version)},
        {QStringLiteral("body"), QStringLiteral("Notes for %1 <b>not markup</b>").arg(version)},
        {QStringLiteral("draft"), draft},
        {QStringLiteral("prerelease"), prerelease},
        {QStringLiteral("published_at"), QStringLiteral("2026-10-03T12:03:47Z")},
        {QStringLiteral("html_url"), QStringLiteral("https://github.com/%1/releases/tag/%2").arg(repo, tag)},
        {QStringLiteral("assets"), assets},
    };
}

QByteArray json(const QJsonArray& releases)
{
    return QJsonDocument(releases).toJson();
}

} // namespace

class TestReleases : public QObject {
    Q_OBJECT

private slots:
    void versions();
    void usableReleasesOnly();
    void assetsMustBeTheRepositorysOwn();
    void newerReleasesNewestFirst();
    void notAListIsAnError();
    void checksumFiles();
    void trustedDownloadHosts();
};

void TestReleases::versions()
{
    QCOMPARE(Version::parse(QStringLiteral("v0.3.0")), (Version{0, 3, 0}));
    QCOMPARE(Version::parse(QStringLiteral("1.10.2")), (Version{1, 10, 2}));
    for (const char* bad : {"", "v1.2", "1.2.3.4", "v1.2.3-rc1", "1.2.x", " 1.2.3", "w1.2.3"})
        QVERIFY2(!Version::parse(QString::fromLatin1(bad)), bad);
    QVERIFY((Version{0, 10, 0}) > (Version{0, 9, 9}));
    QVERIFY((Version{1, 0, 0}) > (Version{0, 99, 99}));
    QVERIFY((Version{0, 3, 1}) > (Version{0, 3, 0}));
    QVERIFY(!((Version{0, 3, 0}) > (Version{0, 3, 0})));
    QCOMPARE((Version{0, 4, 0}).toString(), QStringLiteral("0.4.0"));
    QCOMPARE(zipAssetName(Version{0, 4, 0}), QStringLiteral("MyBooksLibrary-0.4.0-win64.zip"));
}

void TestReleases::usableReleasesOnly()
{
    const QJsonArray list{
        release(QStringLiteral("v0.2.0")),
        release(QStringLiteral("v0.5.0"), true),           // Draft.
        release(QStringLiteral("v0.4.0"), false, true),    // Pre-release.
        release(QStringLiteral("v0.3.0")),
        release(QStringLiteral("0.6.0")),                  // Not a vX.Y.Z tag.
        release(QStringLiteral("v0.7.0-beta")),
        release(QStringLiteral("v0.8.0"), false, false,    // No checksum.
                {asset(QStringLiteral("v0.8.0"), QStringLiteral("MyBooksLibrary-0.8.0-win64.zip"))}),
    };
    QString error = QStringLiteral("stale");
    const QList<Release> releases = parseReleases(json(list), repo, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(releases.size(), 2);
    QCOMPARE(releases.at(0).version, (Version{0, 3, 0}));  // Newest first.
    QCOMPARE(releases.at(1).version, (Version{0, 2, 0}));
    const Release& r = releases.at(0);
    QCOMPARE(r.tag, QStringLiteral("v0.3.0"));
    QCOMPARE(r.name, QStringLiteral("MyBooksLibrary 0.3.0"));
    QCOMPARE(r.notes, QStringLiteral("Notes for 0.3.0 <b>not markup</b>"));
    QCOMPARE(r.zip.toString(),
             QStringLiteral("https://github.com/PantaKoda/MyBooksLibrary/releases/download/v0.3.0/MyBooksLibrary-0.3.0-win64.zip"));
    QCOMPARE(r.sha256.toString(), r.zip.toString() + QStringLiteral(".sha256"));
    QCOMPARE(r.zipSize, 1234);
    QVERIFY(r.published.isValid());
    QCOMPARE(r.page.host(), QStringLiteral("github.com"));
}

void TestReleases::assetsMustBeTheRepositorysOwn()
{
    const QString tag = QStringLiteral("v0.4.0");
    const QString zip = QStringLiteral("MyBooksLibrary-0.4.0-win64.zip");
    const QString sha = zip + QStringLiteral(".sha256");
    const QStringList badZipUrls{
        QStringLiteral("http://github.com/%1/releases/download/%2/%3").arg(repo, tag, zip),         // Not HTTPS.
        QStringLiteral("https://evil.example/%1/releases/download/%2/%3").arg(repo, tag, zip),      // Other host.
        QStringLiteral("https://github.com/Someone/Fork/releases/download/%1/%2").arg(tag, zip),    // Other repository.
        QStringLiteral("https://github.com/%1/releases/download/v0.3.0/%2").arg(repo, zip),         // Other tag.
        QStringLiteral("https://github.com/%1/releases/download/%2/%3?x=1").arg(repo, tag, zip),    // A query.
        QStringLiteral("https://github.com/%1/releases/download/%2/../%3").arg(repo, tag, zip),     // Dot segments.
        QStringLiteral("https://user@github.com/%1/releases/download/%2/%3").arg(repo, tag, zip),   // User info.
        QStringLiteral("https://github.com:8443/%1/releases/download/%2/%3").arg(repo, tag, zip),   // A port.
    };
    for (const QString& url : badZipUrls) {
        const QJsonArray list{release(tag, false, false, {asset(tag, zip, url), asset(tag, sha)})};
        QVERIFY2(parseReleases(json(list), repo).isEmpty(), qPrintable(url));
    }
    // An asset of another name is not the package.
    const QJsonArray other{release(tag, false, false,
                                   {asset(tag, QStringLiteral("MyBooksLibrary-0.4.0-win64.exe")), asset(tag, sha)})};
    QVERIFY(parseReleases(json(other), repo).isEmpty());
}

void TestReleases::newerReleasesNewestFirst()
{
    const QList<Release> all = parseReleases(
        json({release(QStringLiteral("v0.1.0")), release(QStringLiteral("v0.4.0")), release(QStringLiteral("v0.3.0")),
              release(QStringLiteral("v0.10.0"))}),
        repo);
    const QList<Release> newer = newerReleases(all, Version{0, 3, 0});
    QCOMPARE(newer.size(), 2);
    QCOMPARE(newer.at(0).version, (Version{0, 10, 0}));
    QCOMPARE(newer.at(1).version, (Version{0, 4, 0}));
    QVERIFY(newerReleases(all, Version{0, 10, 0}).isEmpty());
    QVERIFY(newerReleases(all, Version{1, 0, 0}).isEmpty());  // A build newer than any release.
}

void TestReleases::notAListIsAnError()
{
    for (const char* answer : {"", "{\"message\": \"API rate limit exceeded\"}", "not json", "[1, 2"}) {
        QString error;
        QVERIFY(parseReleases(QByteArray(answer), repo, &error).isEmpty());
        QVERIFY2(!error.isEmpty(), answer);
    }
    QString error = QStringLiteral("stale");
    QVERIFY(parseReleases("[]", repo, &error).isEmpty());
    QVERIFY(error.isEmpty());
}

void TestReleases::checksumFiles()
{
    const QByteArray hash = "26f29980aa3b1c0d195fe0123456789abcdef0123456789abcdef01234567890";
    QCOMPARE(hash.size(), 64);
    const QString name = QStringLiteral("MyBooksLibrary-0.4.0-win64.zip");
    QCOMPARE(parseSha256File(hash + "  MyBooksLibrary-0.4.0-win64.zip\r\n", name), std::optional<QByteArray>(hash));
    QCOMPARE(parseSha256File(hash.toUpper() + " *MyBooksLibrary-0.4.0-win64.zip", name), std::optional<QByteArray>(hash));
    QCOMPARE(parseSha256File(hash + "\n", name), std::optional<QByteArray>(hash));  // No name.
    QVERIFY(!parseSha256File(hash + "  Other-0.4.0.zip", name));
    QVERIFY(!parseSha256File("abc  MyBooksLibrary-0.4.0-win64.zip", name));
    QVERIFY(!parseSha256File("", name));
}

void TestReleases::trustedDownloadHosts()
{
    QVERIFY(UpdateDownloader::isTrustedUrl(QUrl(QStringLiteral("https://github.com/a/b/releases/download/v1.0.0/x.zip"))));
    QVERIFY(UpdateDownloader::isTrustedUrl(QUrl(QStringLiteral("https://release-assets.githubusercontent.com/x"))));
    QVERIFY(UpdateDownloader::isTrustedUrl(QUrl(QStringLiteral("https://objects.githubusercontent.com/x"))));
    QVERIFY(!UpdateDownloader::isTrustedUrl(QUrl(QStringLiteral("http://github.com/x"))));
    QVERIFY(!UpdateDownloader::isTrustedUrl(QUrl(QStringLiteral("https://githubusercontent.com.evil.example/x"))));
    QVERIFY(!UpdateDownloader::isTrustedUrl(QUrl(QStringLiteral("https://evilgithub.com/x"))));
    QVERIFY(!UpdateDownloader::isTrustedUrl(QUrl(QStringLiteral("file:///C:/x.zip"))));
}

QTEST_GUILESS_MAIN(TestReleases)
#include "tst_releases.moc"
