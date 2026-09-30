// Composition: where the library folder is. First match wins:
//   1. --library <dir> on the command line;
//   2. the MYBOOKSLIBRARY_ROOT environment variable;
//   3. <AppLocalDataLocation>/Library (on Windows
//      %LOCALAPPDATA%\MyBooksLibrary\MyBooksLibrary\Library).
#pragma once

#include <QString>
#include <QStringList>

namespace mbl::app {

struct LibraryRoot {
    QString path;
    QString source;  // "command line", "environment" or "default".
};

// `arguments` as from QCoreApplication::arguments(). Requires the
// organization and application names to be set for the default location.
LibraryRoot resolveLibraryRoot(const QStringList& arguments);

// The default library folder (3. above), whatever the command line and the
// environment say: what Library → Open the default library opens.
QString defaultLibraryRoot();

} // namespace mbl::app
