#include "app/libraryroot.h"

#include <QDir>
#include <QStandardPaths>

namespace mbl::app {

LibraryRoot resolveLibraryRoot(const QStringList& arguments)
{
    const qsizetype flag = arguments.indexOf(QStringLiteral("--library"));
    if (flag >= 0 && flag + 1 < arguments.size())
        return {QDir::cleanPath(QDir(arguments.at(flag + 1)).absolutePath()), QStringLiteral("command line")};
    const QString fromEnv = qEnvironmentVariable("MYBOOKSLIBRARY_ROOT");
    if (!fromEnv.isEmpty())
        return {QDir::cleanPath(QDir(fromEnv).absolutePath()), QStringLiteral("environment")};
    return {defaultLibraryRoot(), QStringLiteral("default")};
}

QString defaultLibraryRoot()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir::cleanPath(QDir(base).filePath(QStringLiteral("Library")));
}

} // namespace mbl::app
