# MyBooksLibrary: user guide

MyBooksLibrary keeps your PDF books in one place. For each book it finds the **title, authors, edition and year**, and the **table of contents** printed in the book. It then lets you **search** across all of them and **open a chapter at its page**.

This guide explains every part of the window, what happens in the background, and what the app cannot do (yet).

- [Getting started](#getting-started)
- [The window](#the-window)
- [Adding books](#adding-books)
- [What happens after you add a book](#what-happens-after-you-add-a-book)
- [A book's details](#a-books-details)
- [Searching](#searching)
- [Reading](#reading)
- [Collections and Trash](#collections-and-trash)
- [Saving a copy with bookmarks](#saving-a-copy-with-bookmarks)
- [Backing up and restoring](#backing-up-and-restoring)
- [Opening another library](#opening-another-library)
- [The Activity panel](#the-activity-panel)
- [Closing the app](#closing-the-app)
- [Limitations](#limitations)
- [Questions and problems](#questions-and-problems)

## Getting started

1. Unpack `MyBooksLibrary-X.Y.Z-win64.zip` anywhere, for example into `C:\Programs\MyBooksLibrary`.
2. Run **`appMyBooksLibrary.exe`**. There is no installer. The first time, Windows SmartScreen may warn that the app is from an unknown publisher (it is not code-signed yet): choose **More info → Run anyway**.
3. The first start creates your library.

**Where your books are kept.** Your library lives in your user data folder, apart from the app:

```text
%LOCALAPPDATA%\MyBooksLibrary\MyBooksLibrary\Library
```

It holds the library's own **copy** of each PDF and a catalog of everything the app found and everything you changed.
- **Your original PDF files are never changed, moved or deleted.** You can move or delete them after importing, and the library still works.
- **To upgrade** the app, replace only the folder you unpacked it into; your library stays where it is.

## The window

```text
┌──────────────────────────────────────────────────────────────────────────┐
│ Library  C:\…\Library   [ Search titles, authors and contents ] [All ▾]  │
│                                    [Library ▾] [Import PDFs…] [Backup ▾] │
├────────────────┬──────────────────────┬──────────────────────────────────┤
│ Library (12)   │ Book list            │ The selected book                │
│ Collections    │  Title               │  Title, file, pages              │
│   Networks (3) │  Authors · state     │  [Read] [More ▾]                 │
│ New collection…│                      │  ( Title and authors | Contents )│
│ Trash (1)      │                      │                                  │
├────────────────┴──────────────────────┴──────────────────────────────────┤
│ Library ready.           Processing idle.                       Activity │
└──────────────────────────────────────────────────────────────────────────┘
```

- **Toolbar:** the name of what the list shows and the library's folder (point at it to see the whole path); the **search** field and what to search in (**All**, **Titles**, **Authors**, **Contents**); the **Library** menu, to [open another library](#opening-another-library); **Import PDFs…**; and the **Backup** menu.
- **The window's title** names the library, so two windows on two libraries can be told apart.
- **Sidebar:** the **Library** (all books), your **collections**, and **Trash**. The numbers are book counts.
- **Book list:** each book's title and authors, and its processing state, such as "Analyzing contents…" or "Title uncertain".
- **Book details** (on the right): the selected book. See [A book's details](#a-books-details).
- **Status bar:** what the app is doing. **Activity** (bottom right) shows the processing jobs.

## Adding books

- Click **Import PDFs…** and choose one or many PDF files, or **drag PDF files** onto the window.
- **One at a time:** files are copied into the library one after another, with a progress bar and "*n* of *m*". **Cancel** stops the file being copied and drops the rest.
- **Each book appears in the list at once**, first named after its file.
- **Duplicates:** a file that is already in the library (the same PDF, even under another name) is not copied again. If the existing book is in Trash, a note offers to bring it back.
- **Files that could not be imported** (not a PDF, unreadable) are listed under "Files not imported", with the reason.

## What happens after you add a book

Every new book is **analyzed automatically in the background**. You can keep working meanwhile.

1. **Title and authors:** the first pages are read to find the title, subtitle, authors (with roles such as editor or translator), edition, publication year and copyright year.
2. **Contents:** the book's printed table of contents is found and read, and each entry is matched to the PDF page where that chapter starts.

**One book at a time.** Books are processed in the order they were added; the **Activity** panel shows which one and how many are waiting.
- A book with real text takes **seconds to about half a minute**.
- A **scanned** book, whose pages are pictures, needs OCR (text recognition). That takes **several seconds per page** and up to about **2.5 GB of memory**, so a batch of scanned books can take hours.
- **If you close the app**, unfinished work continues the next time you open it; nothing is lost.

**Nothing is guessed.** When the app is not sure (two possible titles, or a chapter whose page cannot be confirmed), it says so rather than invent an answer, and you can decide.

## A book's details

Select a book to see its details. Buttons at the top:
- **Read** opens the book where you last stopped.
- **More** opens a menu:
  - **Read title and authors again** and **Analyze contents again** run that analysis again. Your corrections are kept.
  - **Save a copy with bookmarks…** writes a new PDF (see [below](#saving-a-copy-with-bookmarks)).
  - **Add to collection**, **Remove from this collection**, **Move to Trash**.

### Title and authors

Each field shows its value and **where it came from**:
- **From the document:** found in the book.
- **Your correction:** your own value.
- **Cleared by you:** you chose to leave it empty.
- **Uncertain: several candidates, none chosen:** the book list then shows the file name as the title.
- **Not found in the pages searched:** nothing reliable was found.
- **Not read yet:** the analysis has not run yet.

**Why?** shows the evidence: the page and the text the value was found in, and the other candidates.

**Correct** changes a field:
- type your own value and **Save**; for authors, add, reorder (**Move up**, **Move down**) and remove people, each with a role;
- **Leave empty:** show the field as empty, even if the document has a value;
- **Use the document's value:** remove your correction.

Corrections are kept for good, even when the book is analyzed again, and search uses them at once. **The PDF itself is never changed.**

### Contents

The top shows a short summary, such as "221 contents entries, 204 with a confirmed page", and notes such as "No page found: 17 of 221". **Why? (*n*)** opens what the analysis reported about these contents.

Below is the **contents as a tree**: parts, chapters and sections, each with its page.
- **"Page 27"** is the page as the reader counts it: the first page of the PDF is page 1. It is not the number printed on the page, which may be "xv" or "1".
- **"Page not found"** means the entry is listed in the contents, but its page could not be confirmed. Such entries are kept and can still be searched.

Select an entry to see **where it points and why**, and to act on it:
- **Open chapter:** open the book at that page.
- **Show contents page *n*:** open the page of the printed table of contents where the entry is listed. This is useful when its page was not found.
- **Rename…**, **Change page…** (or **Set page…**), **No page**, **Indent** (make it a sub-entry of the entry above), **Outdent** (move it up one level), **Add after…**, **Remove** (removed entries are hidden from search; **Restore** brings one back).

**Your edits are kept** as your own version of the contents. If the book is analyzed again and the new result differs, a banner lets you choose: **Keep my edits** or **Use the new analysis**. **Discard my edits…** goes back to the analysis; earlier versions are kept.

## Searching

Type in the search field at the top. The results update as you type.
- **What is searched:** titles, authors, and the **titles of contents entries** (chapters and sections) of every book, including entries whose page was not found. **The full text of the books is not searched.**
- **In:** choose **All**, **Titles**, **Authors** or **Contents**.
- **Results** are grouped by book, with how each matched and the matching chapters:
  - **Open** opens the book at that chapter's page;
  - **Contents page** opens the printed contents where an entry without a confirmed page is listed;
  - **Show more** loads more results.
- **Tips:**
  - Every word you type must match a whole word; the order does not matter.
  - End a word with `*` to match its start: `netw*` finds "Networking".
  - Put words in double quotes to find them together: `"random numbers"`.
  - Terms such as `C++`, `C#`, `TCP/IP` and `HTTP/2` work as typed. Capitals and accents do not matter.
- **Esc** clears the search. **Ctrl+F** jumps to the search field.

## Reading

**Read**, double-clicking a book, **Open chapter** or a search result opens the built-in reader.
- **Previous** and **Next** turn the pages; you can also type a page number ("*n* of *m*").
- **Library** (top left) returns to the list.
- **The app remembers where you stopped** in each book, even after closing it.

## Collections and Trash

**Collections** group books, for example "Networks" or "To read". A book can be in several collections; it is still one book and one file.
- **New collection…** (in the sidebar) creates one.
- **More → Add to collection** puts the selected book in one; **More → Remove from this collection** takes it out.
- **Rename** or **Delete** a collection by right-clicking it (or with the Menu key). **F2** renames; **Delete** asks, then deletes it. Deleting a collection keeps its books.

**Trash** hides a book from the library and from search, and stops its processing.
- **More → Move to Trash** moves the selected book there.
- Open **Trash** in the sidebar, select the book and choose **Restore** to bring it back, with its corrections and contents. Work that was stopped by the trash starts again.
- Books in Trash are kept; there is no way to delete them for good yet.

## Saving a copy with bookmarks

**More → Save a copy with bookmarks…** writes a **new PDF** whose bookmarks (the side panel in any PDF reader) are the book's contents, including your edits. **The book in your library is not changed.**

The dialog shows first **what the copy gets**:
- how many bookmarks;
- every contents entry that is **left out** (no confirmed page) or **placed at another level**, with the reason.

Choose where to save it (**Choose…**; Documents is suggested) and click **Save copy**.
- **Replacing a file:** if a file with that name exists, you are asked whether to **Replace** it.
- **Not allowed:** the copy cannot be saved inside the library folder, or over one of your original PDFs.
- **Waiting:** while another book is being analyzed, the copy waits for it ("Waiting for the current work to finish…"). You can close the dialog; the copy is still saved.
- When it is done, **Show folder** opens where it was saved.

## Backing up and restoring

**Backup → Back up the library…** saves a complete copy of your library: the catalog (titles, corrections, contents, collections, reading positions) and every book's PDF.
- It goes into a new folder, "MyBooksLibrary backup *date time*", in the folder you choose (Documents is suggested; it cannot be inside the library).
- **You can keep working meanwhile.** The backup is **checked** before it is given its name, so a cancelled or failed backup leaves nothing behind.

**Backup → Restore a backup…** makes a **new library** from a backup.
- Choose the backup folder, and where to put the restored library (a new folder).
- The backup is checked first; a damaged or changed backup is refused.
- **Your current library is never changed.** A restore never goes inside or over it.
- **Open restored library** opens it in a new window.

**Opening a restored library later:** **Library → Open library…**, and choose its folder ("MyBooksLibrary restored …"). See [Opening another library](#opening-another-library).
- Use the **restored** folder, not the backup ("MyBooksLibrary backup …"). The app refuses to open a backup as a library, because that would change the backup and it could no longer be restored.
- A copy with bookmarks that was still waiting when the backup was made is not written again; ask again if you want it.

Keep backups on another disk, or in cloud storage, to be safe from a disk failure.

## Opening another library

**Library → Open library…** opens another MyBooksLibrary library, for example one you restored from a backup.
1. Choose the library's folder: the one that holds `library.sqlite`, such as "MyBooksLibrary restored 2026-09-29".
2. Choose **New window**, to keep this library open beside it, or **Instead of this library**, which closes this window once its work has stopped (as when you close it yourself).

**What is refused, with the reason:**
- a folder that is not a library, such as Documents, an empty folder, or a library's own `files` folder. The app never creates a library in a folder you choose;
- a **backup** ("MyBooksLibrary backup …"): restore it first;
- the library already open in this window.

**Good to know:**
- A library can be open in only **one window** at a time; a second window on it says it is already open.
- **Library → Open the default library** opens your usual library, in `%LOCALAPPDATA%\MyBooksLibrary\MyBooksLibrary\Library`.
- **The next time you start the app, it opens the library you last opened from the app:** with **Open library…**, **Open the default library** or **Open restored library**. It is remembered once it has opened, so a folder that failed to open is never remembered.
  - A library opened through a shortcut with `--library` is **not** remembered, so a shortcut does not change your everyday library.
  - With two windows open, the library opened last is the one remembered.
  - If the remembered library cannot be opened any more (it was moved, or its drive is not connected), the window says so, with **Open library…** and **Open the default library**. The app never makes a new, empty library in its place.
- **To make a restored library your everyday one,** open it once with **Library → Open library…** (or **Open restored library**); the next starts open it.
- **To keep a shortcut to one particular library** without changing your everyday one, make a shortcut to `appMyBooksLibrary.exe`, and add `--library` and the folder in quotes at the end of the shortcut's *Target*, for example:

  ```text
  "C:\Programs\MyBooksLibrary\appMyBooksLibrary.exe" --library "D:\MyBooksLibrary restored 2026-09-29"
  ```

## The Activity panel

**Activity** (bottom right) shows the processing jobs: title and authors, contents, and copies with bookmarks.
- Each shows its book and state, such as "Waiting", "Analyzing the contents… · 12 pages read", "Done" or "Failed", and why.
- **Cancel** stops one job; **Cancel all** stops every waiting and running job.
- **Retry** runs a failed or cancelled analysis again.
- The status bar summarizes it, for example "Analyzing contents: *title* · 2 books waiting".

## Closing the app

You can close the app at any time.
- **If something is running**, the window shows "Finishing before closing…" and closes once it has stopped cleanly. An OCR page in progress is finished first, which can take a few seconds.
- **Unfinished analyses** continue the next time you open the app.
- **A backup or restore in progress** is cancelled, and leaves nothing half-made.

## Limitations

- **Search does not cover the full text of the books**, only titles, authors and contents entries.
- **Books are processed one at a time.** Scanned books are slow (OCR, several seconds per page) and use up to about 2.5 GB of memory. You cannot yet choose which book goes next.
- **Automatic results can be incomplete or uncertain.** Some titles have several candidates; some contents entries get no confirmed page; an unusual table of contents may not be found at all. The app shows this rather than guess, and you can correct everything.
- **No permanent deletion:** books in Trash stay in the library (and take disk space).
- **One library per window.** Another library opens in its own window ([Opening another library](#opening-another-library)), and the app starts with the library you last opened from the app.
- **Windows 10 and 11 (64-bit) only.** macOS and Linux need native builds of the text-recognition library.
- **Windows "N" and "KN" editions** need the **Media Feature Pack**; without it the app does not start. Install it from Windows' *Optional features*: on Windows 11, *Settings → System → Optional features*; on Windows 10, *Settings → Apps → Optional features*.
- **Not code-signed**, so SmartScreen may warn the first time.
- **The reader is simple:** previous and next page, and a page number. It has **no zoom** and **no search inside a book**; for those, open the PDF in another reader.
- **No cloud sync**, no online lookup of book data, and no annotations.

## Questions and problems

**A book seems to have no contents.**
- Open its **Contents** tab and scroll down to the tree.
- If it really has none, the summary says so. The book may have no printed table of contents, or one the analysis could not read.
- You can **Analyze contents again** (More menu), or add entries yourself with **Add after…**.

**The title shows the file name.** The analysis found several possible titles and did not choose one. Use **Correct** on the **Title and authors** tab; **Why?** shows the candidates.

**A chapter opens at the wrong page.** Select the entry in **Contents** and use **Change page…**. Type the page as the reader shows it: 1 is the first page of the PDF.

**Processing seems stuck.** Look at **Activity**: scanned books take a long time, and the page counter shows progress. **Cancel** and **Retry** are there if needed.

**The app does not start.** On a Windows "N" edition, install the Media Feature Pack (Windows 11: *Settings → System → Optional features*; Windows 10: *Settings → Apps → Optional features*). Otherwise, check that the whole unpacked folder is there: the executable needs the files next to it.

**A library is gone after restarting the app.** It is not gone: the app opened the library you last opened *from the app*. A library opened through a `--library` shortcut is not remembered. Use **Library → Open library…** and choose its folder; from then on, it opens at each start.

**"This library could not be opened."** The window says why, with **Open library…** and **Open the default library** next to the reason. Common reasons:
- the library is **already open in another window**: switch to that window;
- its folder was **moved**, or its drive is **not connected**: connect it, or choose its new folder with **Open library…**;
- it is a **backup** (see below).

**"… is a MyBooksLibrary backup, not a library."** The folder you chose, or the one the app was started with (for example in a shortcut with `--library`), is a backup. A backup is never opened directly. Restore it with **Backup → Restore a backup…**, and use the restored folder instead.

**Where are the licences?** `NOTICE.txt` and the `licenses` folder, next to the app.
