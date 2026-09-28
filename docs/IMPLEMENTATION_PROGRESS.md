# Implementation progress

One active milestone at a time. Status values: **NotStarted**, **InProgress**, **LocallyVerified**, **AwaitingReview**, **Merged**, **Blocked**.

| Step | Status | Branch / PR | Notes |
| --- | --- | --- | --- |
| M00 Baseline | Merged | `feat/m00-baseline` / [PR #1](https://github.com/PantaKoda/MyBooksLibrary/pull/1), merge `46de94a` | See below |
| M01 Feasibility | Merged: part 1 [PR #2](https://github.com/PantaKoda/MyBooksLibrary/pull/2) (merge `f126ad0`); part 2 [PR #4](https://github.com/PantaKoda/MyBooksLibrary/pull/4) (merge `e9d8be2`) | `feat/m01-a3-fts5-probe`, `feat/m01-reader-qtpdf-coexistence` | Qt PDF coexists; OCR memory-pressure and Qt Quick teardown risks tracked in READER.md |
| M02 Contracts/persistence | Merged | `feat/m02-a2-catalog-persistence` / [PR #3](https://github.com/PantaKoda/MyBooksLibrary/pull/3), merge `b643446` | See below |
| SDK 0.2.0 update | Merged | `chore/m01-sdk-0.2.0` / [PR #5](https://github.com/PantaKoda/MyBooksLibrary/pull/5), merge `4f87975` | See "SDK 0.2.0 update" |
| M03 Import/library shell | Merged: part 1 [PR #6](https://github.com/PantaKoda/MyBooksLibrary/pull/6) (merge `515ff43`); part 2 [PR #7](https://github.com/PantaKoda/MyBooksLibrary/pull/7) (merge `f39b141`) | `feat/m03-a1-managed-import`; `feat/m03-presentation-library-shell` | See "M03" |
| M04 Metadata jobs | Merged: part 1 [PR #9](https://github.com/PantaKoda/MyBooksLibrary/pull/9) (merge `6b5d740`); part 2 [PR #10](https://github.com/PantaKoda/MyBooksLibrary/pull/10) (merge `578e951`) | `feat/m04-a4-metadata-jobs`; `feat/m04-presentation-metadata-jobs` | See "M04" |
| SDK 0.3.0 update | Merged | `chore/sdk-0.3.0` / [PR #11](https://github.com/PantaKoda/MyBooksLibrary/pull/11), merge `a4b7d59` | See "SDK 0.3.0 update" |
| M05 Contents | Merged: part 1 [PR #12](https://github.com/PantaKoda/MyBooksLibrary/pull/12) (merge `9feb9b3`); part 2 [PR #13](https://github.com/PantaKoda/MyBooksLibrary/pull/13) (merge `8ba9f49`) | `feat/m05-a4-contents-analysis`; `feat/m05-presentation-contents-inspector` | See "M05" |
| M06 Search/read | Merged: part 1 [PR #14](https://github.com/PantaKoda/MyBooksLibrary/pull/14) (merge `c8dc23e`); part 2 [PR #15](https://github.com/PantaKoda/MyBooksLibrary/pull/15) (merge `e964184`) | `feat/m06-presentation-search`; `feat/m06-reader-chapter-navigation` | See "M06" |
| M07 Corrections/reruns | Part 1 Merged ([PR #16](https://github.com/PantaKoda/MyBooksLibrary/pull/16), merge `152eb8c`); part 2a AwaitingReview ([PR #17](https://github.com/PantaKoda/MyBooksLibrary/pull/17), contents edits in catalog and search); part 2b (editing UI) NotStarted | `feat/m07-presentation-metadata-corrections`; `feat/m07-a2-toc-edits` | See "M07" |
| M08–M11 | NotStarted | | |

## M07 — Corrections and reruns

M07 is split in three:
- **Part 1** (merged): metadata corrections in the inspector, and reruns started by the user.
- **Part 2a** (A2 + A3): contents edits in the catalog and search.
- **Part 2b** (presentation): editing the contents and reconciling in the inspector.

### Part 2a: contents edits in the catalog and search (A2 + A3)

**Scope:**
- **Schema 6:** `toc_edit_revisions` and `toc_edit_entries`, and `books.active_toc_revision_id`.
- **`catalog/tocedits.*`:** `editToc`, `keepTocEdits`, `useAnalyzedToc` and `tocRevisions`.
- **Publishing a new run:** `publishToc` carries edits to a new run whose entries are the same in content; otherwise the book needs reconciliation.
- **`bookDetails` and the search projection:** they use the effective contents. Removed entries are not searched and not counted.
- **Domain:** `TocRevisionId`, `TocRevisionInfo`, `TocEditBase` and `TocEdit`; `TocEntry::edits` and `removed`; `BookSummary::tocEdited` and `tocNeedsReconciliation`; `BookDetails::tocRevision` and `analyzedToc`.
- CATALOG.md documents the rules.

**Touched paths:** `src/domain/`, `src/catalog/`, `src/search/searchindex.h`, `CMakeLists.txt`, `tests/catalog/`, `tests/CMakeLists.txt`, `docs/`.

| Command | Result |
| --- | --- |
| `pwsh scripts/verify.ps1` (Release) and `-Configuration Debug` | VERIFY PASSED in both; 19/19 `ctest` suites; all smoke checks |
| `tst_tocedits` (new, 8 cases) | **Edits:** rename, page 0, set parent, clear page, add, remove and restore. Each save is a new numbered revision, earlier ones are kept, and the analysis is unchanged (`analyzedToc`). Search finds the corrected title and not the OCR noise. Everything survives a restart.<br>**Refused, nothing saved:** a stale base; an empty title; pages 50 and -1 in a 50-page book; an unknown entry; a cycle; a parent that is the entry itself; an unknown parent; no edits; a book without contents; a trashed book.<br>**Hierarchy:** removing a parent listed after its child removes both from search, and the child cannot be restored alone. Moving to a parent listed later works.<br>**Identical rerun (new SDK IDs):** the edits carry over and later edits apply.<br>**Changed rerun:** the edits stay in effect and searchable, and the book needs reconciliation. Editing still works; keep, then discard, both work, and the revisions stay.<br>**During analysis:** an edit made while a rerun runs survives; a superseded run is refused; the current identical run carries the edit.<br>**Rebuild:** `rebuildSearchIndex` matches the edited contents. |
| `tst_migrations::version5CatalogGainsEditedContents` (new) | A schema 5 catalog with a published run upgrades; the book shows the analyzed contents and can be edited |
| Mutations, each reverted | Carrying edits over to a changed rerun: `changedRerunWaitsForTheUser` fails. No stale-base check: `staleOrInvalidEditsChangeNothing` fails. |

**Not in this part:** the inspector UI for editing and reconciling (part 2b), and export plans from edited contents (M09).

**Next action:** review of [PR #17](https://github.com/PantaKoda/MyBooksLibrary/pull/17). After it is merged: M07 part 2b, the editing and reconciliation UI.

### Part 1: metadata corrections and reruns (presentation + A2)

**Already in place from M02–M05, not changed here:**
- `catalog::setOverride`: Auto, Value or Cleared per field, with the search projection updated in the same transaction.
- Effective values computed from the active run and the current override.
- Publication guarded by generation tickets.
- Coordinator tests: `correctionsMadeWhileRunningSurvive` and `supersededResultIsNotPublished`.
- Catalog test: `clearedDoesNotFallBackAfterRerun`.

**Scope:**
- **`BookInspector`:**
  - `setText`, `setYear`, `setContributors`, `clearField` and `useDocumentValue`. Each names its book explicitly and is checked before saving; a refused value sets `correctionError`.
  - `saving`, and `corrected(bookId)`, which the `LibraryController` turns into a refresh of the book list, inspector and search.
  - Title and subtitle are now separate fields.
  - Each field map carries `field`, `kind`, `mode`, `documentValue`, `editText` and `editContributors`.
- **`LibraryController`:** `rerunMetadata(bookId)` and `rerunContents(bookId)`, which enqueue with a new generation.
- **`MetadataFieldEditor.qml`** (new), in a dialog in `BookInspectorPane.qml`:
  - text or year, or ordered contributors with a role, add, move and remove;
  - Save, Cancel, Leave empty, and Use the document's value.
  - The pane shows "The document says: …" beside a correction, and a "More" menu with "Read title and authors again" and "Analyze contents again".
- **Catalog:** override years must be between 1 and 9999.
- **Development option:** `--correct <field>`.

**Touched paths:** `src/presentation/bookinspector.*`, `src/presentation/librarycontroller.*`, `src/catalog/catalog.cpp`, `qml/inspector/`, `Main.qml`, `main.cpp`, `CMakeLists.txt`, `tests/presentation/`, `tests/catalog/tst_catalog.cpp`, `docs/`.

| Command | Result |
| --- | --- |
| `pwsh scripts/verify.ps1` (Release) and `-Configuration Debug` | VERIFY PASSED in both; 18/18 `ctest` suites; all smoke checks, including "close while reading" |
| `tst_librarycontroller::correctionsFromTheInspector` (new) | **Value:** a title is trimmed and shown as "Your correction", with the document's value beside it; the book list and a Titles search follow it, and the old title no longer matches.<br>**Leave empty:** the edition shows "—" and "Cleared by you", with no fallback.<br>**Contributors:** kept in order with their roles; an empty row is ignored.<br>**Refused before saving:** `abc`, `0`, `10000` and an empty year; a blank subtitle; text for a year field; no names; an unknown role.<br>**Back to Auto:** the subtitle returns to the document's value.<br>**Restart:** everything is kept. |
| `tst_librarycontroller::correctionsSurviveARerun` (new) | While "Read title and authors again" runs, the earlier results stay shown and a title correction is saved. After it publishes:<br>- the correction and the earlier Cleared edition stay;<br>- the document's new title and edition are shown beside them;<br>- Auto then shows the new reading.<br>"Analyze contents again" leaves the metadata corrections alone. |
| `tst_inspectorpane::correctionDialogSavesAndSurvivesRefreshes` (new, real QML) | Text typed in the dialog survives an inspector refresh (same editor object). Save stores the value for the book the dialog was opened for, even after another book was selected. Contributor names survive Add and Move up, including a name typed after the last reorder. A refused year keeps the dialog open with the reason. Leave empty works from the dialog, and reopening starts without the old error. |
| `tst_catalog::metadataPublicationAndOverrides` | Years 0, -5 and 10000 are refused (InvalidArgument) |
| Mutations, each reverted | No refresh after a correction: `correctionsFromTheInspector` fails (the book list keeps the old title). Saving contributors from the rows as last restructured, not as typed: `correctionDialogSavesAndSurvivesRefreshes` fails. |
| `all_qmllint` | No warnings |
| `appMyBooksLibrary --import title-page.pdf --inspect-first --correct contributors --screenshot docs/images/m07-correct-contributors.png` (Release, real SDK) | Exit 0, no QML warnings. The contributors editor opens over the inspector with one empty row, a role, Add a person, Save, Cancel and Leave empty |

**Review fixes (PR #16 review of `a58d0b8`):**
1. *Minor:* with about 18 or more contributors, the dialog grew past the window and Save, Cancel and Leave empty went off-screen. The rows are now in a `ScrollView` capped at the window height minus about 260 px. They stay a `Repeater` so every typed name can be read back. Adding a person scrolls to the new row and focuses it.
2. *Nit:* the rerun comment in `librarycontroller.h` now says that a rerun returns a job already waiting or running instead of starting a new generation.

| Command | Result |
| --- | --- |
| `tst_inspectorpane::correctionDialogSavesAndSurvivesRefreshes`, extended | 25 people added in a 640 px window: once the rows are laid out, Save and Add a person are inside the window, and the newest row is scrolled into view with focus. A name typed in it is saved, and empty rows are ignored. **Against the unfixed editor it fails:** Save at y 824. The first version of this check passed on the unfixed editor because it measured before layout; it now waits for the rows to be laid out. |

**Not verified by hand:** keyboard-only use of the dialog and screen readers.

**Next action:** M07 part 2, TOC edits.

## M06 — Search and reading

### Part 2: embedded reader, chapter navigation, reading position (reader + A2)

**Scope:**
- `ReaderController` and `ReaderPane.qml`, owning document lifetime;
- schema 5 with `catalog/reading.*`;
- the entry points in the inspector, search results and book list;
- `--read-page`.

**Touched paths:** `src/reader/`, `src/catalog/reading.*`, `src/catalog/migrations.cpp`, `src/presentation/librarycontroller.*`, `qml/reader/`, `qml/inspector/`, `qml/search/`, `Main.qml`, `main.cpp`, `CMakeLists.txt`, `tests/`, `docs/`.

| Command | Result |
| --- | --- |
| `pwsh scripts/verify.ps1 -Clean` (Release) and `-Configuration Debug` | VERIFY PASSED in both; 18/18 `ctest` suites |
| `tst_readercontroller` (new, 5 cases) | Covers:<br>- opens where last read (first page if never read); paging is saved after one second;<br>- switching books saves at once; the same book with another page just moves;<br>- **the document never changes while the view exists** (originally confirmed with `viewReleased()`; since the review fixes, by the view's destruction), and close waits for the view, then clears the document; reopening resumes;<br>- a newer open or a close drops a load in flight;<br>- pages are clamped, and an unknown book gives a message. |
| `tst_readerpane` (new, real Qt PDF, offscreen) | `contents-book.pdf` opens at physical page 15, "3 Networking with TCP/IP" (printed 12), **checked by the real scroll position**; moving to pages 4 and 1 (index 0) works. Opening while the pane has no size, then laying it out, still reaches page 15. 25 rounds of switching books, with close and reopen every fifth, end with each book's position saved and resumed. It passed **20 times** in Debug and **20** in Release (`--repeat until-fail:20`), and **10 times in a visible window** (`QT_QPA_PLATFORM=windows`) |
| `tst_catalog::readingPositionsPersistAndAreChecked`, `tst_migrations::version4CatalogGainsReadingPositions` | Page index 0 is valid, the last page replaces, past-the-end and negative pages are refused, an unknown page count is accepted, an unknown book gives NotFound; the position survives a restart; a schema 4 catalog upgrades |
| Mutations, each reverted | No view-release handshake, position not saved on a switch, view ignoring the requested page, and no pending page before layout: each makes its test fail |
| `qmllint` on the module's QML | No warnings |
| `appMyBooksLibrary --import contents-book.pdf --read-page 15 --screenshot …` (Debug and Release, real SDK) | Exit 0, no QML warnings. The reader shows physical page 15, "3 Networking with TCP/IP"; the catalog records reading position 14. Five further Release runs show the same page, compared pixel by pixel over the page area |

**Review fixes (PR #15 review of `ddeef8e`):**
1. *Should fix:* quitting with a book open left the document to the QML engine's teardown, which destroyed the `PdfDocument` before its view. `Main.qml`'s `onClosing` now also defers while `reader.open`: it saves the position, closes the book through `ReaderController::close()`, and closes the window once the reader is closed and nothing is busy (deferred with `Qt.callLater`, never from inside the closing handler).
2. *Consider:* the release was reported when the `Loader` dropped its item, while the view was only scheduled for deletion. The pane now registers each view (`attachView()`), and the controller continues only after the view's `QObject::destroyed`, from a queued call. `viewReleased()` and the pane's release reporting are removed.

| Command | Result |
| --- | --- |
| `pwsh scripts/verify.ps1` (Release) and `-Configuration Debug` | VERIFY PASSED in both; 18/18 `ctest` suites; new smoke "close while reading" passes (see below) |
| `tst_readercontroller` (6 cases, now with a fake pane that attaches real `QObject` views) | New `waitsForEveryViewToBeDestroyed`: a view detached with `deleteLater()` still holds the document; so does a second attached view; the document is cleared only after the last one is destroyed, and not inside its destruction |
| `tst_readerpane::viewIsDestroyedBeforeTheDocumentChanges` (new, real Qt PDF) | For a switch and for a close, the old view's `destroyed` comes before the controller's `documentChanged`. **Against the unfixed controller and pane it fails** (the document changed while the old view still existed) |
| `appMyBooksLibrary --library <new> --import contents-book.pdf --read-page 15 --close` (new in `verify.ps1`, offscreen, Release and Debug) | Exit 0, `close: reader.open=0`, `reading_positions` holds 14. **With the old `Main.qml` it prints `reader.open=1` and exits 1** |
| Real `WM_CLOSE` (`Process.CloseMainWindow()`), visible window, Release, book open at page 15, 5 runs | Each run: exit 0, reading position 14 stored |
| `ctest -R tst_reader --repeat until-fail:20` (Release, offscreen); `tst_readerpane` `--repeat until-fail:10` with `QT_QPA_PLATFORM=windows` | All passed |
| `all_qmllint` | No warnings |

`--read-page` now also works on its own (the book stays open), and `--close` closes the window when idle as the close button does; both are development options.

**Found and fixed while testing:** the first Release screenshot showed page 1 while the page box said 15. `goToPage()` on a view without a size changes `currentPage` but does not scroll, and the reader is laid out only as the book opens. The requested page now stays pending until the view has a size. `tst_readerpane` reproduced it once it checked the real scroll position; it had checked only `currentPage` before.

**M06 gate:**
- Chapter-only search finds a book (part 1).
- A resolved hit opens the correct page (Open and Open chapter go to the physical page).
- An unresolved hit offers evidence (Contents page / Show contents page, the page where it is listed).
- The results survive a restart (the catalog, search index and reading position persist).

**Not verified by hand:** mouse and keyboard use of the reader's buttons and page box, scrolling through a long book, and screen readers.

**Next action:** M07, corrections and reruns: metadata overrides (Auto, Value, Cleared) and TOC edits in the inspector, reruns that keep edits, and stale results never replacing current data.

### Part 1: search in the application (presentation)

**Scope:**
- `SearchController` and `SearchResultsModel`, and `SearchResultsView.qml`;
- the toolbar search field and scope, Ctrl+F and Esc, results in place of the book list;
- `BookListModel::processingStateOf`;
- `--search`.

**Touched paths:** `src/presentation/`, `qml/search/`, `Main.qml`, `main.cpp`, `CMakeLists.txt`, `tests/presentation/`, `tests/CMakeLists.txt`, `docs/`.

| Command | Result |
| --- | --- |
| `pwsh scripts/verify.ps1 -Clean` (Release) and `-Configuration Debug` | VERIFY PASSED in both; 16/16 `ctest` suites |
| `tst_searchcontroller` (new, 6 cases, model tester) | Typing is debounced (nothing runs while typing; one response for "c", "co", "cooking"); with two requests in flight only the newer is applied; `clear()` drops a response in flight. Chapter hits: page index 0 → "Page 1" with the printed label kept; an unresolved entry → "Page not found", no page, "Listed on page 5 of the PDF"; ambiguous → "Page uncertain". "No matches…" and the punctuation-only text; a blank query returns to the book list. Scope re-runs the query; 60 books page as 50 + "Show more" → 60 |
| `tst_librarycontroller::searchUpdatesWhenContentsArePublished` | A query entered before processing shows the chapter once the contents are published, with "Page 1" and the book's state "Metadata ready · 2 contents entries" |
| Mutations, each reverted | Stale responses applied, no debounce, no re-run after publications, page shown as the index: each makes its test fail |
| Review fixes (PR #14) | `tst_searchresultsview` (new) loads the real `SearchResultsView.qml` offscreen: choose book A; a query still matching A keeps it highlighted; a query without A highlights nothing and announces nothing; a re-run bringing A back highlights A. `paginatesByBook`: a refresh after "Show more" keeps 60 results. Removing either fix makes its test fail. The view reads the chosen book from the model, because `currentItem` can still be the old row's delegate right after new results |
| `qmllint` on the module's QML | No warnings |
| `appMyBooksLibrary --import (3 fixtures) --search tcp/ip --inspect-first --screenshot …` (Debug and Release, real SDK) | Exit 0, no QML warnings. One contents-only result, `contents-book`: "3 Networking with TCP/IP · Page 15" |

**Next action:** M06 part 2. The embedded reader (Qt PDF) behind an adapter that owns document lifetime; "Open chapter" to the physical page and "Show source TOC page" for unresolved entries, from search results and the inspector; the reading position saved per book; stress-testing opening and closing books in a visible window (READER.md).

## M05 — Contents

### Part 2: book inspector and contents tree (presentation)

**Scope:**
- `BookInspector` and `TocTreeModel` (presentation), and `BookInspectorPane.qml`;
- `BookDetails.metadataRun` and `tocRun`;
- the inspector beside the book list in `Main.qml`, the selected-row contrast fix, and `--inspect-first`.

**Touched paths:** `src/presentation/`, `src/domain/book.h`, `src/catalog/catalog.cpp`, `qml/inspector/`, `Main.qml`, `main.cpp`, `CMakeLists.txt`, `tests/presentation/`, `tests/CMakeLists.txt`, `docs/`.

| Command | Result |
| --- | --- |
| `pwsh scripts/verify.ps1 -Clean` (Release) and `-Configuration Debug` | VERIFY PASSED in both; 14/14 `ctest` suites |
| `tst_toctreemodel` (4 cases, model tester) | A parent listed after its child, with a grandchild. A self-reference, a two-entry cycle, a missing parent, and an entry below a cycle member (kept attached). Unknown level. Page texts: index 0 → "Page 1", ambiguous → "Page 5 or 10?", unresolved → "Page not found" with no guessed page. Plain-language details and technical details |
| `tst_librarycontroller` (21 cases, 2 new) | The inspector shows what processing published: value, source and evidence; candidates capped at five plus "and 2 more"; contents summary and notes; page 0 → "Page 1". A refresh keeps the tree (no reset); a missing book gives a message. Selecting while processing updates when the results publish; a quick second selection drops the first load; clearing the selection empties the pane |
| Mutations, each reverted | No cycle check, single pass, page shown as the index, stale loads applied, tree rebuilt on every load: each makes its test fail |
| Review fix (PR #13): the entry details no longer keep the previous book's entry | `tst_inspectorpane` (new) loads the real `BookInspectorPane.qml` offscreen: book A's entry shows "Alpha chapter · Page 1"; after selecting book B the details read "Select an entry…". With the old binding it still showed "Alpha chapter · Page 1" |
| `qmllint` on the module's `Main.qml` and `BookInspectorPane.qml` | No warnings |
| `appMyBooksLibrary --import contents-book.pdf --inspect-first --screenshot …` (Debug and Release, real SDK) | Exit 0, no QML warnings. The tree shows 5 entries with "2.1 Installing the Tools" under "2 Getting Started"; "1 Introduction" is "Page 4" (printed "1"), with its reasons |

**Not verified by hand:** mouse and keyboard use of the tabs, the "Why?" toggles, and expanding and collapsing in the tree; screen readers.

**Next action:** M06, search and reading. Chapter search, opening the resolved page in the embedded viewer, and "Show source TOC page" for unresolved entries.

### Part 1: contents jobs and one SDK call per book (A4 + A2 + A1)

**Scope:**
- schema 4 (contents evidence and plan columns);
- `completeTocJob`, `claimQueuedJob`; import queues metadata and contents jobs;
- the coordinator's paired runs with a group cancel flag, per-job publication, throttled progress;
- `ContentsAnalyzer`, `sdk::SdkContentsAnalyzer` (`analyze`, `analyze_book`) and `sdk::normalizeContents`;
- presentation wiring: the analyzer is injected, Retry works for contents jobs, and book rows and the activity list show contents states and pages read.

**Touched paths:** `src/domain/toc.h`, `src/catalog/`, `src/processing/`, `src/presentation/`, `main.cpp`, `Main.qml`, `CMakeLists.txt`, `tests/`, `docs/`.

| Command | Result |
| --- | --- |
| `pwsh scripts/verify.ps1` (Release) and `-Configuration Debug` | VERIFY PASSED in both; 12/12 `ctest` suites |
| `tst_processingcoordinator` (fakes, 27 cases, 7 new) | A paired run makes one `analyzeBook` call and no separate metadata call, publishes the metadata while the contents stage still runs, and attributes progress by stage. Cancelling only the contents lets the metadata publish and then stops the call; cancelling only the metadata keeps the call for the contents. A contents-only job uses `analyze` and reports progress. Closing during the contents stage keeps the metadata and requeues the contents. A contents entry past the last page fails as `publish_failed`. Entries and evidence survive a restart, including page 0, a parent listed after its child, ambiguous alternatives, and omissions |
| `tst_sdkcontentsanalyzer` (real SDK 0.3.0, 6 cases) | A constructed report joins by ID with mappings out of order and keeps every entry. `contents-book.pdf`: `plan_ready`, 5 entries, label "1" → page index 3, a known parent, all in the plan. `analyze_book` delivers the metadata once, during the metadata stage, with the same contents as `analyze`. `title-page.pdf` gives `no_toc_found_in_search` with no entries. A pre-set cancel makes both results cancelled. End-to-end import publishes both |
| `tst_librarycontroller` (19 cases) | Adapted to two jobs per book with a fake analyzer: the paired run, closing and resuming (2 interrupted, 4 succeeded), cancelling only the metadata then retrying it alone, crash recovery, and waiting without an extractor |
| `tst_importservice`, `tst_migrations` | Import and recovery queue one job of each kind; a schema 3 catalog's contents run loads after the upgrade |
| Mutations, each reverted | Group flag raised by any member, join by position, every refusal treated as a cancel, page count recorded after the check: each makes its test fail |
| Review fix (PR #12): the footer counts waiting **books**, not jobs | `withoutAnExtractorJobsWait`: 3 books give 6 pending jobs and "3 book(s) waiting"; `closingDuringExtractionResumesNextSession`: "Reading title and authors: … · 1 book(s) waiting". Counting jobs again makes both fail (6 and 2) |
| `appMyBooksLibrary` smoke, Release, real SDK and models: the three fixtures | Exit 0, no QML warnings, all 6 jobs `succeeded/published`. `contents-book.pdf` `plan_ready` with 5 entries and a stored plan; the other two `no_toc_found_in_search`; 6 reports |
| Same, `image-only.pdf` alone (issue #3 in-app timing) | Metadata and contents in **24.1 s** of app time, startup included (separate SDK calls: about 58 s) |
| Close during the paired OCR run, then restart (Debug) | Closed after 8.8 s, responding; both jobs `interrupted` and requeued; after restart both `succeeded/published` |

**Limitations:**
- There is no contents view yet (part 2).
- Titles of a large import appear book by book, because each book's call includes its contents analysis.
- Stage names are not shown.

**Next action:** M05 part 2. A book inspector with the contents tree (two-pass, so a parent may come after its child, with cycle checks), per-entry states and reasons, and metadata evidence.

## SDK 0.3.0 update (2026-09-27)

**Why:** PDFMegine released SDK **0.3.0** (tag `v0.3.0` = `bd46d90`; PR #4, merge `dfcd6f8`) for PDFMegine issue #3. It adds `analyze_book()`, which extracts metadata and analyzes the TOC on one session, so each page is read and OCR'd once. M05 will use it.

| Item | Value |
| --- | --- |
| Release asset | `pdfbookmark-sdk-0.3.0-win64.zip`, SHA-256 `998508d45f28ef36ea1d4ae27840814e81df95fbea1e6f3b5895669aa957e744` (matches the release), unpacked to `…\Dev\pdfbookmark-sdk\0.3.0\`; 0.2.0 kept. `PDFBOOKMARK_SDK` (user environment) points at 0.3.0 |
| Header changes from 0.2.0 | New `engine/book.hpp`; `using` lines in `pdfbookmark.hpp`; C API additions in `pdfbookmark.h`; `version.hpp`. All other headers byte-identical |
| Header / loaded version | `0.3.0` / `0.3.0` (Debug and Release) |

**Changes:** `find_package(pdfbookmark 0.3)`; the CI SDK version and digest; the AGENTS.md baseline; the CLAUDE.md SDK brief path; BUILDING.md and the `verify.ps1` example. No application code changes.

**Verification** (clean build folders, SDK taken from the `PDFBOOKMARK_SDK` environment variable):

| Command | Result |
| --- | --- |
| `pwsh scripts/verify.ps1 -Clean` (Release) and `-Configuration Debug -Clean`, at `578e951` with `find_package(pdfbookmark 0.2)`, and again on this branch with `0.3` | VERIFY PASSED in all four: 11/11 `ctest` suites, no `warning C…`, and every `--reader-check` check passes |
| `qt-cmake … -DPDFBOOKMARK_SDK=<sdk 0.2.0>` on this branch | Refused at configure: *Could not find a configuration file for package "pdfbookmark" that is compatible with requested version "0.3"* (found 0.2.0) |
| App smoke with models, Release: import `title-page.pdf`, `contents-book.pdf` and `image-only.pdf` | Exit 0, no QML warnings. Same results as on 0.2.0: one title resolved and two ambiguous; OCR ran on `image-only.pdf`; page counts 3/27/4; 15 field-detail rows |
| Close during OCR on `image-only.pdf`, then restart (Release) | Closed after 5.5 s (6.8 s on 0.2.0) while responding, exit 0. The job was requeued and succeeded after restart |
| Throwaway harness outside the repo: `extract_metadata()` + `analyze()` vs `analyze_book()`, Release, models | On `image-only.pdf` (4 scanned pages), 59.1 s and 58.0 s became **29.0 s and 29.3 s**, with 4 OCR attempts instead of 8 (`pages_reused` 4). Metadata reports identical. Analysis reports identical except `ocr_attempts_used` (0), `acquisition.ocr_budget` (the stage's allowance, 60 instead of 64) and the configuration string of reused pages (`ocr_budget=16`, from the metadata stage). Reported on PDFMegine issue #3 |

**Limitations:** the application does not call `analyze_book()` yet (M05). Issue #3 stays open until the in-app timing is confirmed.

## M04 — Metadata jobs

### Part 2: metadata jobs in the application (presentation + A2)

**Scope:**
- the import transaction queues the metadata job;
- `LibraryController` runs the coordinator: recovery, start, cancel, retry, cancel all, and stop on close;
- `JobListModel` and the job states in `BookListModel`;
- the activity panel and footer summary in `Main.qml`;
- `main.cpp` injects `SdkMetadataExtractor`.

**Touched paths:** `main.cpp`, `Main.qml`, `CMakeLists.txt`, `src/presentation/`, `src/catalog/imports.*`, `src/catalog/jobs.*`, `src/catalog/catalog_internal.h`, `src/catalog/catalog.cpp`, `src/domain/book.h`, `tests/presentation/tst_librarycontroller.cpp`, `tests/storage/tst_importservice.cpp`, `docs/`.

| Command | Result |
| --- | --- |
| `pwsh scripts/verify.ps1 -SdkDir <sdk 0.2.0> -Clean` (Release) and `-Configuration Debug` | VERIFY PASSED in both; 11/11 `ctest` suites |
| `ctest -R "librarycontroller\|processing\|importservice" --repeat until-fail:10` (Debug) | 3/3 passed, 10 times each |
| `tst_librarycontroller` (fake extractor, 5 new cases) | Import → extracted title in the list, job "Done", models updated on the GUI thread; `prepareToClose` during extraction returns promptly and the next session completes both books (states interrupted, succeeded, succeeded); cancel → "Metadata extraction cancelled" with Retry, retry publishes and the old row loses Retry; a job left running by a crash is reported ("queued again") and completed; without an extractor, jobs wait |
| `tst_importservice::importQueuesOneMetadataJob` | Import and recovery-completed import each queue one metadata job (generation 1, the asset's digest); a duplicate queues none |
| `qmllint -I build\verify-debug Main.qml` | No warnings |
| `appMyBooksLibrary --library <new> --import title-page.pdf --import contents-book.pdf --import image-only.pdf --activity --screenshot …` (Debug and Release, real SDK and models) | Exit 0. "Practical Library Engineering" resolved; the other two ambiguous, shown as "Title uncertain"; `image-only.pdf` read by OCR (model identity recorded); page counts 3, 27 and 4; three reports; 15 field-detail rows; no QML warnings on stderr |
| Close (`CloseMainWindow`) while OCR runs on `image-only.pdf`, then restart (scripted) | Debug: closed after 8.7 s; Release: after 6.8 s. Exit 0 and responding throughout in both. Jobs `interrupted` + `queued`; after restart `succeeded/published` |

| Review fixes (PR #10) | `tst_catalog::latestJobsPicksTheNewestPerBookAndKind` (3000 books × 3 jobs: one newest row per book, under a 3 s guard; the old query took 8.0 s there), `tst_librarycontroller::pendingCountIncludesTheWholeBacklog` (150 of 150 waiting jobs counted and listed) and `refreshesAreCoalesced` (20 calls, 2 reloads). Each fails with its fix reverted |

**Found and fixed while testing:**
- QML `String.arg()` takes one argument, so the activity detail line was blank. The calls are now chained, with the free-text error substituted last.
- Ambiguous titles were shown as "No title found". `BookSummary.extractedTitleStatus` now tells them apart.

**Limitations:**
- Closing during OCR waits for the page in progress (cooperative SDK cancellation): 6.8 s observed in Release.
- There is no inspector for metadata evidence yet (field details are stored; M05/M07), and no rerun of a succeeded extraction (M07).
- The native file dialog, drag and drop, and the activity buttons were exercised through the controller API and tests, not by clicking.

**Next action:** M05, contents. TOC analysis jobs through the same queue, with the full parsed TOC and mappings persisted and shown.

### Part 1: durable queue, SDK extraction and publication (A4 + A2 + A1)

**Scope:** schema 3 (`jobs`, `metadata_field_details`); `catalog/jobs.*` (enqueue, claim, cancel, finish, transactional completion, restart recovery); `storage/reportstore.*` (immutable reports and removal of unpublished ones); `processing::ProcessingCoordinator` with the `MetadataExtractor` interface; `sdk::SdkMetadataExtractor` and `sdk::normalizeMetadata`. The application does not use them yet (part 2).

**Touched paths:** `src/domain/jobs.h`, `src/domain/ids.h`, `src/domain/metadata.h`, `src/domain/codes.cpp`, `src/catalog/jobs.*`, `src/catalog/catalog.cpp` (shared publication helpers; `''` for empty required run text), `src/catalog/catalog_internal.h`, `src/catalog/migrations.cpp`, `src/storage/reportstore.*`, `src/processing/`, `CMakeLists.txt`, `tests/processing/`, `tests/catalog/tst_migrations.cpp`, `tests/CMakeLists.txt`, `docs/`.

| Command | Result |
| --- | --- |
| `pwsh scripts/verify.ps1 -SdkDir <sdk 0.2.0>` (Release `-Clean`) and `-Configuration Debug` | VERIFY PASSED in both; 11/11 `ctest` suites; the new suites passed `--repeat until-fail:10`. CI `build-and-test` passed for head `a323e9c` (run 36274652262) |
| `tst_processingcoordinator` (fake extractor) | 20 cases (14 at `c921e81`, the first revision reviewed), 0 failures:<br>- publishes from the worker, signals on the owner thread;<br>- report, details and page count stored;<br>- open job reused without a new generation;<br>- cancel while running, and cancel while the SDK completes anyway: nothing published, report removed;<br>- title override and cleared year set during the run survive;<br>- trash and a newer request during the run: not published;<br>- failure keeps the earlier result;<br>- digest mismatch fails;<br>- restart: running → interrupted + requeued and completed, cancel requested → cancelled, idempotent;<br>- recovery removes only unpublished reports;<br>- a trashed book's queued job never runs;<br>- metadata jobs before TOC jobs;<br>- review fixes: `stop()` requeues the running job and keeps queued ones, and both run after a restart; a result completed after `stop()` is published; destroying the coordinator mid-job interrupts and requeues it; a retry during `cancel_requested` queues a new job that publishes; a cancel recorded after the claim reaches the SDK call; `cancelAll` cancels running and queued jobs; a throwing extractor fails only its job |
| `tst_sdkmetadataextractor` (real SDK 0.2.0) | 5 cases: normalization (ambiguous not promoted, contributor order, separate years, details); `title-page.pdf` → "Practical Library Engineering", 3 pages, report kind `pdfbookmark.metadata`, models used; pre-set cancel → cancelled; `image-only.pdf` without models completes without a title; end-to-end job through the coordinator publishes |
| `tst_migrations` | `version2CatalogGainsJobs`: a schema 2 catalog upgrades and its book can be queued |
| Mutation: completion no longer requires `running`, worker ignores the flag | `cancelWhileTheSdkCompletesDoesNotPublish` fails (job `succeeded`); restored |
| Mutations for the review fixes, each reverted afterwards | Shutdown handled as a cancel: `destructionDuringAJobInterruptsIt`, `stopKeepsWorkForRestart` and `stopDoesNotDiscardACompletedResult` fail. `cancel_requested` counted as pending: `retryWhileCancellingIsNotLost` fails. Flag not raised where the cancel is recorded: `cancelRacingTheClaimReachesTheSdk` fails |

**Found and fixed:** publication failed with "NOT NULL constraint failed: metadata_runs.model_identity" when the model identity was empty (no OCR models), because a null `QString` binds as SQL NULL.

**Limitations:** not yet wired into the application, and no job UI (part 2). TOC jobs are not implemented (M05). There is no automatic retry: a failed job stays failed until the user asks again. `recover()` must be called before `start()`. The `runLoop` exception boundary is verified by inspection only; no test injects a database-task exception.

**Next action:** M04 part 2. Queue metadata after import, run recovery at open, show job progress with cancel and retry, show extracted titles, and handle a responsive close during extraction.

## M03 — Import and library shell

### Part 2: composition root and library window (presentation)

**Touched paths:** `main.cpp`, `Main.qml`, `src/app/`, `src/presentation/`, `CMakeLists.txt`, `tests/presentation/`, `tests/CMakeLists.txt`, `docs/` (UI.md, BUILDING.md, images).

| Command | Result |
| --- | --- |
| Debug and Release builds + `ctest` | 9/9 in both; no `warning C…`; `--repeat until-fail:3` passed in both |
| `tst_librarycontroller` | 10 cases, 0 failures:<br>- opens off the GUI thread with GUI-thread model updates;<br>- imports, lists and survives a restart, with Greek file names;<br>- duplicates and a non-PDF are reported;<br>- files queued while opening get imported;<br>- cancel drops the queue and the session keeps working;<br>- startup recovery completes an interrupted import;<br>- a second session on the same library fails;<br>- library-root resolution |
| `qmllint -I build\sdk020-debug Main.qml` | No warnings (typed `LibraryController` via the `MyBooksLibrary.Presentation` module) |
| `appMyBooksLibrary --library <tmp> --screenshot …` (Debug and Release) | Window opens; the empty state is shown; exit 0 |
| `… --import <two PDFs, one Greek name> --import <duplicate> --screenshot …` | "2 imported, 1 already in the library."; the list shows both, with "From the file name" |
| Close (WM_CLOSE via `Process.CloseMainWindow`) during a 600 MB import | Exited in 128 ms with code 0; responding throughout; `files/` and `staging/` empty; operation `cancelled`; original unchanged (SHA-256 and modification time) |
| Fresh `windeployqt --qmldir .` package, clean `PATH`/QML environment, `--import contents-book.pdf --screenshot` | Exit 0; `QtQuick/Dialogs` and `QtQuick/Pdf` deployed |

**Not verified:** interactive use of the native file dialog and drag-and-drop (scripted runs use `--import`); keyboard-only operation beyond list navigation; screen readers.

### Part 1: managed import (A1 + A2)

**Touched paths:** `src/storage/`, `src/catalog/imports.*`, `src/catalog/catalog_internal.h`, `src/catalog/catalog.*` (insert shared, `Duplicate` code), `src/catalog/migrations.cpp` (v2), `src/domain/importing.h`, `src/domain/ids.h`, `src/domain/result.h`, `src/domain/codes.cpp`, `CMakeLists.txt`, `tests/storage/`, `tests/catalog/tst_migrations.cpp`, `docs/`.

| Command | Result |
| --- | --- |
| Clean Debug and Release builds against SDK 0.2.0 + `ctest` | 8/8 passed in both; no `warning C…` |
| `ctest --test-dir build\sdk020-debug --repeat until-fail:3` | Passed |
| JUnit totals | TestImportService 15, TestMigrations 8; 0 failures |

The first run had one failure in a test: a path compared relative to `files/` instead of the library root. The test was fixed; the behaviour was correct.

**Review fixes (PR #6, review of `2d74ae4`):** two P2 findings fixed (duplicate outcome persisted; recovery keeps the verified stage until installed), plus strict `commitMove` and `ON DELETE RESTRICT` (see DECISIONS.md).

| Check | Result |
| --- | --- |
| Debug and Release builds + `ctest` | 8/8 in both; `--repeat until-fail:3` passed in both |
| `tst_importservice` | 18 cases, 0 failures |
| Mutation: previous migration constraint restored | Fails `duplicateBytesUnderAnotherNameReuseTheBook`, `duplicateOfTrashedBookIsReportedNotRestored` and two `crashRecovery` rows |
| Mutation: recovery removes the stage before installing | Fails `recoveryKeepsVerifiedStageWhenInstallFails`, `recoveryReplacesWrongDigestDestinationWithGoodStage` and `crashRecovery(verified, still in staging)` |

**Re-review fix (PR #6, review of `e12e5dc`):** recovery distinguishes unreadable from damaged copies (`checkDigest`). `tst_importservice` has 21 cases. `recoveryDefersUnreadableCopy` (2 rows) fails on the previous code. ctest 8/8 in Debug and Release, with `--repeat until-fail:3` passing in each.

**Not in this part:** GUI wiring, library-root configuration and the book list (part 2); restoring a trashed duplicate from the UI; page count at import (M04).

## SDK 0.2.0 update (2026-09-25)

**Why:** PDFMegine released SDK **0.2.0** (tag `v0.2.0` = `93d9128`; OCR fix PR #2, merge `eeb977c`) in response to PDFMegine issue #1, which records the OCR memory and throughput findings from M01. Owners: integration and the A4 SDK boundary. Branch `chore/m01-sdk-0.2.0`.

**Installed SDK identity**

| Item | Value |
| --- | --- |
| Release asset | `pdfbookmark-sdk-0.2.0-win64.zip`, SHA-256 `728f5c5115e77082074e355621dfde810363e7fdc0f844fda578bcc9bdb374e0`, unpacked to `…\Dev\pdfbookmark-sdk\0.2.0\` (0.1.0 kept) |
| Header / loaded version | `0.2.0` / `0.2.0` (Debug and Release) |
| `pdfbookmark.dll` SHA-256 | `d71d094382b487a3ae78fbdac273a8bfd047268cd986a307f0bd7cb9d515283f` |
| `pdfbookmarkd.dll` SHA-256 | `8cd0fa4ab476d06cf36e2303f68393b7155d824b01525b0cb3308b700f4a41a7` |
| `pdfbookmark.hpp` SHA-256 | `39b8636f…bf62` (unchanged from 0.1.0) |
| API change | `ocr_threads` in `MetadataRunOptions`, `AnalysisOptions`, `TextOptions` and `text::OpenOptions`, plus `text::resolve_ocr_threads()`. Option struct sizes changed, so a clean rebuild is required. |
| Automatic OCR threads on this machine | 8 (`resolve_ocr_threads(0)`; Ryzen 9 5900X, 24 logical processors) |

**Changes:** `find_package(pdfbookmark 0.2)`. `ProbeOptions.ocrThreads` and `SdkIdentity.ocrThreadsAuto` added (`--sdk-check` prints `sdk.ocr_threads_auto`). The harness gains `--ocr-threads N`, a GUI responsiveness measure (largest gap between event-loop turns), and a QML create/destroy stress option (`--qml-cycles N`, `--qml-naive-teardown`). The QML view now sits in a `Loader`, so it can be destroyed before its document. AGENTS.md baseline, CLAUDE.md SDK-brief import and BUILDING.md (switching SDKs) are updated.

**Verification** (fresh build folders `build\sdk020-debug`, `build\sdk020-release` against the 0.2.0 SDK):

| Command | Result |
| --- | --- |
| Clean Debug and Release builds + `ctest` | 7/7 passed in both; no `warning C…` |
| `--sdk-check tests\fixtures\title-page.pdf` (Debug, Release) | header 0.2.0 = loaded 0.2.0; models found; title resolved |
| `--reader-check title-page.pdf` / `contents-book.pdf --rounds 5` (Debug) | 8/8 PASS each |
| E3 `image-only.pdf --require-ocr --no-models` (Release) | `sdk_alone=NOT_EXERCISED`, exit 1 |
| E1 `--require-ocr --view churn` (Release) | 8/8 PASS |
| E2 `--require-ocr --view persistent` (Release) | 8/8 PASS |
| E4 `--require-ocr --view persistent --ocr-threads 4` (Release) | 8/8 PASS; digests equal to automatic threads |
| Fresh `windeployqt` package against 0.2.0, clean environment: `--sdk-check`, `--sqlite-check`, `--reader-check contents-book.pdf --rounds 20` (25 repetitions), `--reader-check image-only.pdf --require-ocr --view persistent` | All exit 0; OCR 8/8 PASS with models, QML plugin and `Qt6PdfQuick.dll` from the package |
| `--qml-cycles 300 --qml-naive-teardown`, 5 processes (Release) | 1,500/1,500 cycles, no crash |

**Before and after** (`image-only.pdf`, 4 scanned pages, Release, same harness, same machine):

| Measure | SDK 0.1.0 | SDK 0.2.0 |
| --- | --- | --- |
| Metadata + analysis with OCR (8 page OCRs), SDK alone | 151.5 s (~19 s/page) | **57.7 s** (~7.2 s/page); 81.1 s with `ocr_threads=4` |
| Process private memory peak | 8,915 MB | **2,381 MB** |
| System commit peak during OCR with viewing (limit 57,248 MB; ~43 GB before the run) | 51,875 MB | **46,525 MB** |
| Cancellation during OCR (request to return) | 290 ms | **21–28 ms** |
| GUI max gap between event-loop turns, persistent viewer: Qt only / during OCR | not measured | 4 ms / **10 ms** (auto threads); 10 ms / 7 ms (4 threads) |
| GUI max gap, churn viewer (whole document per turn): Qt only / during OCR | not measured | 19 ms / 34 ms |
| Crashes with viewing during OCR | 3/3 with the first harness; 0/2 later | 0/4 (E1, E2, E4, package) |

**New observation (open, unexplained):** one package run of `--reader-check contents-book.pdf --rounds 20` (text only, no OCR) ended with `0xC0000005` on a **Qt Quick worker thread**. The stack was `Qt6Core` thread start → `Qt6Quick` → `Qt6Gui`, with no SDK module. It happened after `qml_pdf_module` passed, during teardown of that check. It did not recur in 50 further package runs or in 1,500 naive QML create/destroy cycles. It is recorded as an M06 reader-lifecycle risk (READER.md). The ordered teardown is kept as the default, but it is not proven to address it.

**Next action:** owner review; then the PDFMegine issue #1 comment with these figures (drafted for owner approval); then M03.

## M02 — Contracts and persistence

**Owners:** A2 (catalog, migrations, library lock), A3 foundation (projections, query compiler), infrastructure (database executor), domain contracts.

**Touched paths:** `CMakeLists.txt`, `src/domain/`, `src/infrastructure/databaseexecutor.*`, `src/catalog/`, `src/search/`, `tests/CMakeLists.txt`, `tests/infrastructure/tst_databaseexecutor.cpp`, `tests/search/`, `tests/catalog/`, `docs/`.

| Command (in `vcvars64`; Qt `bin` on `PATH` for direct runs) | Result |
| --- | --- |
| `qt-cmake … -B build\cli-debug -Wno-dev` + `cmake --build build\cli-debug` | Builds with no `warning C…` lines in the log |
| `ctest --test-dir build\cli-debug --output-on-failure` | 5/5 test executables passed |
| `ctest --test-dir build\cli-debug --repeat until-fail:5` | 5/5 passed on every repetition |
| Per-executable JUnit totals | `TestSqliteCapabilities` 4, `TestDatabaseExecutor` 7, `TestFtsQuery` 34, `TestMigrations` 7, `TestCatalog` 17; 0 failures |
| `appMyBooksLibrary.exe --sqlite-check` / `--sdk-check tests\fixtures\title-page.pdf` | Exit 0 / exit 0 (no regression) |

**Behaviour covered by tests:**
- Executor: tasks run on its own thread and in order; exceptions are delivered; foreign keys are enabled and the persistent journal mode is left untouched (the library enables WAL after its schema check, since the PR #3 review); open failures are reported; the connection is removed on destruction.
- Migrations: a fresh library reaches the latest version; reopening is idempotent; a newer schema is refused unchanged; a failing migration rolls back (including DDL); a non-consecutive list is rejected.
- Catalog:
  - a second open is locked, and works again after release;
  - a registered book is searchable by its file-name fallback;
  - duplicate SHA-256 is rejected;
  - Auto/Value/Cleared overrides and their search effects;
  - Cleared survives a rerun;
  - Ambiguous is not auto-accepted;
  - stale and wrong-source results are rejected, and metadata and TOC generations are independent;
  - every TOC entry is kept, with page 0, labels, child-before-parent, a missing parent downgraded to Unknown, and a cycle broken;
  - a resolved hit opens its page, an unresolved hit offers only the source TOC page, and an omitted entry is still searchable;
  - `C++`/`C`/`C#`/`HTTP/2`, diacritics, prefix and operator text;
  - two-tier ranking with a bounded chapter list and pagination with the generation echo;
  - a failed TOC publication leaves no index rows and keeps the old TOC and metadata;
  - trash/restore races;
  - index rebuild after damage;
  - Greek and non-ASCII records survive a restart.

**Review fixes (PR #3, review of `0c2b420`):** three P2 findings were fixed, covering the TOC outcome, per-book title lookups and WAL before the schema check (see DECISIONS.md).

| Check | Result |
| --- | --- |
| `ctest --test-dir build\cli-debug --output-on-failure` | 5/5 passed; `--repeat until-fail:3` passed |
| JUnit totals | DatabaseExecutor 7, Migrations 7, Catalog 19 (two new cases), 0 failures |
| Old code for the three fixes restored temporarily (`git stash` of `library.cpp`, `databaseexecutor.cpp`, `catalog.cpp`) | `enablesForeignKeysWithoutPersistentChanges`, `newerSchemaIsRefusedUnchanged` and `tocOutcomeRoundTripsAndMismatchIsRejected` fail; with the fixes restored, all pass |
| `manyContentsOnlyMatchesUseOneTitleRead` | 401 matches, first page correct, search 14 ms (Debug) |

**Not covered in this step:** no GUI or composition-root use yet (M03); no managed files; SDK results are not yet normalised into these contracts (M04/M05); no crash-in-the-middle test for the filesystem side (A1, M03).

## M01 — Feasibility

### Part 1: FTS5 through QSQLITE (A3 foundation, infrastructure)

**Touched paths:** `CMakeLists.txt`, `main.cpp`, `src/infrastructure/`, `tests/`, `docs/`.

| Command (in `vcvars64`, Qt `bin` on `PATH` where needed) | Result |
| --- | --- |
| `qt-cmake … -B build\cli-debug` + `cmake --build build\cli-debug` | Builds `mbl_core`, the app and `tst_sqlitecapabilities` |
| `ctest --test-dir build\cli-debug --output-on-failure -V` | 1/1 passed; ctest applied `PATH=path_list_prepend:C:/Qt/6.11.2/msvc2022_64/bin` |
| `tst_sqlitecapabilities.exe -o file,txt` | 4 passed, 0 failed (`probeReportsFts5`, `punctuationNeedsEscaping`, init and cleanup) |
| `appMyBooksLibrary.exe --sqlite-check` | Exit 0; SQLite **3.53.4**; compile options include `ENABLE_FTS5`, `ENABLE_FTS3/4`, `THREADSAFE=1`, `OMIT_LOAD_EXTENSION`, `TEMP_STORE=1`; fts5, bm25, remove_diacritics and prefix all `true` |
| `appMyBooksLibrary.exe --sdk-check tests\fixtures\title-page.pdf` | Exit 0; title still `resolved` (no regression) |

**Findings:**
- `OMIT_LOAD_EXTENSION` means no custom tokenizers or extensions can be loaded. The built-in `unicode61` (with diacritic folding) and `trigram` tokenizers are the options.
- Raw user text is FTS5 query syntax (`C++` is rejected), so A3 must quote and escape queries.
- On this Windows setup, the Qt Test plain-text logger writes nothing to a console or pipe. JUnit XML to stdout and `-o file,txt` both work.

**Review fix (PR #2, P3):** `mbl_add_test()` now registers tests with `-o -,junitxml`. To verify, I temporarily added a failing `QCOMPARE(caps.sqliteVersion, "deliberate-failure")`. `ctest --output-on-failure -V` reported `Failed` and printed the `<failure>` element with `Actual "3.53.4"` / `Expected "deliberate-failure"`. After restoring the test (no diff left), ctest passed 1/1 with the command `tst_sqlitecapabilities.exe "-o" "-,junitxml"`.

### Part 2: Qt PDF availability and coexistence (reader)

The owner installed Qt PDF on 2026-09-25. **Touched paths:** `CMakeLists.txt`, `main.cpp`, `src/processing/sdk/sdkinfo.*`, `src/reader/`, `tests/fixtures/`, `docs/`.

| Command | Result |
| --- | --- |
| Debug and Release builds (`qt-cmake` + `cmake --build`, `-Wno-dev`) | No `warning C…`; ctest 1/1 passed on this branch |
| `dumpbin /dependents` / `/exports` | `Qt6Pdf.dll` embeds PDFium (imports no `pdfium.dll`); the SDK uses its own `pdfium.dll` |
| `--reader-check title-page.pdf 3` (Debug) | 7/7 PASS |
| `--reader-check contents-book.pdf 10` (Debug) | 7/7 PASS |
| Packaged Release (`windeployqt --release --qmldir`, SDK DLLs and models; `PATH` = System32), `--reader-check contents-book.pdf 20`, `--sqlite-check`, `--sdk-check` | 7/7 PASS; exit 0; exit 0 |
| `--reader-check image-only.pdf 2 no-view` (Release) | 7/7 PASS; cancel honoured after 18.9 s |
| `--reader-check image-only.pdf 1` (Release), 3 runs | **Exit `0xE0000008`** every time, on the GUI thread in `Qt6Pdf.dll`, at ~8.98 GB private memory |

**Review fixes (PR #4, review of `d07c0af`):** four P2 findings fixed (OCR must really run, QML import visible to deployment, real cancellation, semantic comparison), plus the controls the reviewer asked for.

| Command | Result |
| --- | --- |
| Debug build + ctest (branch before merging `main`) | 3/3 passed: `tst_sqlitecapabilities`, `tst_checkverdict` (9 cases), `tst_sdksnapshot` (8 cases, including a real missing-models analysis giving NOT_EXERCISED) |
| `--reader-check title-page.pdf --rounds 5`, `contents-book.pdf --rounds 5` (Debug) | 8/8 PASS each; cancellation observed during active work (8 ms, 18 ms) |
| E3 `image-only.pdf --require-ocr --no-models` (Release) | `sdk_alone=NOT_EXERCISED`; exit 1 |
| E1 `image-only.pdf --require-ocr --view churn` (Release) | 8/8 PASS; 10,438 cycles during OCR; process peak 8,915 MB; system commit peak 51,875/57,248 MB |
| E2 `image-only.pdf --require-ocr --view persistent` (Release) | 8/8 PASS; 54,526 renders during OCR; system commit peak 51,134/57,248 MB |
| Fresh `windeployqt --qmldir .` package, clean `PATH`/QML/plugin environment | 8/8 PASS; QtQuick.Pdf plugin and `Qt6PdfQuick.dll` loaded from the package |

**Re-review fix (PR #4, review of `bd61dc0`):** the semantic snapshot encoding is now unambiguous (quoted and escaped strings, explicit `null`, no format-string substitution). `tst_sdksnapshot` has 9 cases; `encodingHasNoCollisions` fails on the previous encoding. ctest 7/7; `--reader-check contents-book.pdf --rounds 3` 8/8 PASS (Debug). The OCR runs above were not repeated for this change, because it only affects how equal results are encoded.

**Risk (not a proven blocker):** earlier runs crashed 3/3 (`0xE0000008` in `Qt6Pdf.dll`); the corrected runs passed 0/2 crashes near the system commit limit. Details and options are in READER.md. Not verified: Qt Quick rendering in a visible window (only instantiation was checked), and macOS/Linux.

**Next action:** owner review, and a decision on the blocker options in READER.md. Report the OCR memory profile and cancel latency to PDFMegine.

## M00 — Baseline

**Owner:** Integration, A4 SDK boundary (`src/processing/sdk/`).

**Touched paths:** `CMakeLists.txt`, `main.cpp`, `src/processing/sdk/`, `tests/fixtures/`, `.gitignore`, `docs/`.

### Installed SDK identity (recorded 2026-09-24)

| Item | Value |
| --- | --- |
| Engine source | `PantaKoda/PDFMegine` @ `4499b719d7adb71e5cc372bde88d47f66d8ffebf` (per AGENTS.md baseline) |
| Package | `find_package(pdfbookmark 0.1 CONFIG REQUIRED)` → `pdfbookmark::pdfbookmark` |
| Header version | `PDFBOOKMARK_VERSION_STRING` = `0.1.0` |
| Loaded version | `pdfbookmark::version()` = `0.1.0` (Debug DLL) |
| `pdfbookmark.dll` SHA-256 | `a8fdd9b9ebca94293076370f0fbc56626c2f6ee56761bdc03f759600a783ee73` |
| `pdfbookmarkd.dll` SHA-256 | `d2c0fc3a58da528106ea1f4a9f9af8d4ffc2e2680be74a8a4467c280e99f53f6` |
| `pdfbookmark.hpp` SHA-256 | `39b8636f80ff1b29bbf0ad615f75889997e46e45b1594109b349e1fe5643bf62` |
| OCR models | Deployed to `<exe dir>/models`; `find_models()` located them |

### Toolchain used

Qt 6.11.2 msvc2022_64; MSVC 14.51.36231 (VS 2026 Community); CMake 3.30.5 and Ninja 1.12.1 from `C:\Qt\Tools`; Windows 11 Pro 10.0.26200.

### Verification (run locally on Windows, 2026-09-24)

| Command | Result |
| --- | --- |
| `qt-cmake -S . -B build\cli-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPDFBOOKMARK_SDK=…` then `cmake --build build\cli-debug` (inside `vcvars64.bat`) | Configured and linked; runtime DLLs and `models/` deployed |
| `appMyBooksLibrary.exe --sdk-check` | Exit 0; header 0.1.0 = loaded 0.1.0; models found |
| `appMyBooksLibrary.exe --sdk-check tests\fixtures\title-page.pdf` | Exit 0; sha256 `26f29980…0d195f` (matches `sha256sum`); 3 pages; title `resolved` = "Practical Library Engineering"; 3 pages searched; metadata JSON 4253 bytes |
| Same with a copy at `%TEMP%\mbl-Βιβλία-ü\Τίτλος ü.pdf` | Exit 0; same digest and title |
| `appMyBooksLibrary.exe --sdk-check missing.pdf` | Exit 1; `pdf.error=Not a readable regular file: missing.pdf` |
| Launch `appMyBooksLibrary.exe` (GUI) | Still running after 4 s; terminated by the test. Window contents not inspected. |

**Review fixes (PR #1, review of `ccafd38`):**

| Check | Result |
| --- | --- |
| P2 sequence: env unset, no `-D`, fresh folder | Exit 1, "PDFBOOKMARK_SDK is not set" message; cache holds `PDFBOOKMARK_SDK:PATH=` (empty) |
| Then set env, configure the same folder | Exit 0; cache = SDK path |
| Then set env to `C:/does-not-exist`, configure the same folder | Exit 0; cache keeps the SDK path (nonempty cache wins) |
| Fresh folder, bogus env, explicit `-DPDFBOOKMARK_SDK=<sdk>` | Exit 0; cache = explicit SDK path |
| P3: `git check-ignore -v CMakeUserPresets.json` | Exit 0, matched `.gitignore:/CMakeUserPresets.json`; a nested `x/CMakeUserPresets.json` is not ignored (root-only rule) |
| Rebuild `build\cli-debug` and `--sdk-check tests\fixtures\title-page.pdf` | Exit 0; same versions, digest and resolved title as before |

The Qt Creator build folder was not rebuilt in this step. CI is not configured.

### Findings

- **SDK deploy-script warning (report to PDFMegine):** Every build prints a CMake dev warning from `lib/cmake/pdfbookmark/pdfbookmarkDeployRuntime.cmake:12`: "Invalid escape sequence `\.`" in the regex `/pdfbookmarkd?\.dll$`, under policy CMP0010. Deployment still succeeds. The SDK is read-only here, so this needs a fix upstream.
- **Qt PDF is not installed** in `C:\Qt\6.11.2\msvc2022_64` (no `Qt6Pdf*`). M01 needs it; installing it requires owner approval through the Qt Maintenance Tool. *(Resolved 2026-09-25: the owner installed Qt PDF; see M01 part 2.)*
- **QSQLITE is present** (`plugins/sqldrivers/qsqlite.dll`). The FTS5 probe is part of M01.
- Outside Qt Creator, Qt DLLs must be on `PATH` to run the executable. This will be addressed by packaging (M10).

### Next action

Merged as PR #1. Continued in M01.
