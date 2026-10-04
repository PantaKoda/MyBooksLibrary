#include "presentation/updatecontroller.h"

#include "update/installinfo.h"
#include "update/updatedownloader.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QVariantMap>

#include <chrono>
#include <cmath>

namespace mbl::presentation {

namespace {

const auto autoCheckKey = QStringLiteral("updates/autoCheck");
const auto lastCheckKey = QStringLiteral("updates/lastCheck");
constexpr int firstCheckDelayMs = 30 * 1000;
constexpr qint64 checkIntervalSecs = 24 * 60 * 60;
constexpr qint64 maxReleasesBytes = 4 * 1024 * 1024;

} // namespace

UpdateController::UpdateController(Config config, QSettings* settings, QNetworkAccessManager* network, QObject* parent)
    : QObject(parent),
      m_config(std::move(config)),
      m_settings(settings),
      m_network(network),
      m_downloader(new update::UpdateDownloader(network, this)),
      m_dailyTimer(new QTimer(this)),
      m_releasesUrl(QStringLiteral("https://api.github.com/repos/%1/releases?per_page=30").arg(m_config.repository))
{
    if (m_settings)
        m_autoCheck = m_settings->value(autoCheckKey, true).toBool();
    m_dailyTimer->setInterval(std::chrono::hours(1));
    connect(m_dailyTimer, &QTimer::timeout, this, &UpdateController::checkIfDue);

    connect(m_downloader, &update::UpdateDownloader::stageChanged, this, [this](const QString& stage) {
        m_stage = stage;
        if (stage != QLatin1String("download")) {
            m_progress = -1;
            emit progressChanged();
        }
        emit stateChanged();
    });
    connect(m_downloader, &update::UpdateDownloader::progress, this, [this](qint64 received, qint64 total) {
        // Whole percents only: no flood of updates for a 150 MB download.
        const double value = total > 0 ? std::floor(100.0 * received / total) / 100.0 : -1;
        if (value != m_progress) {
            m_progress = value;
            emit progressChanged();
        }
    });
    connect(m_downloader, &update::UpdateDownloader::finished, this, [this](const QString& app) {
        m_stagedApp = app;
        setState(ReadyToRestart);
        emit restartRequested();
    });
    connect(m_downloader, &update::UpdateDownloader::failed, this, [this](const QString& error, bool cancelled) {
        if (cancelled)
            setState(Available);
        else
            setState(InstallFailed, error);
    });
}

UpdateController::~UpdateController()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
    }
}

QString UpdateController::stagingFolder() const
{
    return QDir(m_config.dataFolder).filePath(QStringLiteral("updates"));
}

QString UpdateController::logFile() const
{
    return QDir(stagingFolder()).filePath(QStringLiteral("update.log"));
}

QString UpdateController::statusText() const
{
    switch (m_state) {
    case Idle:
        return {};
    case Checking:
        return tr("Checking for updates…");
    case UpToDate:
        return tr("MyBooksLibrary %1 is the latest version.").arg(currentVersion());
    case Available:
        return m_newer.size() == 1 ? tr("Version %1 is available.").arg(latestVersion())
                                   : tr("Version %1 is available (%2 releases since yours).")
                                         .arg(latestVersion())
                                         .arg(m_newer.size());
    case CheckFailed:
        return tr("Could not check for updates: %1").arg(m_message);
    case Installing:
        if (m_stage == QLatin1String("download"))
            return tr("Downloading version %1…").arg(latestVersion());
        if (m_stage == QLatin1String("verify"))
            return tr("Checking the download…");
        if (m_stage == QLatin1String("unpack"))
            return tr("Unpacking…");
        return tr("Preparing the download…");
    case ReadyToRestart:
        return tr("Closing to finish the update. MyBooksLibrary %1 will start by itself.").arg(latestVersion());
    case InstallFailed:
        return tr("The update was not installed: %1").arg(m_message);
    }
    return {};
}

QString UpdateController::latestVersion() const
{
    return m_newer.isEmpty() ? QString() : m_newer.first().version.toString();
}

QVariantList UpdateController::releases() const
{
    QVariantList list;
    for (const update::Release& release : m_newer) {
        list.append(QVariantMap{
            {QStringLiteral("version"), release.version.toString()},
            {QStringLiteral("name"), release.name.isEmpty() ? release.tag : release.name},
            {QStringLiteral("date"), release.published.isValid()
                                         ? QLocale().toString(release.published.toLocalTime().date(), QLocale::LongFormat)
                                         : QString()},
            {QStringLiteral("notes"), release.notes},
        });
    }
    return list;
}

QUrl UpdateController::releasePage() const
{
    if (!m_newer.isEmpty() && m_newer.first().page.isValid())
        return m_newer.first().page;
    return QUrl(QStringLiteral("https://github.com/%1/releases/latest").arg(m_config.repository));
}

bool UpdateController::canInstall() const
{
    return cannotInstallReason().isEmpty();
}

QString UpdateController::cannotInstallReason() const
{
    return m_newer.isEmpty() ? tr("There is no newer release.") : m_installBlocker;
}

void UpdateController::refreshInstallCheck()
{
    QStringList kept{stagingFolder()};
    if (m_config.keptFolders)
        kept += m_config.keptFolders();
    m_installBlocker = update::checkInstall(m_config.appFolder, m_config.current, kept).reason;
}

void UpdateController::setAutoCheck(bool on)
{
    if (on == m_autoCheck)
        return;
    m_autoCheck = on;
    if (m_settings)
        m_settings->setValue(autoCheckKey, on);
    emit autoCheckChanged();
}

void UpdateController::startAutomaticChecks()
{
    QTimer::singleShot(firstCheckDelayMs, this, &UpdateController::checkIfDue);
    m_dailyTimer->start();
}

void UpdateController::checkIfDue()
{
    const QDateTime last = m_settings ? m_settings->value(lastCheckKey).toDateTime() : QDateTime();
    if (m_autoCheck && (!last.isValid() || last.secsTo(QDateTime::currentDateTimeUtc()) >= checkIntervalSecs))
        check();
}

void UpdateController::check()
{
    if (m_state == Checking || m_state == Installing || m_state == ReadyToRestart)
        return;
    setState(Checking);
    QNetworkRequest request(m_releasesUrl);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("MyBooksLibrary/%1").arg(currentVersion()));
    request.setTransferTimeout(30000);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &UpdateController::onReleases);
}

void UpdateController::onReleases()
{
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    if (!reply)
        return;
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError) {
        const QString reason = status == 403 || status == 429
                                   ? tr("GitHub limits how often it may be asked; try again in an hour.")
                                   : reply->errorString();
        setState(CheckFailed, reason);
        return;
    }
    QString error;
    const QList<update::Release> all = update::parseReleases(reply->read(maxReleasesBytes), m_config.repository, &error);
    if (!error.isEmpty()) {
        setState(CheckFailed, error);
        return;
    }
    if (m_settings)
        m_settings->setValue(lastCheckKey, QDateTime::currentDateTimeUtc());
    m_newer = update::newerReleases(all, m_config.current);
    refreshInstallCheck();
    setState(m_newer.isEmpty() ? UpToDate : Available);
}

void UpdateController::install()
{
    if (m_state == Installing || m_state == ReadyToRestart || m_newer.isEmpty())
        return;
    refreshInstallCheck();
    if (!canInstall()) {
        setState(InstallFailed, cannotInstallReason());
        return;
    }
    m_progress = -1;
    m_stage.clear();
    emit progressChanged();
    setState(Installing);
    m_downloader->start(m_newer.first(), stagingFolder());
}

void UpdateController::cancel()
{
    if (m_state == Installing)
        m_downloader->cancel();
}

void UpdateController::dismissNotice()
{
    setNotice({}, false);
}

bool UpdateController::startHandover(qint64 pid)
{
    if (m_state != ReadyToRestart)
        return false;
    const QString exe = QDir(m_stagedApp).filePath(QString::fromLatin1(update::appExecutableName));
    QStringList args{QStringLiteral("--apply-update"), QDir::toNativeSeparators(m_config.appFolder),
                     QStringLiteral("--wait-pid"), QString::number(pid),
                     QStringLiteral("--from-version"), currentVersion(),
                     QStringLiteral("--log"), QDir::toNativeSeparators(logFile())};
    if (m_config.currentLibrary) {
        if (const QString library = m_config.currentLibrary(); !library.isEmpty())
            args << QStringLiteral("--restart-library") << QDir::toNativeSeparators(library);
    }
    return QProcess::startDetached(exe, args, m_stagedApp);
}

void UpdateController::handleStartArguments(const QStringList& args)
{
    const auto option = [&args](const char* name) {
        const qsizetype at = args.indexOf(QLatin1String(name));
        return at >= 0 && at + 1 < args.size() ? args.at(at + 1) : QString();
    };
    if (const QString from = option("--updated-from"); !from.isEmpty()) {
        setNotice(tr("Updated from %1 to %2.").arg(from, currentVersion()), false);
        // The new copy that put this one in place has exited by now, or soon:
        // the downloads and unpacked copies go; the log stays.
        QTimer::singleShot(15000, this, [this] {
            QDirIterator it(stagingFolder(), QDir::Dirs | QDir::NoDotAndDotDot);
            while (it.hasNext())
                QDir(it.next()).removeRecursively();
        });
    } else if (const QString reason = option("--update-failed"); !reason.isEmpty()) {
        setNotice(tr("The update could not be installed, so this version is still in use: %1. Details are in %2.")
                      .arg(reason, QDir::toNativeSeparators(logFile())),
                  true);
    }
}

void UpdateController::setState(State state, const QString& message)
{
    m_state = state;
    m_message = message;
    emit stateChanged();
}

void UpdateController::setNotice(const QString& notice, bool error)
{
    if (notice == m_notice && error == m_noticeIsError)
        return;
    m_notice = notice;
    m_noticeIsError = error;
    emit noticeChanged();
}

} // namespace mbl::presentation
