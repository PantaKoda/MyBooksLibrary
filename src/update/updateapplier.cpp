#include "update/updateapplier.h"

#include "update/installinfo.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThread>

#include <algorithm>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace mbl::update {

namespace {

bool hasApp(const QString& folder)
{
    return QFileInfo(QDir(folder).filePath(QString::fromLatin1(appExecutableName))).isFile()
           && readReleaseMarker(folder).has_value();
}

// Waits until the process `pid` has exited, at most `timeoutMs`.
bool waitForExit(qint64 pid, int timeoutMs)
{
#ifdef Q_OS_WIN
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!process)
        return true;  // Already gone.
    const DWORD result = WaitForSingleObject(process, static_cast<DWORD>(timeoutMs));
    CloseHandle(process);
    return result == WAIT_OBJECT_0;
#else
    Q_UNUSED(pid)
    QThread::msleep(static_cast<unsigned long>(std::min(timeoutMs, 3000)));
    return true;
#endif
}

} // namespace

bool copyFolder(const QString& from, const QString& to, QString* error)
{
    const QDir source(from);
    if (!QDir().mkpath(to)) {
        if (error)
            *error = QStringLiteral("cannot create %1").arg(QDir::toNativeSeparators(to));
        return false;
    }
    QDirIterator it(from, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info = it.fileInfo();
        const QString target = QDir(to).filePath(source.relativeFilePath(path));
        if (info.isSymLink()) {
            if (error)
                *error = QStringLiteral("unexpected link %1").arg(QDir::toNativeSeparators(path));
            return false;
        }
        const bool ok = info.isDir() ? QDir().mkpath(target) : QFile::copy(path, target);
        if (!ok) {
            if (error)
                *error = QStringLiteral("cannot copy %1").arg(QDir::toNativeSeparators(path));
            return false;
        }
    }
    return true;
}

ApplyResult applyUpdate(const QString& installFolder, const QString& stagedFolder, const UpdateLog& log)
{
    const QString install = QDir::cleanPath(QFileInfo(installFolder).absoluteFilePath());
    const QString staged = QDir::cleanPath(QFileInfo(stagedFolder).absoluteFilePath());
    const QString previous = install + QStringLiteral(".previous");
    const auto fail = [&](const QString& error, bool restored = true) {
        log(QStringLiteral("failed: %1").arg(error));
        return ApplyResult{false, restored, error};
    };

    if (!hasApp(install))
        return fail(QStringLiteral("%1 does not hold a MyBooksLibrary release").arg(QDir::toNativeSeparators(install)));
    if (!hasApp(staged))
        return fail(QStringLiteral("%1 does not hold a MyBooksLibrary release").arg(QDir::toNativeSeparators(staged)));
    if (staged.compare(install, Qt::CaseInsensitive) == 0
        || staged.startsWith(install + u'/', Qt::CaseInsensitive))
        return fail(QStringLiteral("the new copy is inside the app's folder"));

    if (QFileInfo::exists(previous)) {
        log(QStringLiteral("removing the previous version kept at %1").arg(QDir::toNativeSeparators(previous)));
        if (!QDir(previous).removeRecursively())
            return fail(QStringLiteral("could not remove %1").arg(QDir::toNativeSeparators(previous)));
    }
    log(QStringLiteral("moving %1 to %2").arg(QDir::toNativeSeparators(install), QDir::toNativeSeparators(previous)));
    // Retried briefly: the old app's files may stay locked a moment after it exits.
    bool moved = false;
    for (int attempt = 0; attempt < 10 && !moved; ++attempt) {
        moved = QDir().rename(install, previous);
        if (!moved)
            QThread::msleep(500);
    }
    if (!moved)
        return fail(QCoreApplication::translate(
            "Updates", "the app's folder is in use; close every MyBooksLibrary window and try again"));

    log(QStringLiteral("copying %1 to %2").arg(QDir::toNativeSeparators(staged), QDir::toNativeSeparators(install)));
    QString copyError;
    if (copyFolder(staged, install, &copyError) && hasApp(install)) {
        log(QStringLiteral("done"));
        return ApplyResult{true, false, {}};
    }
    log(QStringLiteral("copy failed (%1); restoring the previous version").arg(copyError));
    const bool cleared = !QFileInfo::exists(install) || QDir(install).removeRecursively();
    const bool restored = cleared && QDir().rename(previous, install);
    if (!restored)
        return fail(QStringLiteral("%1, and the previous version could not be put back; it is at %2")
                        .arg(copyError, QDir::toNativeSeparators(previous)),
                    false);
    return fail(copyError.isEmpty() ? QStringLiteral("the copied app is incomplete") : copyError);
}

int runUpdater(const QStringList& args)
{
    const auto option = [&args](const char* name) {
        const qsizetype at = args.indexOf(QLatin1String(name));
        return at >= 0 && at + 1 < args.size() ? args.at(at + 1) : QString();
    };
    const QString install = option("--apply-update");
    const QString fromVersion = option("--from-version");
    const qint64 pid = option("--wait-pid").toLongLong();
    QFile logFile(option("--log"));
    const bool logging = !logFile.fileName().isEmpty() && logFile.open(QIODevice::Append | QIODevice::Text);
    const UpdateLog log = [&](const QString& line) {
        if (logging) {
            logFile.write(QStringLiteral("%1 %2\n")
                              .arg(QDateTime::currentDateTime().toString(Qt::ISODate), line)
                              .toUtf8());
            logFile.flush();
        }
    };
    const QString staged = QCoreApplication::applicationDirPath();
    log(QStringLiteral("update from %1 in %2, new copy %3")
            .arg(fromVersion, QDir::toNativeSeparators(install), QDir::toNativeSeparators(staged)));
    if (install.isEmpty() || pid <= 0) {
        log(QStringLiteral("failed: missing arguments"));
        return 2;
    }
    if (!waitForExit(pid, 120000)) {
        // The old app is still running: nothing is changed.
        log(QStringLiteral("failed: the app did not exit within 2 minutes; nothing changed"));
        return 3;
    }

    const ApplyResult result = applyUpdate(install, staged, log);
    const QString exe = QDir(install).filePath(QString::fromLatin1(appExecutableName));
    QStringList restartArgs;
    if (result.ok)
        restartArgs = {QStringLiteral("--updated-from"), fromVersion};
    else
        restartArgs = {QStringLiteral("--update-failed"), result.error};
    if (const QString library = option("--restart-library"); !library.isEmpty())
        restartArgs << QStringLiteral("--library") << library << QStringLiteral("--existing-library");
    if (result.ok || result.restored) {
        const bool started = QProcess::startDetached(exe, restartArgs, install);
        log(QStringLiteral("%1 %2").arg(started ? QStringLiteral("started") : QStringLiteral("could not start"),
                                        QDir::toNativeSeparators(exe)));
    }
    return result.ok ? 0 : 1;
}

} // namespace mbl::update
