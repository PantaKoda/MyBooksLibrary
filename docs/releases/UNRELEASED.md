<!--
Notes for the next release, collected as pull requests are merged (docs/RELEASING.md).
The release pull request renames this file to docs/releases/vX.Y.Z.md and replaces X.Y.Z.
Merged since v0.2.0: none yet.
-->
**MyBooksLibrary X.Y.Z**

## Download

**`MyBooksLibrary-X.Y.Z-win64.zip`**, for Windows 10 or 11 (64-bit).
- **Install or upgrade:** unpack it anywhere and run `appMyBooksLibrary.exe`. To upgrade, replace only the folder you unpacked the app into. Your library (`%LOCALAPPDATA%\MyBooksLibrary\MyBooksLibrary\Library`) stays where it is.
- **SmartScreen:** Windows may warn the first time, because the app is not code-signed yet.
- **`.sha256`:** the file next to the zip lets you check the download.

## New

## Fixed

- **No more "Why?" buttons.** A field the analysis could not fill now says why, in one line under it. The buttons say what they show: **Show candidates (*n*)** or **Show evidence**, and there is none when there is nothing more to show. On the **Contents** tab, **Why? (*n*)** is now **Analysis notes (*n*)**. When contents pages were found but the analysis could not settle on a table of contents, the summary says so instead of "No contents entries."

## Known limits
