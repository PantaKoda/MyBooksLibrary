<!--
Notes for the next release, collected as pull requests are merged (docs/RELEASING.md).
The release pull request renames this file to docs/releases/vX.Y.Z.md and replaces X.Y.Z.
Merged since v0.1.0: #31, #32, #33, #34.
-->
**MyBooksLibrary X.Y.Z** can open another library from the window and remembers it, and finds the pages of many more books' contents.

## Download

**`MyBooksLibrary-X.Y.Z-win64.zip`**, for Windows 10 or 11 (64-bit).
- **Install or upgrade:** unpack it anywhere and run `appMyBooksLibrary.exe`. To upgrade from 0.1.0, replace only the folder you unpacked the app into. Your library (`%LOCALAPPDATA%\MyBooksLibrary\MyBooksLibrary\Library`) stays where it is.
- **SmartScreen:** Windows may warn the first time, because the app is not code-signed yet.
- **`.sha256`:** the file next to the zip lets you check the download.

**New to the app?** Read `USER_GUIDE.md` in the zip (also [online](https://github.com/PantaKoda/MyBooksLibrary/blob/main/docs/USER_GUIDE.md)).

## New

- **Library → Open library…** opens another library, for example one you restored from a backup, in a new window or instead of the current one (#33).
  - The folder must already be a library. A folder that is not one is refused with the reason, for example Documents, an empty folder, or a library's own `files` folder. The app never creates a library in a folder you choose.
  - **Library → Open the default library** takes you back to your usual library.
- **The app remembers the library you opened last** and opens it at the next start (#34).
  - That means a library opened with **Open library…**, **Open the default library** or **Open restored library**. A library opened through a `--library` shortcut does not replace it.
  - If that library was moved, or its drive is not connected, the window says so and offers **Open library…** and **Open the default library**. It never opens an empty library in its place.
- **The window's title names the library**, so two windows can be told apart. Point at the folder in the toolbar to see its whole path (#33).
- **A library that cannot be opened** now shows the reason and a way out, instead of a window where every command is disabled. It may be open in another window, have been moved, or be a backup (#33).

## Fixed

- **Contents entries without a page when the printed page numbers skip pages** (#31).
  - Many publishers' PDFs leave out the blank pages of the printed book, so the printed numbers jump ahead at each chapter's end. Version 0.1.0 then gave most entries no page.
  - The app now uses the page numbering stored in the PDF (its *page labels*) to find them, and still confirms every page from what is printed on it. Nothing is guessed. On one 640-page book, 351 of 378 entries now have their page; before, none did.
  - Such a book is read a second time, which takes about as long again.
  - **Books added with 0.1.0 keep their contents as they were.** If most of a book's entries say "Page not found", select it and use **More → Analyze contents again**.
- **Long lists of possible pages hid the chapter titles** on the Contents tab (#31). An uncertain entry now shows at most three possible pages, or "Page uncertain (*n* possible)". Its details list them.
- **A backup could be opened as a library** (#32). Starting the app on a backup folder, for example with a `--library` shortcut pointed at the wrong folder, changed the backup so that it could no longer be restored. A backup folder is now refused and left unchanged, with the reason. To use a backup, restore it with **Backup → Restore a backup…**.

## Known limits

- **Search** covers titles, authors and contents entries, not the full text of the books.
- **OCR** of scanned books is slow (several seconds per page) and uses up to about 2.5 GB of memory.
- **Contents pages:** the fix above helps only PDFs that record their page numbering. Some books still get few pages for other reasons. Any entry's page can be set with **Change page…**.
- **Two windows:** the library opened last is the one remembered.
- **Windows "N" and "KN" editions** need the Media Feature Pack, or the app does not start ([PantaKoda/PDFMegine#7](https://github.com/PantaKoda/PDFMegine/issues/7)). Install it from *Optional features*: *Settings → System* on Windows 11, *Settings → Apps* on Windows 10.
- **Windows only** for now: macOS and Linux need native builds of the OCR library.
- **Third-party licences** are in `NOTICE.txt` and `licenses\` in the zip.
