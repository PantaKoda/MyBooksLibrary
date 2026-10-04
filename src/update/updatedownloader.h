// Updates: downloading a release and preparing it to be put in place.
//
// For one Release, into <staging>/<version>/:
//   1. the .sha256 file (small), parsed for the zip's name;
//   2. the zip, streamed to disk and hashed as it arrives (no copy in
//      memory), with progress; a download that receives nothing for 30 s
//      stops;
//   3. the SHA-256 compared with the published one: on a mismatch nothing is
//      unpacked;
//   4. unpacked with Windows' own tar.exe (which refuses absolute and ".."
//      paths), then checked: MyBooksLibrary\appMyBooksLibrary.exe and a
//      release marker for exactly this version.
// Only https URLs on github.com and GitHub's download hosts
// (*.githubusercontent.com) are followed, redirects included. Everything runs
// asynchronously on the owning (GUI) thread: network and the unpacking
// process never block it. cancel() stops at any step and removes the folder.
#pragma once

#include "update/releases.h"

#include <QCryptographicHash>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

namespace mbl::update {

class UpdateDownloader : public QObject {
    Q_OBJECT

public:
    explicit UpdateDownloader(QNetworkAccessManager* network, QObject* parent = nullptr);
    ~UpdateDownloader() override;

    // Tests only: also accept file:// URLs.
    void setAllowLocalUrls(bool allow) { m_allowLocalUrls = allow; }
    void setTar(const QString& tarExecutable) { m_tar = tarExecutable; }
    void setStallTimeoutMs(int ms) { m_stallTimeoutMs = ms; }

    bool running() const { return m_running; }
    void start(const Release& release, const QString& stagingRoot);
    void cancel();

    // True if `url` may be fetched (https on github.com or a GitHub download host).
    static bool isTrustedUrl(const QUrl& url);

signals:
    void stageChanged(const QString& stage);  // "checksum", "download", "verify", "unpack"
    void progress(qint64 received, qint64 total);
    // The unpacked app's folder, ready for the hand-over.
    void finished(const QString& stagedAppFolder);
    void failed(const QString& error, bool cancelled);

private:
    QNetworkReply* get(const QUrl& url);
    void onChecksum();
    void onZipData();
    void onZipFinished();
    void onUnpacked(int exitCode);
    void fail(const QString& error, bool cancelled = false);
    void cleanUp();

    QNetworkAccessManager* m_network;
    bool m_allowLocalUrls = false;
    QString m_tar;
    int m_stallTimeoutMs = 30000;

    bool m_running = false;
    Release m_release;
    QString m_folder;
    QByteArray m_expectedSha;
    QPointer<QNetworkReply> m_reply;
    QPointer<QProcess> m_unpack;
    std::unique_ptr<QFile> m_file;
    std::unique_ptr<QCryptographicHash> m_hash;
};

} // namespace mbl::update
