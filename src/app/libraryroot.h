// Composition: where the library folder is. First match wins:
//   1. --library <dir> on the command line;
//   2. the MYBOOKSLIBRARY_ROOT environment variable;
//   3. the library the user last opened from the app (app/librarymemory.h),
//      unless it is the default one;
//   4. <AppLocalDataLocation>/Library (on Windows
//      %LOCALAPPDATA%\MyBooksLibrary\MyBooksLibrary\Library).
#pragma once

#include <QString>
#include <QStringList>

namespace mbl::app {

struct LibraryRoot {
    QString path;
    QString source;  // "command line", "environment", "remembered" or "default".
};

// `arguments` as from QCoreApplication::arguments(); `remembered` the
// remembered library, or empty (a parameter, so tests never read the real
// settings). Requires the organization and application names to be set for
// the default location. A remembered library must already exist: the caller
// opens it with Library::OpenMode::ExistingOnly, so a moved one fails visibly
// rather than silently becoming a new, empty library.
LibraryRoot resolveLibraryRoot(const QStringList& arguments, const QString& remembered = {});

// The default library folder (4. above), whatever the command line, the
// environment and the settings say: what Library → Open the default library
// opens.
QString defaultLibraryRoot();

} // namespace mbl::app
