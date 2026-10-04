// Updates: putting a downloaded, verified release in place of the app.
//
// The running app cannot overwrite its own files, so it hands over to the new
// copy, unpacked in the staging folder, and quits:
//
//   <staged>\appMyBooksLibrary.exe --apply-update <install folder>
//       --wait-pid <old app's pid> --from-version <old version> --log <file>
//       [--restart-library <folder>]
//
// That copy (runUpdater) waits for the old app to exit, then applyUpdate:
//   1. removes the <install>.previous left by the update before, if any;
//   2. renames <install> to <install>.previous (refused while any file in it
//      is in use, for example by another MyBooksLibrary window: nothing has
//      changed then);
//   3. copies the staged copy to <install>;
//   4. on a failed copy, removes the partial <install> and renames
//      <install>.previous back.
// Then it starts <install>\appMyBooksLibrary.exe with --updated-from <old
// version>, or, after a failure, the restored old app with --update-failed
// <reason>; on the library the window had (--library <folder>
// --existing-library), when given. Every step is written to the log. The user's library, settings
// and the staging folder are outside <install> (checkInstall) and untouched.
#pragma once

#include <QString>
#include <QStringList>

#include <functional>

namespace mbl::update {

struct ApplyResult {
    bool ok = false;
    bool restored = true;  // After a failure: <install> is the previous app again.
    QString error;
};

using UpdateLog = std::function<void(const QString&)>;

ApplyResult applyUpdate(const QString& installFolder, const QString& stagedFolder, const UpdateLog& log);

// Copies `from` (a folder) to `to`, which must not exist. False on the first failure.
bool copyFolder(const QString& from, const QString& to, QString* error);

// The --apply-update mode (see above). `args` are the app's arguments.
// Returns the process exit code: 0 when the new version was started.
int runUpdater(const QStringList& args);

} // namespace mbl::update
