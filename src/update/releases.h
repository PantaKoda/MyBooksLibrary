// Updates: the app's releases on GitHub, as the update check sees them.
//
// The check reads https://api.github.com/repos/<repository>/releases
// anonymously and keeps only what an update can use: published releases (no
// drafts, no pre-releases) with a vX.Y.Z tag and both of the release
// workflow's assets, MyBooksLibrary-X.Y.Z-win64.zip and its .sha256, at the
// repository's own release download URLs on github.com over HTTPS. Anything
// else is ignored, never guessed at. Release notes are untrusted text: the
// views show them as plain text.
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QUrl>

#include <optional>

namespace mbl::update {

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;

    // "1.2.3" or "v1.2.3"; nothing else (no suffixes, no missing parts).
    static std::optional<Version> parse(const QString& text);
    QString toString() const;

    friend bool operator==(const Version& a, const Version& b)
    {
        return a.major == b.major && a.minor == b.minor && a.patch == b.patch;
    }
    friend bool operator!=(const Version& a, const Version& b) { return !(a == b); }
    friend bool operator<(const Version& a, const Version& b)
    {
        if (a.major != b.major)
            return a.major < b.major;
        if (a.minor != b.minor)
            return a.minor < b.minor;
        return a.patch < b.patch;
    }
    friend bool operator>(const Version& a, const Version& b) { return b < a; }
};

struct Release {
    Version version;
    QString tag;         // "v1.2.3"
    QString name;        // The release's title.
    QString notes;       // Its body, as published (plain text to show).
    QDateTime published;
    QUrl page;           // The release page on github.com.
    QUrl zip;            // MyBooksLibrary-X.Y.Z-win64.zip
    QUrl sha256;         // Its checksum file.
    qint64 zipSize = 0;  // Bytes, as GitHub reports it.
};

// The asset names the release workflow publishes for `version`.
QString zipAssetName(const Version& version);

// True for https://github.com/<repository>/releases/download/<tag>/<name>.
bool isReleaseDownloadUrl(const QUrl& url, const QString& repository, const QString& tag, const QString& name);

// The usable releases in GitHub's JSON answer, newest first. `error` is set
// (and the list empty) when the answer is not a list of releases.
QList<Release> parseReleases(const QByteArray& json, const QString& repository, QString* error = nullptr);

// The releases newer than `current`, newest first.
QList<Release> newerReleases(const QList<Release>& releases, const Version& current);

// The SHA-256 (lower-case hex) for `fileName` in a .sha256 file written as
// "<64 hex>  <name>" (sha256sum's format; the name may be omitted).
std::optional<QByteArray> parseSha256File(const QByteArray& content, const QString& fileName);

} // namespace mbl::update
