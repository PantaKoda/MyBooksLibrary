// Updates: may this copy of the app replace itself with a newer release?
//
// Only a copy unpacked from a release zip may. scripts/package.ps1 puts
// release.json next to the executable ({"product": "MyBooksLibrary",
// "version": "X.Y.Z"}); a build from source has none and is never replaced
// (the update window offers the release page instead). The marker's version
// must be the running one, so a stale marker never vouches for another build.
//
// The install folder is swapped as a whole (UpdateApplier), so nothing the
// user keeps may be inside it: not the library being used, not the update
// staging folder. Its parent folder must be writable, to hold the new copy
// and the previous one (<folder>.previous).
#pragma once

#include "update/releases.h"

#include <QString>

#include <optional>

namespace mbl::update {

inline constexpr const char* releaseMarkerName = "release.json";
inline constexpr const char* productName = "MyBooksLibrary";
inline constexpr const char* appExecutableName = "appMyBooksLibrary.exe";

// The version in `folder`/release.json, if it is this product's marker.
std::optional<Version> readReleaseMarker(const QString& folder);

// The marker's content for `version` (what package.ps1 writes).
QByteArray releaseMarker(const Version& version);

// The app's executable and a release marker: a folder an update may replace.
bool holdsRelease(const QString& folder);

// A MyBooksLibrary library (its library.sqlite) at any depth in `folder`.
// An update never moves or deletes a folder that holds one.
bool containsLibrary(const QString& folder);

// May an update replace <app>.previous? Yes when there is none, or when it
// holds only a previous release (no library, nothing else of the user's).
bool previousMayBeReplaced(const QString& previousFolder);

struct InstallCheck {
    bool canInstall = false;
    QString reason;  // Why not, for the user; empty when it can.
};

// `appFolder`: the running executable's folder. `keptFolders`: folders that
// must survive the swap (the library in use, the staging folder). Also
// refused: any library inside the app's folder (open or not), and an
// <app>.previous that is not only a previous release.
InstallCheck checkInstall(const QString& appFolder, const Version& running, const QStringList& keptFolders);

} // namespace mbl::update
