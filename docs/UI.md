# Library window (presentation)

## Structure

| Piece | Where | Role |
| --- | --- | --- |
| Composition root | `main.cpp`, `src/app/libraryroot.*` | Resolves the library folder, creates the `LibraryController`, gives it the SDK metadata extractor (`sdk::SdkMetadataExtractor`), contents analyzer and bookmark writer (`sdk::SdkBookExporter`) and whether OCR models were found, passes it to `Main.qml` as the required property `library`, and opens the library. |
| `LibraryController` | `src/presentation/librarycontroller.*` | The library session. It opens the library (lock, migrations, `ImportService::recover`) and imports files on a **one-thread worker pool**. It delivers progress (throttled to about 10 updates a second) and per-file results through queued invocations, and reloads the book list through `QFuture::then(this, …)`, so models change only on the GUI thread. |
| `LibraryController` and processing | same | Once the library is open, it creates the `ProcessingCoordinator` (docs/PROCESSING.md), runs job recovery, and then starts it. Coordinator signals update the models on the GUI thread: `jobChanged` updates one row, and `metadataPublished` reloads the book list. `refresh()` loads the books, each book's latest job (`catalog::latestJobs`, one window-function pass), the 100 most recent jobs and **every open job** in one database task. Refreshes are coalesced: one runs at a time, and calls made meanwhile schedule a single further one. |
| `BookListModel` | `src/presentation/booklistmodel.*` | GUI-owned list of copied `BookSummary` values. Roles: `bookId`, `title`, `titleFromFileName`, `contributors`, `processingState`. The processing state combines what the catalog holds with the book's latest metadata job. |
| `JobListModel` | `src/presentation/joblistmodel.*` | GUI-owned activity list, newest first, capped at 200 finished rows. Roles: `jobId`, `bookId`, `bookTitle`, `kindText`, `state`, `stateText`, `detail`, `running`, `canCancel`, `canRetry`. Properties: `pendingCount` and a one-line `summary`. Updates older than the row shown (by `updatedAt`) are ignored, so a late snapshot cannot undo a newer state. |
| `BookInspector` | `src/presentation/bookinspector.*` | The selected book, loaded on the database thread: effective metadata with where each value came from, the evidence and candidates behind it (at most five candidates, then a count), and the contents summary and notes. Loads are tagged, so only the newest selection or reload is applied. It reloads after each book-list refresh, and rebuilds the tree only when the contents changed (a new run, or an edited revision saved, kept or discarded), so expanded branches and the current entry survive other updates. It carries the metadata corrections (M07 part 1) and the contents edits (part 2b), each checked before saving; a contents edit also carries the run and revision on screen, so a stale one is refused and the contents are reloaded. |
| `TocTreeModel` | `src/presentation/toctreemodel.*` | The **catalog's** contents as a tree (never the PDF's own bookmarks). It is built in two passes, so a parent may be listed after its child. A missing parent, a self-reference or a cycle keeps the entry at the top level with the note "parent not found"; an entry below a cycle member stays attached; Unknown hierarchy stays at the top level. Roles: title, page text, physical page (index + 1), source page, printed label, state text, uncertain, in plan, plain-language details, technical details, removed, and what the user changed. A page or level the user set replaces the analysis's reasons for it. `indexOfEntry(key)` finds an entry again after a rebuild. |
| `BookInspectorPane.qml` | `qml/inspector/` | The inspector: title and file, then two tabs. **Title and authors** shows each field's value and source, with "Why?" revealing its evidence and candidates. **Contents** shows the summary, the notes, a banner for edited contents, the tree, and the selected entry's reasons and edit actions. Corrections and edits open dialogs; all changes go through `BookInspector`. |
| `SearchController`, `SearchResultsModel` | `src/presentation/search*` | The search field: debounced, newest request wins, paginated, re-run after catalog changes. See SEARCH.md, "In the application". |
| `SearchResultsView.qml` | `qml/search/` | Results: book, how it matched, its processing state, and chapter hits with the physical page, or "Page not found" and where it is listed. Choosing a result shows the book in the inspector. |
| `ReaderController` | `src/reader/readercontroller.*` | The reading session: the open book, the document, the requested and shown pages (physical indices), and the saved reading position. It owns document lifetime; see READER.md, "The embedded reader". |
| `ReaderPane.qml` | `qml/reader/` | The reader: "Library" (back), title, previous and next page, an editable page number "of *n*", and the PDF. |
| `CollectionListModel` | `src/presentation/collectionlistmodel.*` | The collections for the sidebar, by name, with active book counts. Roles: `collectionId`, `name`, `bookCount`. Unchanged rows keep their state across refreshes. |
| `LibrarySidebar.qml` | `qml/library/` | The views: **Library (*n*)**, each collection (*n*), and **Trash (*n*)**; **New collection…**, and Rename… / Delete… on a collection (right-click, press and hold, the Menu key or Shift+F10; F2 renames, Delete asks to delete). Names are plain text. Refusals (a duplicate name) are shown under it. |
| `ExportController` | `src/presentation/exportcontroller.*` | The Export dialog's session (M09). `prepare(book)` loads on the database thread what the copy will get: the number of bookmarks, a summary, one note per entry left out or moved with the reason, a suggested "*title* (bookmarked).pdf" in Documents, and the book's last export. `exportTo(path)` asks the `ProcessingCoordinator` for the copy and follows its job: Waiting (behind an analysis), Writing, Cancelling, then Saved, NotSaved or Cancelled. A file already at the destination moves to **NeedsReplace**; only `confirmReplace()` replaces it. A refusal shows the coordinator's reason (inside the library folder, a protected file, a book in Trash). |
| `ExportDialog.qml` | `qml/export/` | "Save a copy with bookmarks", from the inspector's **More** menu. It shows the preview, a "Save as" field with **Choose…** (a native save dialog whose own overwrite prompt is off, so replacing is always confirmed here), the progress and the result, **Show folder** once saved, and **Cancel saving**. Closing it does not stop a copy being saved; the job queue shows it. |
| `BookListView.qml` | `qml/library/` | The book list of the current view. The selection is kept by book ID across every row change (not only resets), so the list and the inspector always show the same book, or none. |
| `Main.qml` | repository root | The list-first window. QML only reads properties and calls `importUrls`, `cancelImports`, `refresh`, `cancelJob`, `retryJob`, `cancelAllJobs` and `prepareToClose`. There is no SQL, file or SDK work in QML. |

`mbl_presentation` is a static QML module (`MyBooksLibrary.Presentation`), so `Main.qml` uses typed `LibraryController`/`BookListModel` and `qmllint` checks its member accesses. Both types are uncreatable from QML.

## Behaviour

- **Opening:** "Opening the library…" with a busy indicator. The result of recovering interrupted imports is shown in the status bar ("1 interrupted import completed"). If the library cannot be opened (for example it is already open in another instance), the reason is shown.
- **Import:** use "Import PDFs…" (a native file dialog for multiple PDFs) or drop files on the window.
  - Files queue and import one at a time off the GUI thread, with "n of m", a per-file progress bar and Cancel.
  - The batch summary ("2 imported, 1 already in the library.") and per-file problems (duplicates, duplicates in Trash, non-PDFs, failures) are listed under it.
  - File-dialog URLs are converted with `toLocalFile()`.
- **Processing:** every imported book gets a metadata job and a contents job, queued in the import transaction. Jobs run one at a time and start once recovery has run; a book's two jobs share one SDK call (PROCESSING.md), and its title appears before its contents analysis ends.
  - The footer shows "Reading title and authors: *book*" or "Analyzing contents: *book*", with "*n* pages read" when the SDK reports it, and "*n* books waiting", with an indeterminate busy indicator. The SDK reports no totals, so no percentage is shown.
  - **Activity** opens a panel listing each job's book, task and state ("Waiting", "Reading the first pages…", "Done", "Failed: …", "Cancelled", "Interrupted when the application closed; queued again"). It has **Cancel** for waiting and running jobs, **Retry** for failed or cancelled jobs that have no newer job, and **Cancel all**.
  - When OCR models were not found, the panel says that title pages that are scanned images cannot be read.
- **Book list:**
  - It shows the effective title, or the file name with "From the file name", and the processing state:
    - "Waiting to read title and authors" and "Reading title and authors…";
    - "Metadata ready";
    - "Title uncertain: several candidates" (ambiguous; never accepted automatically);
    - "No title found in the document";
    - "Title and authors could not be read" and "Metadata extraction cancelled";
    followed by the contents state: "contents waiting", "analyzing contents…", "*n* contents entries", "no printed contents found", "contents could not be analyzed", "contents analysis cancelled" or "contents not analyzed".
    A published result stays shown while a rerun waits, runs or fails.
  - Extracted text is rendered as plain text.
  - The selection follows the book ID across refreshes.
  - Empty state: "No books yet. Choose "Import PDFs…" or drop PDF files here."
- **Search:** a search field and a scope (All, Titles, Authors, Contents) are always in the toolbar; Ctrl+F focuses the field and Esc clears it.
  - While a query is entered, the results replace the book list; clearing it shows the book list and its selection again.
  - Search covers titles, authors and contents entries, not the full text; the field's accessible description says so.
- **Inspector:** selecting a book shows it beside the list.
  - Metadata values say where they came from: "From the document", "Your correction", "Cleared by you", "Uncertain: several candidates, none chosen" or "Not found in the pages searched".
  - The contents tab summarizes coverage ("5 contents entries, every page confirmed."), lists what is uncertain ("No page found: 1 of 5."), and says whether a bookmarked copy could be made, with the plan's blockers.
  - Tree rows show the physical page ("Page 4", although it is printed "1"), "Page 5 or 10?" for ambiguous entries, or "Page not found". Nothing is guessed.
  - The selected entry's reasons are in plain language ("Listed on page 3 of the PDF", "Left out of the bookmarks: …"), with technical details on request.
  - The selected entry offers **Open chapter** (its physical page) and **Show contents page *n*** (the page where the contents list it). They are two different actions; an unresolved entry offers only the second.
  - **Correcting metadata (M07):** each field has **Correct**. The dialog offers Save, **Leave empty** (stays empty even if the document has a value) and **Use the document's value**. A corrected field shows "The document says: …". The **More** menu reads the title and authors or the contents again; corrections stay.
  - **Editing contents (M07):** the selected entry offers:
    - **Rename…** and **Set page…** or **Change page…** (the page as shown in the reader);
    - **No page**;
    - **Indent** (under the entry above it at its level) and **Outdent**;
    - **Add after…**;
    - **Remove**, which removes the entry and its sub-entries (kept, struck through, not searched), and **Restore**.

    Each save is a new version of the contents, and the edited entry stays selected. The entry says what you changed ("Changed by you: title, page"), and a page you set replaces the analysis's reasons.
  - **Edited contents** show a banner: "You edited these contents (version *n*). Search uses your version." with **Discard my edits…** (after confirming, the analysis is shown again).
  - **When a newer analysis finds different contents,** the edits stay shown and searched, and the banner offers **Keep my edits** or **Use the new analysis**. Nothing is reconciled automatically. The book list says "(edited; a new analysis to review)".
- **Organizing (M08):**
  - The sidebar switches the book list between the library, one collection and Trash, and the toolbar heading names the view.
  - Search covers the shown collection, or the whole library otherwise.
  - The inspector's **More** menu has **Add to collection ▸**, **Remove from "*collection*"** (in a collection view) and **Move to Trash**.
  - A book in Trash shows "In Trash" and **Restore**, and its list row says "In Trash since *date*".
  - **Moving a book to Trash** stops its running extraction at once and hides it from the library, its collections and search. **Restoring** it resumes the work the trash stopped, right away.
  - **Importing a file whose book is in Trash** offers **Restore *n* book(s) from Trash** under the import summary.
  - Collections never copy files. Deleting a collection, after confirming, keeps its books.
- **Reading:** the reader replaces the library view while a book is open, and "Library" returns.
  - A book opens where it was last read: double-click it in the list, or use **Read** in the inspector.
  - A chapter opens at its physical page ("Open chapter" in the inspector; "Open" on a search hit).
  - An entry without a confirmed page opens the page where the contents list it ("Show contents page", or "Contents page" on a search hit). No page is guessed.
  - The position is saved per book and survives a restart.
- **Saving a copy with bookmarks (M09):** **More → Save a copy with bookmarks…** opens the Export dialog.
  - Before anything is written, it says how many bookmarks the copy gets, and lists every contents entry left out (no confirmed page) or placed at another level, with the reason. The book in the library is never changed.
  - **Save copy** writes a new PDF in the background: "Waiting for the current work to finish…" while an analysis runs, then "Writing the bookmarked copy…", then "Saved as …, with *n* bookmarks".
  - An existing file is replaced only after **Replace**, and only if it is still the same file when the copy is written (PROCESSING.md, "Export jobs"). The library folder, the book's own PDF and imported originals are refused with the reason.
  - A book in Trash, or one whose contents are not analyzed yet, shows why it cannot be exported.
- **Closing:** closing while work runs calls `prepareToClose()`, which cancels imports and **stops** processing. Stopping is not cancelling: the running extraction is interrupted and requeued, and waiting jobs stay queued, so the next start continues them. The window shows "Finishing before closing…" and closes when the controller is idle. There is no blocking wait on the GUI thread. The SDK stops at its next checkpoint, so an OCR page in progress finishes first: closing took 6.8 s during OCR of `image-only.pdf` in Release.

## Not yet

A list of all saved copies of a book (the dialog shows the last one), comparing a newer analysis with the edited contents entry by entry, browsing earlier versions of the contents, emptying Trash (permanent deletion), selecting several books at once, and keyboard shortcuts beyond list navigation.

## Screenshots (Release build, `--screenshot`, library at `C:\MBL-demo\Library`)

Empty library:

![Empty library](images/m03-empty-library.png)

After importing two PDFs and a duplicate:

![Imported books](images/m03-imported-books.png)

After importing the three test PDFs with the real SDK and OCR models (`--activity` shows the panel). "Title uncertain" means the SDK reported several candidates:

![Metadata extracted](images/m04-metadata-extracted.png)

After M05 part 1, with the same three test PDFs: each book shows its metadata state and its contents state, and each has a "Title and authors" and a "Contents" job, served by one SDK call:

![Contents analyzed](images/m05-contents-analyzed.png)

The inspector's Contents tab after importing `contents-book.pdf` (Release, real SDK, `--inspect-first`):

![Inspector contents](images/m05-inspector-contents.png)

The Contents tab with an entry selected: its reasons, Open chapter and Show contents page, and the edit actions. Indent and Outdent are unavailable for the first top-level entry (Release, real SDK, `--inspect-first`, library at `C:\MBL-demo-Contents`):

![Contents editing](images/m07-contents-editing.png)

The window with the sidebar (Release, real SDK, `--inspect-first`, library at `C:\MBL-demo-Contents`):

![Library sidebar](images/m08-library-sidebar.png)

The Export dialog for `contents-book.pdf` imported as "Βιβλίο", with an example destination (`tst_exportcontroller`, real SDK, `MBL_SCREENSHOT_DIR`):

![Export dialog](images/m09-export-dialog.png)

Searching "tcp/ip" after importing the three test PDFs: a contents-only match, with its physical page (Release, real SDK, `--search tcp/ip --inspect-first`):

![Search results](images/m06-search-results.png)

The reader after "Open chapter" on "3 Networking with TCP/IP": physical page 15, whose printed page number is 12 (Release, `--read-page 15`):

![Reader at a chapter](images/m06-reader-chapter.png)
