#include "update/releases.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

namespace mbl::update {

std::optional<Version> Version::parse(const QString& text)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^v?(\d{1,5})\.(\d{1,5})\.(\d{1,5})$)"));
    const QRegularExpressionMatch m = pattern.match(text);
    if (!m.hasMatch())
        return std::nullopt;
    return Version{m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()};
}

QString Version::toString() const
{
    return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
}

QString zipAssetName(const Version& version)
{
    return QStringLiteral("MyBooksLibrary-%1-win64.zip").arg(version.toString());
}

bool isReleaseDownloadUrl(const QUrl& url, const QString& repository, const QString& tag, const QString& name)
{
    if (!url.isValid() || url.scheme() != QLatin1String("https") || url.host() != QLatin1String("github.com")
        || url.port() != -1 || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment())
        return false;
    const QString expected = QStringLiteral("/%1/releases/download/%2/%3").arg(repository, tag, name);
    return url.path(QUrl::FullyDecoded) == expected;
}

QList<Release> parseReleases(const QByteArray& json, const QString& repository, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error)
            *error = QStringLiteral("GitHub's answer is not a list of releases.");
        return {};
    }
    QList<Release> releases;
    for (const QJsonValue& value : document.array()) {
        const QJsonObject object = value.toObject();
        if (object.value(QLatin1String("draft")).toBool(true) || object.value(QLatin1String("prerelease")).toBool(true))
            continue;
        const QString tag = object.value(QLatin1String("tag_name")).toString();
        const std::optional<Version> version = Version::parse(tag);
        if (!version || !tag.startsWith(u'v'))
            continue;

        Release release;
        release.version = *version;
        release.tag = tag;
        release.name = object.value(QLatin1String("name")).toString();
        release.notes = object.value(QLatin1String("body")).toString();
        release.published = QDateTime::fromString(object.value(QLatin1String("published_at")).toString(), Qt::ISODate);
        const QUrl page(object.value(QLatin1String("html_url")).toString());
        if (page.scheme() == QLatin1String("https") && page.host() == QLatin1String("github.com"))
            release.page = page;

        const QString zipName = zipAssetName(*version);
        const QString shaName = zipName + QStringLiteral(".sha256");
        for (const QJsonValue& assetValue : object.value(QLatin1String("assets")).toArray()) {
            const QJsonObject asset = assetValue.toObject();
            const QString name = asset.value(QLatin1String("name")).toString();
            const QUrl url(asset.value(QLatin1String("browser_download_url")).toString());
            if (name == zipName && isReleaseDownloadUrl(url, repository, tag, zipName)) {
                release.zip = url;
                release.zipSize = asset.value(QLatin1String("size")).toInteger();
            } else if (name == shaName && isReleaseDownloadUrl(url, repository, tag, shaName)) {
                release.sha256 = url;
            }
        }
        if (release.zip.isEmpty() || release.sha256.isEmpty())
            continue;
        releases.append(release);
    }
    std::sort(releases.begin(), releases.end(), [](const Release& a, const Release& b) { return a.version > b.version; });
    if (error)
        error->clear();
    return releases;
}

QList<Release> newerReleases(const QList<Release>& releases, const Version& current)
{
    QList<Release> newer;
    for (const Release& release : releases) {
        if (release.version > current)
            newer.append(release);
    }
    std::sort(newer.begin(), newer.end(), [](const Release& a, const Release& b) { return a.version > b.version; });
    return newer;
}

std::optional<QByteArray> parseSha256File(const QByteArray& content, const QString& fileName)
{
    static const QRegularExpression line(QStringLiteral(R"(^([0-9a-fA-F]{64})(?:\s+\*?(\S.*?))?\s*$)"));
    const QStringList lines = QString::fromUtf8(content).split(u'\n');
    for (const QString& text : lines) {
        const QRegularExpressionMatch m = line.match(text.trimmed());
        if (!m.hasMatch())
            continue;
        const QString name = m.captured(2);
        if (name.isEmpty() || name == fileName)
            return m.captured(1).toLower().toLatin1();
    }
    return std::nullopt;
}

} // namespace mbl::update
