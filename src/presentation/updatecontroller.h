// Presentation: checking for a newer release on GitHub and installing it
// (docs/UPDATES.md). Modelled on repo-watch's in-app updates.
//
// - Checking: anonymously (no token), once a day when "Check automatically"
//   is on (first 30 s after start), and on request. A check lists every
//   newer release with its notes (plain text), newest first.
// - Installing, only on request: UpdateDownloader fetches and verifies the
//   newest release into <app data>/updates. Then it starts the new copy,
//   which waits for this process to exit (UpdateApplier), and only if that
//   started does the window close through its normal flow (work stops
//   first). The new copy then replaces the app's folder and starts the new
//   version. A copy that cannot be started is reported in the window, which
//   stays open.
// - A copy that was not installed from a release, or whose folder holds the
//   library or the staging folder, never replaces itself (checkInstall);
//   the window offers the release page instead.
// - Starting after an update: --updated-from <version> shows "Updated to …"
//   and tidies the staging folder; --update-failed <reason> explains why the
//   previous version is still running.
// Settings: updates/autoCheck (default on) and updates/lastCheck.
#pragma once

#include "update/releases.h"

#include <QDateTime>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QSettings;
class QTimer;

namespace mbl::update {
class UpdateDownloader;
}

namespace mbl::presentation {

class UpdateController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the composition root.")
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT FINAL)
    Q_PROPERTY(State state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool available READ available NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY stateChanged FINAL)
    // [{version, name, date, notes}], newest first.
    Q_PROPERTY(QVariantList releases READ releases NOTIFY stateChanged FINAL)
    Q_PROPERTY(QUrl releasePage READ releasePage NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString cannotInstallReason READ cannotInstallReason NOTIFY stateChanged FINAL)
    // 0–1 while downloading; -1 when unknown.
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged FINAL)
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY autoCheckChanged FINAL)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged FINAL)
    Q_PROPERTY(bool noticeIsError READ noticeIsError NOTIFY noticeChanged FINAL)

public:
    enum State { Idle, Checking, UpToDate, Available, CheckFailed, Installing, ReadyToRestart, InstallFailed };
    Q_ENUM(State)

    struct Config {
        QString repository;       // "owner/name"
        update::Version current;
        QString appFolder;        // The running executable's folder.
        QString dataFolder;       // <app data>: holds updates/.
        // Folders that must stay outside the app's folder (the library in use).
        std::function<QStringList()> keptFolders;
        // The library the window has, reopened after the update.
        std::function<QString()> currentLibrary;
    };

    UpdateController(Config config, QSettings* settings, QNetworkAccessManager* network, QObject* parent = nullptr);
    ~UpdateController() override;

    QString currentVersion() const { return m_config.current.toString(); }
    State state() const { return m_state; }
    QString statusText() const;
    bool available() const { return !m_newer.isEmpty(); }
    QString latestVersion() const;
    QVariantList releases() const;
    QUrl releasePage() const;
    bool canInstall() const;
    QString cannotInstallReason() const;
    double progress() const { return m_progress; }
    bool autoCheck() const { return m_autoCheck; }
    void setAutoCheck(bool on);
    QString notice() const { return m_notice; }
    bool noticeIsError() const { return m_noticeIsError; }

    QString stagingFolder() const;
    QString logFile() const;

    // The daily check, if on. Not called in development modes.
    void startAutomaticChecks();
    // --updated-from / --update-failed, before the window shows.
    void handleStartArguments(const QStringList& args);
    // Tests: read releases from this URL (file:// allowed), and start the
    // hand-over with this instead of QProcess::startDetached.
    using Starter = std::function<bool(const QString& program, const QStringList& args, const QString& folder)>;
    void setReleasesUrl(const QUrl& url) { m_releasesUrl = url; }
    void setStarter(Starter starter) { m_starter = std::move(starter); }
    update::UpdateDownloader* downloader() const { return m_downloader; }

    Q_INVOKABLE void check();
    Q_INVOKABLE void install();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void dismissNotice();

signals:
    void stateChanged();
    void progressChanged();
    void autoCheckChanged();
    void noticeChanged();
    // An install is ready: the window should close so the update can be put in place.
    void restartRequested();

private slots:
    // The verified, unpacked new copy: start it and close (see above).
    void onDownloaded(const QString& stagedApp);

private:
    void setState(State state, const QString& message = {});
    void checkIfDue();
    void refreshInstallCheck();  // File-system checks: only after a check and before installing.
    void onReleases();
    void setNotice(const QString& notice, bool error);

    Config m_config;
    QSettings* m_settings;
    QNetworkAccessManager* m_network;
    update::UpdateDownloader* m_downloader;
    QTimer* m_dailyTimer;
    QUrl m_releasesUrl;
    QNetworkReply* m_reply = nullptr;

    State m_state = Idle;
    QString m_message;
    QList<update::Release> m_newer;
    double m_progress = -1;
    QString m_stage;
    QString m_stagedApp;
    QString m_installBlocker;
    Starter m_starter;
    bool m_autoCheck = true;
    QString m_notice;
    bool m_noticeIsError = false;
};

} // namespace mbl::presentation
