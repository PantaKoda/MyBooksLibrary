#include "update/updatedownloader.h"

#include "update/installinfo.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>

namespace mbl::update {

namespace {

constexpr qint64 maxChecksumBytes = 4096;

QString defaultTar()
{
    const QString root = qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows"));
    return QDir(root).filePath(QStringLiteral("System32/tar.exe"));
}

} // namespace

UpdateDownloader::UpdateDownloader(QNetworkAccessManager* network, QObject* parent)
    : QObject(parent), m_network(network), m_tar(defaultTar())
{
}

UpdateDownloader::~UpdateDownloader()
{
    if (m_running)
        cancel();
}

bool UpdateDownloader::isTrustedUrl(const QUrl& url)
{
    if (!url.isValid() || url.scheme() != QLatin1String("https") || !url.userInfo().isEmpty())
        return false;
    const QString host = url.host().toLower();
    return host == QLatin1String("github.com") || host.endsWith(QLatin1String(".githubusercontent.com"));
}

QNetworkReply* UpdateDownloader::get(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("MyBooksLibrary/%1").arg(QCoreApplication::applicationVersion()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setTransferTimeout(m_stallTimeoutMs);
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::redirected, reply, [this, reply](const QUrl& target) {
        if (isTrustedUrl(target)) {
            emit reply->redirectAllowed();
        } else {
            // fail() aborts the reply after disconnecting it, so its error is not reported instead.
            fail(tr("The download was redirected to an unexpected address (%1).").arg(target.host()));
        }
    });
    return reply;
}

void UpdateDownloader::start(const Release& release, const QString& stagingRoot)
{
    if (m_running)
        return;
    const auto allowed = [this](const QUrl& url) {
        return isTrustedUrl(url) || (m_allowLocalUrls && url.isLocalFile());
    };
    if (!allowed(release.zip) || !allowed(release.sha256)) {
        emit failed(tr("The release's download addresses are not GitHub's."), false);
        return;
    }
    m_running = true;
    m_release = release;
    m_folder = QDir(stagingRoot).filePath(release.version.toString());
    m_expectedSha.clear();
    // A fresh folder: whatever an interrupted attempt left is not trusted.
    if (QFileInfo::exists(m_folder) && !QDir(m_folder).removeRecursively()) {
        fail(tr("Could not clear the update folder %1.").arg(QDir::toNativeSeparators(m_folder)));
        return;
    }
    if (!QDir().mkpath(m_folder)) {
        fail(tr("Could not create the update folder %1.").arg(QDir::toNativeSeparators(m_folder)));
        return;
    }
    emit stageChanged(QStringLiteral("checksum"));
    m_reply = get(release.sha256);
    connect(m_reply, &QNetworkReply::finished, this, &UpdateDownloader::onChecksum);
}

void UpdateDownloader::onChecksum()
{
    QNetworkReply* reply = m_reply;
    if (!reply || !m_running)
        return;
    reply->deleteLater();
    m_reply = nullptr;
    if (reply->error() != QNetworkReply::NoError) {
        fail(tr("Could not download the checksum: %1").arg(reply->errorString()));
        return;
    }
    const std::optional<QByteArray> sha = parseSha256File(reply->read(maxChecksumBytes), zipAssetName(m_release.version));
    if (!sha) {
        fail(tr("The release's checksum file could not be read."));
        return;
    }
    m_expectedSha = *sha;

    m_file = std::make_unique<QFile>(QDir(m_folder).filePath(zipAssetName(m_release.version) + QStringLiteral(".part")));
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(tr("Could not write the download: %1").arg(m_file->errorString()));
        return;
    }
    m_hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    emit stageChanged(QStringLiteral("download"));
    m_reply = get(m_release.zip);
    connect(m_reply, &QNetworkReply::readyRead, this, &UpdateDownloader::onZipData);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        emit progress(received, total > 0 ? total : m_release.zipSize);
    });
    connect(m_reply, &QNetworkReply::finished, this, &UpdateDownloader::onZipFinished);
}

void UpdateDownloader::onZipData()
{
    if (!m_reply || !m_file)
        return;
    const QByteArray chunk = m_reply->readAll();
    m_hash->addData(chunk);
    if (m_file->write(chunk) != chunk.size()) {
        const QString error = m_file->errorString();
        m_reply->abort();
        fail(tr("Could not write the download: %1").arg(error));
    }
}

void UpdateDownloader::onZipFinished()
{
    QNetworkReply* reply = m_reply;
    if (!reply || !m_running)
        return;
    onZipData();
    if (!m_running)
        return;
    reply->deleteLater();
    m_reply = nullptr;
    if (reply->error() != QNetworkReply::NoError) {
        fail(reply->error() == QNetworkReply::OperationCanceledError
                 ? tr("The download stopped: nothing was received for %1 seconds.").arg(m_stallTimeoutMs / 1000)
                 : tr("The download failed: %1").arg(reply->errorString()));
        return;
    }
    emit stageChanged(QStringLiteral("verify"));
    m_file->close();
    const QByteArray actual = m_hash->result().toHex();
    if (actual != m_expectedSha) {
        fail(tr("The download does not match the release's checksum, so it was not installed."));
        return;
    }
    const QString zip = QDir(m_folder).filePath(zipAssetName(m_release.version));
    if (!m_file->rename(zip)) {
        fail(tr("Could not keep the download: %1").arg(m_file->errorString()));
        return;
    }
    m_file.reset();

    emit stageChanged(QStringLiteral("unpack"));
    const QString target = QDir(m_folder).filePath(QStringLiteral("unpacked"));
    if (!QDir().mkpath(target)) {
        fail(tr("Could not create the update folder %1.").arg(QDir::toNativeSeparators(target)));
        return;
    }
    m_unpack = new QProcess(this);
    m_unpack->setProgram(m_tar);
    m_unpack->setArguments({QStringLiteral("-xf"), QDir::toNativeSeparators(zip), QStringLiteral("-C"),
                            QDir::toNativeSeparators(target)});
    m_unpack->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_unpack, &QProcess::finished, this, [this](int exitCode) { onUnpacked(exitCode); });
    connect(m_unpack, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            fail(tr("Could not unpack the update: %1 did not start.").arg(QDir::toNativeSeparators(m_tar)));
    });
    m_unpack->start();
}

void UpdateDownloader::onUnpacked(int exitCode)
{
    if (!m_running || !m_unpack)
        return;
    const QString output = QString::fromLocal8Bit(m_unpack->readAll()).trimmed();
    m_unpack->deleteLater();
    m_unpack = nullptr;
    if (exitCode != 0) {
        fail(tr("Could not unpack the update (%1).").arg(output.left(300)));
        return;
    }
    const QString app = QDir(m_folder).filePath(QStringLiteral("unpacked/MyBooksLibrary"));
    const std::optional<Version> marker = readReleaseMarker(app);
    if (!QFileInfo(QDir(app).filePath(QString::fromLatin1(appExecutableName))).isFile() || !marker
        || *marker != m_release.version) {
        fail(tr("The downloaded package does not hold MyBooksLibrary %1.").arg(m_release.version.toString()));
        return;
    }
    m_running = false;
    emit finished(app);
}

void UpdateDownloader::cancel()
{
    if (!m_running)
        return;
    fail(tr("Cancelled."), true);
}

void UpdateDownloader::fail(const QString& error, bool cancelled)
{
    if (!m_running)
        return;
    m_running = false;
    cleanUp();
    emit failed(error, cancelled);
}

void UpdateDownloader::cleanUp()
{
    if (m_reply) {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (m_unpack) {
        QProcess* unpack = m_unpack;
        m_unpack = nullptr;
        unpack->disconnect(this);
        unpack->kill();
        unpack->waitForFinished(5000);
        unpack->deleteLater();
    }
    m_file.reset();
    m_hash.reset();
    if (!m_folder.isEmpty())
        QDir(m_folder).removeRecursively();
}

} // namespace mbl::update
