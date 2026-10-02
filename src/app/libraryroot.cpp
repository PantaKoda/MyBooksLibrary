#include "app/libraryroot.h"

#include <QDir>
#include <QStandardPaths>

namespace mbl::app {

namespace {

QString cleaned(const QString& path)
{
    return QDir::cleanPath(QDir(path).absolutePath());
}

bool samePath(const QString& a, const QString& b)
{
    const Qt::CaseSensitivity cs =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    return cleaned(a).compare(cleaned(b), cs) == 0;
}

} // namespace

LibraryRoot resolveLibraryRoot(const QStringList& arguments, const QString& remembered)
{
    const qsizetype flag = arguments.indexOf(QStringLiteral("--library"));
    if (flag >= 0 && flag + 1 < arguments.size())
        return {cleaned(arguments.at(flag + 1)), QStringLiteral("command line")};
    const QString fromEnv = qEnvironmentVariable("MYBOOKSLIBRARY_ROOT");
    if (!fromEnv.isEmpty())
        return {cleaned(fromEnv), QStringLiteral("environment")};
    // The default library, even when remembered, stays the default: it is
    // made again on first use, as on a first start.
    if (!remembered.isEmpty() && !samePath(remembered, defaultLibraryRoot()))
        return {cleaned(remembered), QStringLiteral("remembered")};
    return {defaultLibraryRoot(), QStringLiteral("default")};
}

QString defaultLibraryRoot()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir::cleanPath(QDir(base).filePath(QStringLiteral("Library")));
}

} // namespace mbl::app
