<!--
Notes for the next release, collected as pull requests are merged (docs/RELEASING.md).
The release pull request renames this file to docs/releases/vX.Y.Z.md and replaces X.Y.Z.
Merged since v0.3.0: #42.
-->
**MyBooksLibrary X.Y.Z**

## Download

**`MyBooksLibrary-X.Y.Z-win64.zip`**, for Windows 10 or 11 (64-bit).
- **Install or upgrade:** unpack it anywhere and run `appMyBooksLibrary.exe`. To upgrade, replace only the folder you unpacked the app into. Your library (`%LOCALAPPDATA%\MyBooksLibrary\MyBooksLibrary\Library`) stays where it is.
- **SmartScreen:** Windows may warn the first time, because the app is not code-signed yet.
- **`.sha256`:** the file next to the zip lets you check the download.

## New

- **Themes and accent colours.** The new round **Appearance** button at the right of the toolbar chooses the theme (as Windows is set, light or dark) and an accent colour: Lapis blue, Teal, Violet, Rose, Graphite or Windows' own. The accent colours selections, the main buttons and a light tint on the bars and sidebar; the choice is kept for the next start. (#42)
- **Updates from inside the app.** MyBooksLibrary checks its Releases page once a day (or when you choose **Library → Check for updates…**). When a newer version exists, an **Update to X.Y.Z** button shows what is new, and **Install update** downloads it, checks it against the published checksum, and restarts into the new version on the same library. The previous version is kept next to it for going back. See "Updates" in the user guide. (#43)

## Fixed

## Known limits

- **Updating from 0.3.0 or earlier:** those versions cannot update themselves. Download this zip once, close the app, and replace the app's folder with the new one. From this version on, the app updates itself.
- **Updates are checked, not signed:** the download is checked against the checksum published with the release on GitHub. The app is not code-signed yet.
