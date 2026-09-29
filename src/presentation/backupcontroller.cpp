#include "presentation/backupcontroller.h"

#include "catalog/library.h"
#include "storage/backup.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QMetaObject>
#include <QProcess>
#include <QStandardPaths>

namespace mbl::presentation {

namespace {

QString trn(const char* text, int n)
{
    return QCoreApplication::translate("mbl::presentation::BackupController", text, nullptr, n);
}

QString megabytes(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1);
}

QString documents()
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return folder.isEmpty() ? QDir::homePath() : folder;
}

// "MyBooksLibrary restored <date>" in `parent`, or "... (2)", "... (3)" when
// that folder is taken (a second restore the same day): a restore needs a new
// or empty folder.
QString restoredFolderIn(const QString& parent)
{
    const QString base = QStringLiteral("MyBooksLibrary restored %1").arg(QDate::currentDate().toString(Qt::ISODate));
    const QDir dir(parent);
    QString candidate = dir.filePath(base);
    for (int n = 2; QFileInfo::exists(candidate); ++n)
        candidate = dir.filePath(QStringLiteral("%1 (%2)").arg(base).arg(n));
    return QDir::toNativeSeparators(candidate);
}

} // namespace

BackupController::BackupController(QObject* parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
    m_pool.setObjectName(QStringLiteral("mbl-backup"));
}

BackupController::~BackupController()
{
    stop();
}

void BackupController::stop()
{
    m_cancel->store(true);
    m_pool.waitForDone();
}

void BackupController::setLibrary(std::shared_ptr<catalog::Library> library)
{
    m_library = std::move(library);
}

QString BackupController::pathOf(const QString& pathOrUrl)
{
    const QString trimmed = pathOrUrl.trimmed();
    return trimmed.startsWith(QLatin1String("file:"), Qt::CaseInsensitive) ? QUrl(trimmed).toLocalFile() : trimmed;
}

QString BackupController::localPath(const QUrl& url) const
{
    return QDir::toNativeSeparators(url.toLocalFile());
}

QString BackupController::restoreFolderIn(const QUrl& parent) const
{
    return restoredFolderIn(parent.toLocalFile());
}

QUrl BackupController::resultFolderUrl() const
{
    return m_resultFolder.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_resultFolder);
}

QString BackupController::suggestedBackupFolder() const
{
    return QDir::toNativeSeparators(documents());
}

QString BackupController::suggestedRestoreFolder() const
{
    return restoredFolderIn(documents());
}

void BackupController::start(Operation operation, const QString& text)
{
    m_cancel->store(false);
    m_operation = operation;
    m_running = true;
    m_succeeded = false;
    m_statusText = text;
    m_resultFolder.clear();
    m_done = 0;
    m_total = 0;
    emit progressChanged();
    emit stateChanged();
}

void BackupController::setProgress(int done, int total)
{
    if (!m_running || (done == m_done && total == m_total))
        return;
    m_done = done;
    m_total = total;
    emit progressChanged();
}

void BackupController::finish(const Outcome& outcome)
{
    m_running = false;
    m_succeeded = outcome.ok;
    m_statusText = outcome.text;
    m_resultFolder = outcome.ok ? QDir::toNativeSeparators(outcome.folder) : QString();
    emit stateChanged();
    emit finished();
}

void BackupController::backUp(const QString& folder)
{
    if (m_running || !m_library)
        return;
    start(Operation::Backup, tr("Backing up the library…"));
    const QString parent = pathOf(folder);
    QPointer<BackupController> self(this);
    m_pool.start([self, library = m_library, cancel = m_cancel, parent] {
        const auto progress = [self](int done, int total) {
            QMetaObject::invokeMethod(self, [self, done, total] {
                if (self)
                    self->setProgress(done, total);
            }, Qt::QueuedConnection);
        };
        const auto result = storage::createBackup(*library, parent, cancel.get(), progress);
        Outcome outcome;
        if (result) {
            const storage::BackupInfo& info = result.value();
            outcome.ok = true;
            outcome.folder = info.folder;
            outcome.text = tr("Backed up %1 (%2) to %3.")
                               .arg(trn("%n book(s)", info.books), megabytes(info.bytes),
                                    QDir::toNativeSeparators(info.folder));
            if (!info.missingReports.isEmpty())
                outcome.text += u' ' + trn("%n analysis report(s) were already missing from the library; the backup "
                                           "lists them.",
                                           int(info.missingReports.size()));
        } else if (cancel->load()) {
            outcome.cancelled = true;
            outcome.text = tr("Backup cancelled; nothing was saved.");
        } else {
            outcome.text = tr("Not backed up: %1").arg(result.error().message);
        }
        QMetaObject::invokeMethod(self, [self, outcome] {
            if (self)
                self->finish(outcome);
        }, Qt::QueuedConnection);
    });
}

void BackupController::restore(const QString& backupFolder, const QString& targetFolder)
{
    if (m_running || !m_library)
        return;
    start(Operation::Restore, tr("Checking the backup…"));
    const QString backup = pathOf(backupFolder);
    const QString target = pathOf(targetFolder);
    const QString inUse = m_library->rootDir();
    QPointer<BackupController> self(this);
    m_pool.start([self, cancel = m_cancel, backup, target, inUse] {
        const auto progress = [self](int done, int total) {
            QMetaObject::invokeMethod(self, [self, done, total] {
                if (self) {
                    if (self->m_done == 0)
                        self->m_statusText = tr("Restoring…");
                    self->setProgress(done, total);
                    emit self->stateChanged();
                }
            }, Qt::QueuedConnection);
        };
        const auto result = storage::restoreBackup(backup, target, {inUse}, cancel.get(), progress);
        Outcome outcome;
        if (result) {
            const storage::RestoreInfo& info = result.value();
            outcome.ok = true;
            outcome.folder = info.libraryFolder;
            outcome.text = tr("Restored %1 from the backup of %2 as a new library in %3.")
                               .arg(trn("%n book(s)", info.backup.books),
                                    QLocale().toString(info.backup.createdAt.toLocalTime(), QLocale::ShortFormat),
                                    QDir::toNativeSeparators(info.libraryFolder));
            if (info.exportsClosed > 0)
                outcome.text += u' ' + trn("%n waiting copy with bookmarks was not carried over; ask again if you still "
                                           "want it.",
                                           info.exportsClosed);
        } else if (cancel->load()) {
            outcome.cancelled = true;
            outcome.text = tr("Restore cancelled; nothing was changed.");
        } else {
            outcome.text = tr("Not restored: %1").arg(result.error().message);
        }
        QMetaObject::invokeMethod(self, [self, outcome] {
            if (self)
                self->finish(outcome);
        }, Qt::QueuedConnection);
    });
}

void BackupController::cancel()
{
    if (m_running)
        m_cancel->store(true);
}

void BackupController::reset()
{
    if (m_running)
        return;
    m_operation = Operation::None;
    m_succeeded = false;
    m_statusText.clear();
    m_resultFolder.clear();
    m_done = 0;
    m_total = 0;
    emit progressChanged();
    emit stateChanged();
}

bool BackupController::openRestoredLibrary()
{
    if (m_running || !m_succeeded || m_operation != Operation::Restore || m_resultFolder.isEmpty())
        return false;
    return QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                   {QStringLiteral("--library"), QDir::fromNativeSeparators(m_resultFolder)});
}

} // namespace mbl::presentation
