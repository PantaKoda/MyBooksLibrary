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
| M07 Corrections/reruns | Merged: part 1 [PR #16](https://github.com/PantaKoda/MyBooksLibrary/pull/16) (merge `152eb8c`); part 2a [PR #17](https://github.com/PantaKoda/MyBooksLibrary/pull/17) (merge `c215319`); part 2b [PR #18](https://github.com/PantaKoda/MyBooksLibrary/pull/18) (merge `f40b99c`) | `feat/m07-presentation-metadata-corrections`; `feat/m07-a2-toc-edits`; `feat/m07-presentation-toc-editing` | See "M07" |
| M08 Organization | Merged: part 1 [PR #19](https://github.com/PantaKoda/MyBooksLibrary/pull/19) (merge `7052536`); part 2 [PR #20](https://github.com/PantaKoda/MyBooksLibrary/pull/20) (merge `c182ec3`). Permanent deletion of trashed books remains open | `feat/m08-a2-collections-trash`; `feat/m08-presentation-organization` | See "M08" |
| M09 Export | Merged: part 1 [PR #21](https://github.com/PantaKoda/MyBooksLibrary/pull/21) (merge `acc2b89`); part 2 [PR #22](https://github.com/PantaKoda/MyBooksLibrary/pull/22) (merge `f2bd6b4`); part 3 [PR #23](https://github.com/PantaKoda/MyBooksLibrary/pull/23) (merge `1dbbe0c`) | `feat/m09-a1-export-core`; `feat/m09-a2-export-jobs`; `feat/m09-presentation-export` | See "M09" |
| M10 Windows release | Merged: part 1 [PR #24](https://github.com/PantaKoda/MyBooksLibrary/pull/24) (merge `0c682c3`); part 2 [PR #25](https://github.com/PantaKoda/MyBooksLibrary/pull/25) (merge `54ed8fc`); part 3 [PR #26](https://github.com/PantaKoda/MyBooksLibrary/pull/26) (merge `1683e9a`). The owner's manual check of the package on a machine without Qt or Visual Studio remains | `feat/m10-a1-backup-restore`; `feat/m10-packaging`; `feat/m10-presentation-backup` | See "M10" |
| Releases on GitHub; **v0.1.0** | Merged: [PR #28](https://github.com/PantaKoda/MyBooksLibrary/pull/28) (merge `4a7dda1`). **v0.1.0** tagged at `e142797` and published on the Releases page | `ci/m10-release-workflow` | See "Releases on GitHub" |
| After M10: owner's testing | Merged. **In v0.1.0:** [PR #27](https://github.com/PantaKoda/MyBooksLibrary/pull/27) (merge `adcf7b2`), [PR #29](https://github.com/PantaKoda/MyBooksLibrary/pull/29) (merge `e142797`). **In v0.2.0:** [PR #32](https://github.com/PantaKoda/MyBooksLibrary/pull/32) (merge `e3539ed`), [PR #33](https://github.com/PantaKoda/MyBooksLibrary/pull/33) (merge `41e7dec`), [PR #34](https://github.com/PantaKoda/MyBooksLibrary/pull/34) (merge `307c8ad`), [PR #31](https://github.com/PantaKoda/MyBooksLibrary/pull/31) (merge `edad42c`). The owner's manual check of the remembered library remains | `fix/m05-contents-notes-visible`; `docs/user-guide`; `fix/m10-a2-refuse-backup-folder`; `feat/m10-presentation-open-library`; `feat/m10-app-remember-library`; `fix/m05-a4-page-label-sections` | See "After M10" |
| UI overhaul; build fix; **v0.2.0** | Merged: [PR #37](https://github.com/PantaKoda/MyBooksLibrary/pull/37) (merge `9968194`, the SDK runtime deployed once), [PR #35](https://github.com/PantaKoda/MyBooksLibrary/pull/35) (merge `03859dc`, release notes), [PR #36](https://github.com/PantaKoda/MyBooksLibrary/pull/36) (merge `c100105`, UI PR 1). **v0.2.0** via the release PR `release/v0.2.0` | `fix/ci-sdk-runtime-deploy-race`; `docs/next-release-notes`; `feat/ui-01-presentation-foundation`; `release/v0.2.0` | See "UI overhaul" |
| Owner's testing after v0.2.0 | Merged: [PR #39](https://github.com/PantaKoda/MyBooksLibrary/pull/39) (merge `5848097`), after the owner's review and CI on `cb19d50` | `fix/inspector-plain-reasons` | See "Plain reasons instead of Why?" |
| SDK 0.4.0 update; **v0.3.0** | Merged: [PR #40](https://github.com/PantaKoda/MyBooksLibrary/pull/40) (merge `b6501da`), after CI on `f72e14d`. **v0.3.0** (PRs #39, #40) via the release PR `release/v0.3.0` | `chore/sdk-0.4.0`; `release/v0.3.0` | See "SDK 0.4.0 update" |
| Themes and accents | AwaitingReview: [PR #42](https://github.com/PantaKoda/MyBooksLibrary/pull/42) | `feat/ui-themes-accents` | See "Themes and accents" |
| M11 | NotStarted | | |

## UI overhaul (after v0.1.0)

**Asked by the owner (2026-10-02):** a better-looking window. The owner's choices after an audit of the running app:
- the FluentWinUI3 style;
- dark mode that follows Windows;
- cover thumbnails later;
- the order: PR 1 foundation, PR 1b English plurals, PR 2 window layout, PR 3 book rows, PR 4 inspector, PR 5 reader.

### PR 1: the FluentWinUI3 style and a shared Theme (presentation)

**Branch:** `feat/ui-01-presentation-foundation`. **Status:** Merged: [PR #36](https://github.com/PantaKoda/MyBooksLibrary/pull/36), merge `c100105` (2026-10-03), after CI on the head `90818ea`. It was merged on the owner's go without an independent review, for the 0.2.0 release.

**Audit of `main`** (Release build, scratch library with the fixtures; `docs/images/ui1-before.png`):
- with no style set, Qt uses its older "Windows" style;
- 11 px captions, and secondary text dimmed with opacity;
- `"firebrick"` errors;
- selection text in `palette.highlightedText`.

With `-style FluentWinUI3` alone (`docs/images/ui1-fluent-style-only.png`), the selected book turns white on light grey, and the contents details run under the status bar.

**Change:**
- `qtquickcontrols2.conf` (in the app's resources): `Style=FluentWinUI3`.
- **`Theme`** (`qml/theme/Theme.qml`, a singleton in `MyBooksLibrary.Presentation`; UI.md, "Theme"):
  - Fluent's type ramp, with Fluent's sizes as a floor;
  - spacing and radii;
  - secondary text;
  - status colours (critical, success, caution), light and dark from `Application.styleHints.colorScheme`;
  - a selected-row fill, card fill and stroke, and dividers.
- **Every view** (`Main.qml`, `qml/**`) uses `Theme` instead of fixed pixel sizes, `"firebrick"`, text opacities and `palette.highlightedText`. Headings are DemiBold rather than bold.
- **Selected rows** keep their text colour, as the style draws them. The contents tree's current row is a subtle fill with an accent bar.
- **The contents entry's details:** a card that scrolls within at most half the Contents tab.
- **The pane dividers:** a thin line with a 7 px grab area, an accent line on hover or drag.
- **Window:** the default size is 1100×720, because Fluent's controls are larger.
- **`main.cpp`:** `--color-scheme light|dark` (development) sets `QStyleHints::setColorScheme`.
- **Docs:** UI.md ("Theme", with screenshots), BUILDING.md (the flag and `-style`), DECISIONS.md.

**Tests:** `tst_theme` (new) checks three things:
- the text and status colours keep at least 4.5:1 contrast in both schemes, on Fluent's window (`#f3f3f3` light, `#202020` dark), on a card and on a selected row;
- the type ramp is ordered, and a caption is at least 11 px;
- no handwritten view sets a fixed colour, a fixed font size or `highlightedText`.

Its first run failed twice, rightly: WinUI's light caution `#9d5d00` reaches 4.36:1 on a selected row (now `#8a5000`), and a ramp scaled from Qt's 9 pt default gave 10 px captions (Fluent's sizes are now the floor). The existing QML tests run on the Basic style and pass unchanged.

**Screenshots:** `docs/images/ui1-after-light.png` and `ui1-after-dark.png` (`--color-scheme`), at 1100×720.

**CI note:** the first CI run of PR #36 failed in the build, before any test ran. The app and five SDK tests each copied the SDK runtime into the same folder in parallel, and two copies of `pdfium.dll` collided. [PR #37](https://github.com/PantaKoda/MyBooksLibrary/pull/37) fixed this (merge `9968194`): the SDK tests reuse the app's deployment (`mbl_use_sdk_runtime`), so the build has one deployment step. A fresh-build `verify.ps1` passed with 33/33 tests, and CI passed.

**Released in v0.2.0**, with issue #30's PRs and the page-label fix (`docs/releases/v0.2.0.md`).

**Next:** PR 1b, English plurals.

### Themes and accents (presentation)

**Asked by the owner (2026-10-04):** "the UI is ugly. Take inspiration from https://github.com/PantaKoda/repo-watch to add themes and accents."

**Branch:** `feat/ui-themes-accents`. **Status:** AwaitingReview: [PR #42](https://github.com/PantaKoda/MyBooksLibrary/pull/42).

**Before** (`main` at `f7a26e2`, Release, scratch library with the fixtures, Windows platform): Fluent takes Windows' accent, which is dark grey on this machine, so the selection bars, the tab underline and focus were black or grey on flat grey surfaces.

**Change:**
- `src/presentation/appearance.*` (new): `Appearance`, the theme (System/Light/Dark, applied with `QStyleHints::setColorScheme`) and accent (six presets, one of them Windows' own), stored in QSettings (`appearance/theme`, `appearance/accent`). A QML singleton; `main.cpp` owns the instance.
- `qml/theme/AppearanceDialog.qml` (new), opened from a round accent swatch at the right of the toolbar.
- `qml/theme/Theme.qml`: `accent`, `tint()`, and `selectedFill` / `paneTint` / `barTint` from the accent.
- `Main.qml`: `palette.accent: Theme.accent`; tinted toolbar and status bar with dividers; the Appearance button; **Import PDFs…** is an accent button. `LibrarySidebar.qml`: tinted background. `BookInspectorPane.qml`: **Read** is an accent button.
- `main.cpp`: `--accent <id>`; with it or `--color-scheme`, the stored choice is neither read nor written.
- Docs: UI.md ("Appearance: theme and accent", screenshots), USER_GUIDE.md, BUILDING.md, DECISIONS.md, `releases/UNRELEASED.md`.

**Tests:**
- `tst_appearance` (new, 5 cases): every preset's contrast (4.5:1 for accent-button text, 3:1 on the window); choices stored and applied again at the next start; unknown stored values fall back; nothing stored without a QSettings; the real dialog changes the choice, its checked state and the Theme singleton's accent. Its first run found that QML built its own `Appearance` instead of using `create()` (now no default constructor).
- `tst_theme`: text and status colours on every accent-tinted surface, for every preset. Its first runs failed at 8–10% light tints (critical 4.42:1, success 4.38:1 on a selected row); the light tints are now 5%.
- The offscreen platform ignores colour-scheme requests. `tst_appearance` reports which case applies and asserts the scheme only where it applies; run with `QT_QPA_PLATFORM=windows` locally, all 7 cases passed with the scheme checks included.

**Verification (local, Windows 11, Qt 6.11.2 MSVC 2022, SDK 0.4.0):** `pwsh scripts/verify.ps1` (Release) passed: text checks, guard tests, build, 35/35 tests and the smoke checks, on the code of `f17c18f`. Debug not run.

**Screenshots:** `docs/images/ui-accent-light-lapis.png`, `ui-accent-dark-violet.png`, `ui-accent-dark-teal.png` (`--color-scheme`, `--accent`), and `ui-appearance-dialog.png` (`tst_appearance` with `MBL_SCREENSHOT_DIR`, the FluentWinUI3 style and the Windows platform).

**Review fixes (PR #42, review of `c28aa8b`):**
- **Blocking, fixed:** clicking the chosen accent again removed its ring (a checkable button outside a group toggles itself off; choosing the same accent emits nothing). The swatches are no longer checkable, so the choice alone decides the ring. They have `Accessible.role: RadioButton` and `checked`. A new test step clicks the chosen swatch with the mouse and with Space. With the old swatch the step fails, offscreen and on the Windows platform.
- **Found while testing that:** `accents` shared the accent-change signal, so every click rebuilt all the swatches. `accents` now has its own `accentsChanged`, emitted only when Windows' accent really changes.
- **Nit, fixed:** the application event filter is installed only for the instance registered with `setInstance`, not once for every instance.
- **Nit, fixed:** the start-up options moved into `Appearance::fromArguments`, tested with a stored choice: the overrides neither read nor overwrite it, and without them it is used.
- **Nit, fixed:** an unknown `--accent` prints `warning=unknown --accent "…"; use one of: …`.
- **Design note, documented:** the contents tree's selected row is an accent tint, while the lists keep Fluent's neutral fill; both have the accent bar (UI.md, "Selected rows"). The owner can choose to keep or change this.

**Remaining:** the owner's look at the app on their own Windows settings. The UI overhaul's layout PRs (window layout, book rows, inspector, reader) are unchanged and still to come.

## After M10: fixes from the owner's testing

### Plain reasons instead of "Why?" (presentation)

**Asked by the owner (2026-10-03):** with a scanned book (*Black Holes, White Dwarfs, and Neutron Stars*, 1983, 660 pages), the inspector showed "—" and a "Why?" button on almost every row, and "No contents entries." with "Why? (1)". The owner asked for "a better message or nothing, not 'why ?'". Before: `docs/images/inspector-why-before.png`, `docs/images/inspector-contents-why-before.png`.

**Why the book shows so little** (SDK 0.3.0, read-only checks with the PDFMegine CLI; engine work, not this PR):
- title and authors are ambiguous: two title blocks disagree, and the second page splits its subtitle over two lines;
- the contents are blocked: the scan's section numbers are separate text regions, so one TOC page (physical index 12) falls below the SDK's row density, and the TOC splits into two candidates of nearly equal score (59.7 and 58.5) that the SDK refuses to choose between.

**Change** (`BookInspector`, `BookInspectorPane.qml`):
- a field without a value shows the analysis's reasons under it, always (`note`);
- the button is named for what it reveals: "Show candidates (*n*)" or "Show evidence" (`detailsLabel`), and there is none when there is nothing to reveal;
- the italic source line wraps, so a longer button never covers it (before, "Why?" already overlapped "none chosen");
- Contents: "Why? (*n*)" is "Analysis notes (*n*)". With no entries and plan blockers, the summary is "Possible contents pages were found, but the analysis could not settle on a table of contents.";
- docs: USER_GUIDE.md, UI.md, DECISIONS.md, releases/UNRELEASED.md.

**Tests:**
- `tst_librarycontroller::inspectorShowsWhatWasPublished` checks the title's "Show candidates (7)", no button for authors without details, and the edition's reason as its `note`, with no evidence and no button;
- `tst_inspectorpane::unreadableContentsSayWhatHappened` (new): a run with no entries and one blocker gives the new summary and "Analysis notes (1)".

**Verified** (Windows, Qt 6.11.2 MSVC2022 64-bit, SDK 0.3.0, worktree on `origin/main` `e47e1b1`):
- `pwsh scripts/verify.ps1 -Configuration Release` and `-Configuration Debug`: VERIFY PASSED, 34/34 tests each, smoke checks passed;
- the same book in a scratch library (`--library <scratch> --import <book> --inspect-first --color-scheme light`), Release, before and after: `docs/images/inspector-plain-reasons-after.png`, `inspector-plain-reasons-candidates.png` (after "Show candidates (2)"), `inspector-contents-notes-after.png`.

**Review follow-up** (owner's review at `8a148f7`, four minor points):
- the note is decided by whether the *document* gave a value (status not resolved), so a corrected or cleared field keeps the reason as its note and gets no "Show evidence" button revealing only that reason;
- reasons are joined with line breaks, since SDK reasons contain "; ";
- the contents summary is "Possible contents pages were found, but the analysis could not settle on a table of contents.", true for all three blockers that reach it (two close candidates, no parsed entries, a requested candidate not detected), not only for poor scans;
- the new pane test found that the source line was squeezed to nothing when the two buttons filled its row (480 px pane; the inspector can be 280 px wide). The row is now a grid that moves the buttons under the source line when the line would have less than 140 px;
- tests: `inspectorShowsWhatWasPublished` now corrects and then clears the edition and checks the note stays with no button; `tst_inspectorpane::fieldsWithoutValueSayWhy` (new) renders the field rows in a 480 px pane: the edition's note is visible with one reason per line and no button, a field without reasons has no note, "Show candidates (2)" toggles the candidates and "Hide", and at 900, 480 and 300 px the source line keeps at least 100 px, fits its text and never overlaps the button (on one line with it at 900 px).

**Remaining:** the "Authors and contributors" label still runs into its value at the default width (unchanged by this PR). Reading this book properly needs PDFMegine changes (S2 density, S6 title and authors).

### Remembering the last library (composition), issue #30 part 3

**Asked in [issue #30](https://github.com/PantaKoda/MyBooksLibrary/issues/30):** remember the library last opened, so that it opens again at the next start. Stacked on part 2.

**Change:**
- **`app::LibraryMemory`** (`src/app/librarymemory.*`): the library last opened from the app, in `QSettings` (`library/last`, on Windows under `HKCU\Software\MyBooksLibrary\MyBooksLibrary`). `app::rememberWhenOpened` stores it once the session is **Ready**, and never if the open fails.
- **`app::resolveLibraryRoot(arguments, remembered)`:** `--library`, then `MYBOOKSLIBRARY_ROOT`, then the remembered library (source "remembered"), then the default. A remembered default folder resolves as "default", so it is made again on first use.
- **`main.cpp`:**
  - it opens a remembered library with `openExisting`, so a moved one fails with the reason and the part 2 notice (Open library…, Open the default library), never falling back to the default or creating an empty library;
  - `--remember` arms `rememberWhenOpened`;
  - the `QSettings` is declared before the library session.
- **What gets remembered:** the in-app starts pass `--remember` (the switcher's Open library… and Open the default library, and **Open restored library**). A `--library` shortcut does not, so it never replaces the everyday library.
- **The tooltip's source text:** "the library you opened last".
- **Docs:**
  - USER_GUIDE.md (the next start, shortcuts, two windows, a moved remembered library; moving folders into place is no longer needed);
  - BUILDING.md (the order, `--remember`), UI.md, DECISIONS.md.

**Tests:**
- `tst_librarycontroller::theRememberedLibraryOpensNext`: the remembered library comes after `--library` and the environment and before the default; a remembered default is the default; nothing remembered is the default. It is a parameter, so the test never reads the real settings.
- `tst_librarycontroller::onlyAnOpenedLibraryIsRemembered` (settings in a temporary INI file, never the registry):
  - nothing is remembered while the library opens, and then it is;
  - a library that fails to open is not remembered, and the last good one stays;
  - the value is read back from the file, and `forget()` clears it.
- `tst_libraryswitcher`: the in-app starts carry `--remember`, and the tooltip text for a remembered library.

**Checked end to end** with the Release app, offscreen:
1. `--library <restored>` alone remembers nothing.
2. `--library <moved> --existing-library --remember` fails, remembers nothing and creates no folder.
3. `--library <restored> --existing-library --remember` writes `library/last` with that folder.

The machine's settings key did not exist before; it was removed afterwards. **A start without `--library` was not run here**, because this machine's default library holds real books. That path is covered by `theRememberedLibraryOpensNext` and the six-line `main.cpp` wiring.

**Verification:** `pwsh scripts/verify.ps1` Release and `-Configuration Debug` both **passed**: 186 text files, the guard tests, 33/33 tests and the smoke checks.

**Merged:** [PR #34](https://github.com/PantaKoda/MyBooksLibrary/pull/34), merge `307c8ad` (2026-10-02), after review and CI on the head `95be063`, which had `main` merged in. It closed issue #30. **Remaining:** a manual check by the owner. Open a restored library with **Library → Open library…**, close the app, and start it with no arguments.

### Open library… (presentation, A2), issue #30 part 2

**Asked in [issue #30](https://github.com/PantaKoda/MyBooksLibrary/issues/30):** the app always started with the default library, and a restored library could be reached only through a `--library` shortcut or by moving folders. Users who restarted the app thought the restore was lost. Stacked on part 1.

**Change:**
- **Library → Open library…** (a folder dialog), then **New window** or **Instead of this library**, and **Library → Open the default library**.
- **The new library always starts in a new process**, as a restored library already did (DECISIONS.md): `appMyBooksLibrary --library <folder> --existing-library`. "Instead" then closes this window through its normal closing flow, which stops work first. **Open restored library** now passes `--existing-library` too.
- **`LibrarySwitcher`** (presentation, owned by `LibraryController` as `switcher`):
  - it checks the chosen folder off the GUI thread and refuses the library this window holds (real paths);
  - it starts the process through a launcher that tests replace;
  - it gives the window's title (`currentName`), the path tooltip and `currentIsDefault`.
- **`catalog::Library::OpenMode::ExistingOnly` and `Library::checkExisting`:** a missing folder, a file, a folder without `library.sqlite` (Documents, the folder around a library, a library's own `files` folder) or a backup is refused with the reason. **Nothing is created, locked or opened.** `LibraryController::openExisting` and `--existing-library` use it; the default folder, `--library` and `MYBOOKSLIBRARY_ROOT` still create a library on first use.
- **The window:**
  - its title is "*folder* – MyBooksLibrary";
  - the toolbar's path has a tooltip with the whole path and how the library was chosen;
  - a library that could not be opened shows the reason (`LibraryController::openError`), with **Open library…** and **Open the default library**. Before, only a status line showed it, and every command was disabled.
- **`app::defaultLibraryRoot()`**, for **Open the default library**.
- **Docs:**
  - USER_GUIDE.md: a new "Opening another library" section, and the backup section, limitations and questions updated;
  - UI.md, BUILDING.md (`--existing-library`), DECISIONS.md.

**Tests:**
- `tst_libraryfolder` (new, catalog):
  - an existing library, with a non-ASCII name and written through `..`, is accepted and opens;
  - a missing folder, an empty folder, a file, the folder around a library, a library's `files` folder and a backup are each refused with the reason, and left exactly as they were: a missing folder is not created, and no lock or catalog appears;
  - create-if-missing still makes a library.
- `tst_libraryswitcher` (new, presentation, with a recording launcher):
  - **Accepted:** another library, given as a URL or a path, starts with `--library <folder> --existing-library`, for a new window or instead of this one, and this session is untouched.
  - **Refused, and nothing started:** no folder, a relative path, a missing folder (not created), Documents, a library's `files` folder, a backup (unchanged), and the library this window holds, written through `..` in upper case. A launcher failure says so.
  - **The default library** starts without `--existing-library`, and is refused when this window holds it.
  - **The title and tooltip texts.**
  - **`openExisting` on a moved library** fails with the reason, creates nothing, and another library can still be opened.
  - **The real `OpenLibraryDialog.qml`** at the window's minimum size: a refusal shows in the dialog, which stays open; **New window** starts the library and closes the dialog; **Instead of this library** also asks the window to close.
- **The app** (Release): `--library "D:\Books\My library" --existing-library` shows the notice with the reason and creates nothing (screenshot in UI.md).

**Verification:** `pwsh scripts/verify.ps1` Release and `-Configuration Debug` both **passed**: 184 text files, the guard tests, 33/33 tests and the smoke checks.
- The first Debug run stopped in the build: two test targets' `pdfbookmark_deploy_runtime` steps copied the OCR models into the same folder at once ("Permission denied" on `models/rec/charset.txt`).
- This is a race between the existing targets; none of the new tests deploys the SDK runtime. The rerun passed.

**Merged:** [PR #33](https://github.com/PantaKoda/MyBooksLibrary/pull/33), merge `41e7dec` (2026-10-02), after review and CI on the head `3f463fc`, which had `main` merged in. The review's nits remain open:
- a window started by **Open library…** describes its source as "opened with --library";
- `openError` is never cleared;
- the toolbar overflows at the minimum width, as it did before this PR.

### A backup folder opened as a library (A2), issue #30 part 1

**Found in the investigation of [issue #30](https://github.com/PantaKoda/MyBooksLibrary/issues/30):** a backup folder has a library's layout, and nothing stopped `Library::open` from opening one, for example through the user guide's `--library` shortcut pointed at the wrong folder. The session sets `journal_mode = WAL`, which is stored in the catalog's header, so the backup's catalog no longer matches its manifest and **Restore** refuses it ("library.sqlite in the backup has changed since it was saved"). Recovery, jobs and imports also write into the folder.

**Fix:**
- `catalog::Library::open` refuses a folder holding `backup.json` (`Library::kBackupManifestFileName`) before it creates, locks or opens anything. The message says it is a backup and points to **Backup → Restore a backup…**. The window shows it as "The library could not be opened: …".
- `storage/backup.cpp` takes the manifest's file name from that constant, so the two cannot drift apart.
- USER_GUIDE.md: the shortcut advice says to use the restored folder, not the backup, and the questions explain the message. STORAGE.md describes the rule.

**Tests:**
- `tst_backup::aBackupIsNeverOpenedAsALibrary`: `Library::open` on a fresh backup, and on the same folder written through `..`, fails with `InvalidArgument`. The folder's entries and the catalog's SHA-256 are unchanged (no lock, WAL or shared-memory file), and the backup still verifies and restores.
- `tst_librarycontroller::aBackupFolderIsNotOpened`: a backup made through the window's backup session, then opened by a second session as `--library` would. The session fails with the reason, and the backup still verifies.
- **Control run:** with the check disabled, both tests fail: the backup opens (`'!opened' returned FALSE`, `'second.failed()' returned FALSE`).

**Verification:** `pwsh scripts/verify.ps1` Release and `-Configuration Debug` both **passed**: 179 text files, the guard tests, 31/31 tests and the smoke checks.

**Merged:** [PR #32](https://github.com/PantaKoda/MyBooksLibrary/pull/32), merge `e3539ed` (2026-10-02), after review and CI on the head `c74a4bb`. The first CI run lost its runner during packaging and was re-run. The review's nit remains open: the refusal points to **Backup → Restore a backup…**, which is disabled in a window whose library failed to open.

### The contents tree hidden by a long list of reasons (presentation)

**Found by the owner:** a 746-page book (*Simulation Modeling and Arena*) seemed to have no contents. The analysis had published 221 entries, 204 with a confirmed page, and indexed all of them for search. But the Contents tab listed every analysis reason as a "Why:" line (18 here, one per entry without a page). At the default window size those lines pushed the tree out of view.

**Fix:**
- `BookInspector::contentsNotes` keeps only the short counts.
- The analysis's reasons are a separate `contentsReasons`, shown on request (**Why? (*n*)**) in a scrolling popup over the pane. Following review of [PR #27](https://github.com/PantaKoda/MyBooksLibrary/pull/27), it is a popup rather than an area in the layout, so the reasons never move the tree, even at the minimum window size. The popup is headed "What the analysis reported:", since edits made later are not reflected in it.
- The "Ready / Not ready for a bookmarked copy" line is gone. Since M09 the Export dialog bookmarks every entry with a confirmed page, so it was misleading.

**Tests:**
- `tst_inspectorpane::manyReasonsLeaveTheTreeInView`: 80 entries and 40 reasons. In a 640×640 pane the tree is in view with at least 120 px. Opening and closing **Why?** leaves the tree exactly where it was, at 640×640 and at 300×300. With the first layout, the tree started at y 1184 and the test fails.
- `tst_librarycontroller` checks that the notes carry only counts.

**Checked on the owner's book**, in a copy of the library: the tree shows Preface (page 17), Acknowledgments, Introduction, "1 Simulation Modeling"…

**Verification:** `pwsh scripts/verify.ps1` Release and `-Configuration Debug` both **passed**, 31/31 tests.

**Also seen, not a bug:** the book's title is "uncertain", because the SDK found several candidates. The app shows the file name rather than guess. The owner can pick or type it with **Correct**.

**Merged:** [PR #27](https://github.com/PantaKoda/MyBooksLibrary/pull/27), merge `adcf7b2`, in **v0.1.0**.

### A user guide

**Asked by the owner:** a guide to what the window's features do, for someone who does not know the app, and to its limitations.

- **`docs/USER_GUIDE.md`** covers:
  - getting started, and where the library lives;
  - a tour of the window;
  - adding books, and what happens automatically (and how long it takes);
  - a book's details (Title and authors, corrections; Contents, pages, editing);
  - searching (what is and is not searched, and query tips), reading, collections and Trash;
  - saving a copy with bookmarks, backing up and restoring, the Activity panel, and closing;
  - **limitations**, and **questions and problems**.
- Its labels and rules were checked against the QML, `BookInspector` and SEARCH.md.
- **`scripts/package.ps1`** ships it as `USER_GUIDE.md` next to the app, and the `v0.1.0` release notes point to it.
- **A `README.md`** for the repository's front page (there was none): what the app is, the Releases page, the guide, and the developer docs.
- **Review fixes ([PR #29](https://github.com/PantaKoda/MyBooksLibrary/pull/29), review of `bb7d70a`):**
  - **Reopening a restored library later:** the app has no *Open library…* command, so the guide now explains a shortcut with `--library "<folder>"`, or moving the folder into place while the app is closed. It is listed under Limitations and in the questions.
  - **The Media Feature Pack:** its location for Windows 11 (*Settings → System → Optional features*) as well as Windows 10, in the guide and the release notes.
  - **The reader:** it has no zoom and no search inside a book, stated plainly.
  - **Closing:** it covers a restore in progress as well as a backup.
  - **The diagram's status line:** it matches the app.
  - **The release notes:** open `USER_GUIDE.md` with Notepad if Windows asks.
  - **Follow-up worth an issue:** an *Open library…* command in the app.

**Verification:** `pwsh scripts/package.ps1 -SdkDir <sdk>` **passed**, with `USER_GUIDE.md` in the package. `pwsh scripts/verify.ps1` **passed**: 177 text files, 31/31 tests.

**Merged:** [PR #29](https://github.com/PantaKoda/MyBooksLibrary/pull/29), merge `e142797`, in **v0.1.0**. The follow-up it named became [issue #30](https://github.com/PantaKoda/MyBooksLibrary/issues/30), done by PRs #32–#34.

### Contents without pages when the printed numbering skips pages (A4, presentation)

**Found by the owner** in the v0.1.0 release: the contents of *Computational Physics* (Springer, 2017, 640 pages) were "trash".
- The 378 entries were parsed well: titles, levels and printed page numbers were right.
- **None had a page:** 372 were `ambiguous` and 6 `unresolved`.
- Each row listed up to 40 possible pages ("Page 24 or 25 or 33 or …"), which pushed the titles out of view.

**Cause:**
- The PDF leaves out the blank pages of the printed book. The offset between physical and printed pages falls from 21 (chapter 1) to 6 (the index).
- SDK 0.3.0 maps with one decimal numbering section and one offset by default. Its anchors disagreed ("Conflicting observed offset"), so it placed nothing.
- The PDF's page labels record every skip, in 20 label runs.
- The SDK accepts caller-supplied numbering sections and entry associations (`AnalysisOptions::sections`, `entry_sections`), which the app did not use.

**Fix (A4, SDK boundary):** `src/processing/sdk/pagelabels.{h,cpp}` and `SdkContentsAnalyzer` (PROCESSING.md, "Numbering sections from the PDF's page labels"; DECISIONS.md).
- If the analysis leaves entries without a page and the page labels show a break in the numbering, a second analysis runs. It has a numbering section per label run, and each entry is associated with the run holding its printed page.
- Its report is kept only if it has the same entries and places more of them. `"page_label_sections"` in the run's options records the outcome.
- Qt PDF reads the labels on the worker, between SDK calls.

**Fix (presentation):** an ambiguous entry's row lists at most 3 possible pages, else "Page uncertain (*n* possible)". The details list up to 10 of them. The page text takes at most half the row, so the title always shows.

**Tests:**
- `tst_sdkcontentsanalyzer`, 6 new cases:
  - label runs, including roman, prefixed and restarted numbering;
  - Qt PDF's labels of the new fixture, and of a PDF without labels;
  - sections and associations from a constructed report (unique runs only; uncertain and unnumbered entries skipped; other options kept);
  - the second report is kept only when it places more of the same entries;
  - `analyze` and `analyze_book` on the new fixture `dropped-pages-book.pdf`, which places all 5 entries with `page_label_sections: "used"`;
  - a cancel during the second analysis cancels.
  - With the second analysis disabled, the two fixture cases fail.
- `tst_toctreemodel`: 3 possible pages are listed, 12 are counted, and the details list them.
- `tst_inspectorpane::uncertainPagesLeaveTheTitleInView`: an entry with 40 possible pages keeps its title in view at 640 and 300 px. It fails without the width limit (page text 336 of 596 px).

**Checked on the owner's book** (the app built from this branch, a new library, `--import` of the book):
- **351 of 378 entries have a page**; 13 are ambiguous and 14 unresolved. It took 4.3 s in all.
- Every placed page's label equals the entry's printed number: "1 Error Analysis" (printed 3) → page 25, "5.1 Gaussian Elimination Method" (64) → page 85, which shows printed 64.
- Searching "Gaussian elimination" finds 5.1 at page 85 with **Open**.
- The v0.1.0 package on the same book shows 0 of 378.

**Also checked:** a throwaway harness ran the same two-step logic over the 84 PDFs in the owner's book folder, without OCR.
- The second analysis ran on 13 of them, and never placed a page whose label differs from the printed number.
- It placed many more entries on 8. Examples: *Numerical Python in Astronomy* 0 → 41 of 41, *Network Programming with Go* 0 → 325 of 325, *Pro C# 10* 0 → 631 of 1367, *Pro Cryptography* 0 → 197 of 231.
- It placed as many on 4, and 2 fewer on 1 (`NMFSC.pdf`, 133 → 131), where the first result is kept.

**Limitations and follow-ups (SDK, PantaKoda/PDFMegine):**
- **The second analysis reads the pages again.** The SDK takes entry associations only by entry ID, known only after parsing. An engine that derived sections from the page labels itself, or took associations by printed page, would need one run.
- **Two chapters in one label run:** chapters 10 and 11 of the owner's book share a run and each has a "Problems" heading. S4 then sees conflicting heading anchors, so their 25 entries stay without a page.
- Books analyzed with 0.1.0 keep their result until **Analyze contents again** (USER_GUIDE.md, "Questions and problems").
- Many other books in the folder place few or no entries for other reasons (e.g. *The Linux Programming Interface*, 0 of 971). The labels do not show a break there; not investigated here.

**UI evidence:** `docs/images/m05-page-labels-before.png` (the v0.1.0 package) and `m05-page-labels-after.png` (this branch), both of `dropped-pages-book.pdf`: `appMyBooksLibrary --library C:\MBL-demo-PageLabels --import tests\fixtures\dropped-pages-book.pdf --inspect-first --screenshot <png>`. Before: "5 contents entries, 0 with a confirmed page". After: "every page confirmed", pages 4, 11, 13, 18 and 25.

**Verification:** `pwsh scripts/verify.ps1 -SdkDir <sdk>` (Release) **passed** at `ce28fd6`: 181 text files, 31/31 tests, smoke checks. `-Configuration Debug` **passed** at the same commit: 31/31 tests, smoke checks. Only documentation and these images changed after it.

**Merged:** [PR #31](https://github.com/PantaKoda/MyBooksLibrary/pull/31), merge `edad42c` (2026-10-02). It was merged after issue #30's PRs, so `main` was merged into it first (`e50a7c6`). The only conflict was in DECISIONS.md, where both sides had added entries, and both are kept. CI passed on that head. **Follow-up from the review (not blocking):** the second report is kept when it places more entries in total. It can therefore drop a page the first analysis confirmed: an entry whose printed page lies in no label run, or in two runs, gets no association. Keeping it only when no confirmed page is lost would be safer.

## Releases on GitHub

**Asked by the owner (2026-09-29):** a Releases page to download the Windows app, with every change going through a PR and CI, and changes grouped into releases.

**Scope** (branch `ci/m10-release-workflow`):
- **`.github/workflows/release.yml`:** a pushed `vX.Y.Z` tag checks that the tag is on `main`, matches `CMakeLists.txt`'s version, and has notes in `docs/releases/`. It then runs `verify.ps1` and `package.ps1`, and publishes the release with `MyBooksLibrary-X.Y.Z-win64.zip`, its `.sha256`, and the notes followed by the merged PRs. It uses `gh` with the job's token and `contents: write`; there is no new action.
- **`.github/actions/setup`:** Qt and the SDK, pinned once, and used by CI and the release, so they cannot drift.
- **CI also packages on every PR** (`package.ps1 -SkipZip`), so a change that breaks packaging fails its PR, not a release.
- **`third_party/licenses/`:** pinned copies of Qt's licence text and PaddleOCR's Apache-2.0 licence, the defaults for `package.ps1`. CI's Qt has no `Licenses` folder, and the SDK lacks the models' licence (PDFMegine#6).
- **Release notes and process:** version **0.1.0** in `CMakeLists.txt`, with its notes in `docs/releases/v0.1.0.md`. `docs/RELEASING.md` describes the process, and AGENTS.md §13 has a short "Releases" rule.

**Verification:** Locally: `pwsh scripts/package.ps1 -SdkDir <sdk>` with the pinned licences (no other options) **passed**, and wrote `MyBooksLibrary-0.1.0-win64.zip`. `pwsh scripts/verify.ps1` **passed** (170 text files, 31/31 tests). The workflows are exercised by this PR's CI (the setup action and the package step). `release.yml` runs only on a tag, so it is first exercised by the first release.

**Review fixes ([PR #28](https://github.com/PantaKoda/MyBooksLibrary/pull/28), review of `a61a92f`):**
- **Must fix, CI failed:** the new package step's OCR check timed out on the 2-core runner. It needs about 370 s there, and every check had 300 s.
  - Each check now has its own limit: 900 s for the OCR check, 300 s for the others.
  - The OCR check skips the Qt-only control phase (`--no-control`), which runs as long as the SDK phase and is already covered by the text check.
  - Locally the check now passes in about 2 minutes, with two phases.
- **Should fix, the release notes' wording:** "replace this folder" could read as the library folder. The notes now say the library lives separately, give its real path (`%LOCALAPPDATA%\MyBooksLibrary\MyBooksLibrary\Library`), and say to replace only the app's folder, backing up first if wanted.
- **Hardening:** the tag reaches the release scripts through `env:`, never pasted into the script text.
- **Retry after a failed publish:** the release is created as a **draft**, both files are checked as uploaded, and only then is it published. docs/RELEASING.md says to delete a left-over draft (`gh release delete`) before tagging again.

**Merged:** [PR #28](https://github.com/PantaKoda/MyBooksLibrary/pull/28), merge `4a7dda1`. **v0.1.0** was tagged at `e142797` (after [PR #29](https://github.com/PantaKoda/MyBooksLibrary/pull/29)) and published on 2026-09-29 with `MyBooksLibrary-0.1.0-win64.zip` and its `.sha256`. Notes for the next release collect in `docs/releases/UNRELEASED.md` (RELEASING.md).

## M10 — Windows release

M10 is split in three:
- **Part 1** (A1, A2): backup and restore, verified.
- **Part 2** (packaging): a clean packaged runtime with models, SQL, the reader and the notices; startup outside the build tree.
- **Part 3** (presentation): Back up… and Restore… in the window.

### Part 1: backup and restore (A1, A2)

**Scope:**
- **`catalog::snapshotCatalog` (`catalog/backup.*`, A2):** on the database thread, lists every asset, with its recorded digest and size, and every report a run references. It then writes the catalog with **`VACUUM INTO`**, a consistent copy of the committed state that includes what is only in the WAL. Nothing else runs on that connection in between.
- **`storage::createBackup` (`storage/backup.*`, A1):**
  - writes "MyBooksLibrary backup *date time*" in a chosen folder outside the library;
  - copies every managed source (trashed books included) and every referenced report with SHA-256. A source whose bytes no longer match its record **stops the backup**, rather than copying damage. A missing report is listed, not fatal;
  - writes the manifest `backup.json` (format 1, schema version, each file with size and digest). The catalog copy is switched to rollback journaling, so it can be read without side files;
  - **verifies everything**, then renames the hidden `.partial` folder into place. A cancelled or failed backup leaves nothing.
- **`storage::verifyBackup`:** the manifest (whose paths must stay under `files/` or `reports/`), every size and digest, and the catalog's integrity check and schema version.
- **`storage::restoreBackup`:**
  - restores into a **new or empty folder only**, never the library in use or a folder inside the backup;
  - verifies first, and writes nothing if that fails;
  - copies through a hidden `.restoring` folder with every digest checked, and puts it in place;
  - then opens it as a library (migrating an older catalog) and runs the integrity check.
- **Why not quiesce jobs:** the catalog snapshot is one database task, and every catalog write goes through that thread. Managed sources and reports are immutable, and a referenced one is never removed (no permanent deletion yet). A backup is therefore consistent while work runs; see DECISIONS.md.
- `storage::isInsideFolder` (A1) exposes the real-path check the export rules use.

**Touched paths:** `src/catalog/backup.*`, `src/storage/{backup.*,exportdestination.*}`, `CMakeLists.txt`, `tests/storage/tst_backup.cpp`, `tests/CMakeLists.txt`, `docs/`.

| Command | Result |
| --- | --- |
| `pwsh scripts/verify.ps1` (Release) and `-Configuration Debug` | **Passed** in both configurations: whitespace and text checks, guard tests, configure, build, **30/30 tests** (ctest), and the application smoke checks. Verified on the working tree based on `1dbbe0c`. |
| `tst_backup` (new, 5 cases, real files and fixtures) | **Backup and restore:** a library with a correction, contents, a report, a collection and a book in Trash is backed up, with no partial folder left. A second backup gets its own name. It is restored as a new library while the original stays open, with the correction, contents, chapter search for "TCP/IP", Trash, the collection, the source digest and the report all kept.<br>**The latest change:** a title change still only in the WAL is in the backup.<br>**Damage and tampering:** a changed managed source stops the backup with nothing left. A report changed in the backup, by size or by bytes at the same size, is refused by verify and restore, which writes nothing. A manifest naming `../../escape/…` is refused before any copy. A library folder is not a backup.<br>**Cancel and refusals:** a cancelled backup or restore leaves nothing, and an empty target stays empty. Refused: backup inside the library (root or `files/`), a missing or relative folder; restore into the library in use (not empty), inside the backup, or a relative path.<br>**Missing report:** listed, not fatal, and kept in the manifest. |

**Control runs:**
- with the catalog copied as a file instead of `VACUUM INTO`, the copy has **schema version 0**, because everything was still in the WAL, and the tests fail;
- with a source's digest not checked, the damage test fails;
- with the manifest path check removed, the escape test fails. The restore is still refused, as a missing file, and nothing is written outside.

**Review fixes ([PR #24](https://github.com/PantaKoda/MyBooksLibrary/pull/24), review of `f811cae`):**
- **Should fix, restoring inside the open library:** a new folder inside the library in use was accepted, including under `staging/`, which that library's next start removes. `restoreBackup` now takes `librariesInUse` and refuses a target inside or around any of them, on real paths. Covered by `tst_backup::neverRestoredInsideTheLibraryInUse`: `restored`, `files/`, `staging/`, `reports/` and a `..` spelling are refused, and nothing is created.
- **Should decide, waiting exports:** an export waiting at backup time ran again in the restored library, perhaps on another machine. Decision: they are closed as not written (`cancelled` / `restored`) by `catalog::closeExportsAfterRestore` when the restore opens the library. Covered by `waitingExportsAreClosedByARestore`: none is open after recovery, the record says not written, and the library in use keeps its own request.
- **Hardening, complete backups:** `verifyBackup` now also checks that every source the catalog references is in the manifest with the digest the catalog records, and every report is listed or listed as missing. Covered by `aBackupMissingANeededFileIsRefused`: a source removed together with its manifest entry is refused, and nothing is restored.
- **Controls:** with each fix undone in one build, its test fails.
- **Verification after the fixes:** `pwsh scripts/verify.ps1` Release and `-Configuration Debug` both **passed**, 30/30 tests.

**Not in this part:** the packaged runtime (part 2), and the window's commands (part 3).

**Next action:** merged ([PR #24](https://github.com/PantaKoda/MyBooksLibrary/pull/24), merge `0c682c3`).

### Part 2: the Windows package

**Scope:**
- **`scripts/package.ps1`** builds a Release app and stages `build\package\MyBooksLibrary\`, then zips it:
  - the executable with the SDK runtime and OCR models;
  - Qt through `windeployqt`, with only the SQLite driver and without software OpenGL, D3D/DXC compilers, QML debugging plugins or translations;
  - the Visual C++ runtime, app-local;
  - the notices (docs/BUILDING.md, "Windows package").
- **Notices are complete by construction:** the Qt module of every shipped Qt file is looked up (qtbase, qtdeclarative, qtpdf, qtsvg), and its SBOM is included with Qt's licence text and the SDK's licences. A file of an unknown module stops the script. This caught `dxcompiler.dll` (Microsoft's shader compiler) and Qt Quick 3D pulled in by the debugging plugins; both are now left out.
- **Checked outside the build tree:** the package is copied to a temporary folder and run with `PATH` reduced to Windows' own folders, no Qt variables, and the real platform. It runs the SDK check, the FTS5 probe, the reader checks (text, and OCR with the packaged models), and the window: import, read, save a copy with bookmarks, close. Each run has a time limit, because a GUI-subsystem program can hang on an error dialog.
- **`--export-first <pdf>`** (development flag): when idle, the first book is saved as a copy with bookmarks through the Export dialog's session, and the app exits 1 unless it was saved. `verify.ps1`'s window check now uses it too.
- **`scripts/toolchain.ps1`:** the toolchain setup (MSVC, CMake, Ninja, Qt and SDK checks), moved out of `verify.ps1` so both scripts use it.

**Touched paths:** `scripts/{package.ps1,toolchain.ps1,verify.ps1}`, `tools/test_verify_guards.ps1`, `main.cpp`, `docs/`; for the CI fix, `qml/inspector/MetadataFieldEditor.qml` and `tests/presentation/tst_inspectorpane.cpp`.

| Command | Result |
| --- | --- |
| `pwsh scripts/package.ps1 -SdkDir <sdk>` | **PACKAGE PASSED**: 323 MB, zip 166 MB; Qt modules qtbase, qtdeclarative, qtpdf, qtsvg.<br>**From the copy in `%TEMP%`:** `sdk-check`, `sqlite-check` (fts5, bm25, remove_diacritics, prefix), and `reader-check` on `contents-book.pdf` (all PASS; `pdfquickplugin` and `Qt6PdfQuick` loaded from the package).<br>**OCR with the packaged models** on `image-only.pdf`: 4 pages OCR'd, 0 failed, peak 2.4 GB; all checks PASS.<br>**The window:** import, page 15, `export=saved` to "Βιβλίο (bookmarked).pdf" (10 404 bytes), `close: reader.open=0`. |
| `pwsh scripts/verify.ps1` (Release) and `-Configuration Debug` | **Passed** in both configurations: guard tests, whitespace and text checks, configure, build, **30/30 tests**, and the smoke checks. The window check now reports `export=saved`. Verified on the working tree based on `0c682c3`. |

**Review fixes ([PR #25](https://github.com/PantaKoda/MyBooksLibrary/pull/25), review of `390408b`):**
- **Should fix before a release, the OCR models' licence:** the PaddleOCR PP-OCR models (Apache-2.0) were shipped without their licence, because SDK 0.3.0's install silently skips it (reported as [PantaKoda/PDFMegine#6](https://github.com/PantaKoda/PDFMegine/issues/6)).
  - The script now maps each shipped SDK DLL to its licence files, and requires the models' licence. `-ModelsLicenseFile` supplies it until the SDK does.
  - An unmapped SDK DLL, or a missing licence, stops the script. Without `-ModelsLicenseFile` the run fails at once with the reason; with it, `NOTICE.txt` names the models and their licence.
- **Record and report, Windows N editions:** `opencv_world500.dll` imports Media Foundation directly, so the app does not start on N editions without the Media Feature Pack. This is reported as [PantaKoda/PDFMegine#7](https://github.com/PantaKoda/PDFMegine/issues/7), and stated in `NOTICE.txt` and BUILDING.md.
  - As the reviewer suggested, the package now **scans every shipped binary's imports** (`dumpbin /dependents`). An import that is neither shipped nor a known Windows component stops it. Media Foundation is reported, and delay-loaded imports only warn.
- **Nit, `--export-first` with no book:** it now fails at once ("export=NOT SAVED: the library has no book") instead of waiting forever.
- **Verification after the fixes:** `package.ps1 -ModelsLicenseFile <PaddleOCR LICENSE>` **passed**. The imports scan reported only Media Foundation, and the smoke checks passed. Without `-ModelsLicenseFile` it stops with the reason. **Control:** with `kernel32` left off the Windows allowlist, it stops ("generic\qtuiotouchplugin.dll needs kernel32.dll…"). `--export-first` on an empty library exits 1 in 1.1 s. `verify.ps1` Release and Debug both **passed**, 30/30.

**CI failure on the first head (`f9769c8`), twice:** `tst_inspectorpane::correctionDialogSavesAndSurvivesRefreshes` found the last of 27 contributor rows outside the scroll area. This was a real race in `MetadataFieldEditor.qml`, not caused by this PR, but exposed by a slower runner:
- After **Add**, the list scrolled to its end once (`Qt.callLater`). When the layout settled later, the new row ended below the visible area.
- Now the list follows its end while the rows or their view change size after Add, and stops as soon as the user scrolls.
- The test also shrinks the rows' visible height after Add. With the fix disabled it fails like CI did (last row at y 565, rows ending at 375); with the fix it passes.

**Found while packaging:** with `QT_QPA_PLATFORM=offscreen`, the packaged reader check waited on Qt's "no platform plugin" message box. The package ships only the Windows platform plugin, as users need. The checks now use the real platform and a time limit.

**Not in this part:**
- **CI does not build the package yet.** Its Qt comes from `aqtinstall`, which does not install `C:\Qt\Licenses`; `-QtLicenseFile` would need a pinned copy of the licence text.
- **No installer and no code signing:** the zip is the release artifact.
- **The owner's manual check of the zip** on a machine without Qt or Visual Studio is still to do.
- Backup and restore in the window come in part 3.

**Next action:** merged ([PR #25](https://github.com/PantaKoda/MyBooksLibrary/pull/25), merge `54ed8fc`).

### Part 3: Back up… and Restore… in the window (presentation)

Independent of part 2 (packaging, [PR #25](https://github.com/PantaKoda/MyBooksLibrary/pull/25)), so it is branched from `main` after part 1.

**Scope:**
- **`BackupController`** (`src/presentation/backupcontroller.*`), owned by `LibraryController` as `backup`:
  - runs `storage::createBackup` and `restoreBackup` (part 1) on its own one-thread pool, with progress and the result delivered through queued calls, as plain-language status text;
  - restore passes the open library's root as `librariesInUse`, so a target inside or around it is refused;
  - a running backup or restore makes the session busy: `prepareToClose()` cancels it, and the window waits;
  - `openRestoredLibrary()` starts MyBooksLibrary with `--library` on the restored folder in a new window.
- **`BackupDialog.qml`** (`qml/backup/`), from the toolbar's **Backup** menu: back up to a folder (Documents by default), or restore a backup as a new folder "MyBooksLibrary restored *date*". It has progress, the result, Show folder, Open restored library, and Cancel.

**Touched paths:** `src/presentation/{backupcontroller.*,librarycontroller.*}`, `qml/backup/BackupDialog.qml`, `Main.qml`, `CMakeLists.txt`, `tests/presentation/tst_backupcontroller.cpp`, `tests/CMakeLists.txt`, `docs/`.

| Command | Result |
| --- | --- |
| `pwsh scripts/verify.ps1` (Release) and `-Configuration Debug` | **Passed** in both configurations: guard tests, whitespace and text checks, configure, build, **31/31 tests** (ctest), and the smoke checks. Verified on the working tree based on `0c682c3`. |
| `tst_backupcontroller` (new, 4 cases, real library) | **Off the GUI thread:** running and busy while it works, then "Backed up 1 book (…) to …" with progress, a folder URL accepted, and only the backup in the folder. The restore says "Restored 1 book …" and the new library has its catalog. After reset, nothing is left to open.<br>**Refusals, in words:** backup inside the library folder; restore under the open library's `staging/` (not created); a folder that is not a backup.<br>**Closing:** `prepareToClose()` during a backup waits until it has stopped, and nothing partial is left.<br>**The real `BackupDialog.qml`:** Documents suggested; **Back up** writes the backup and shows "Backed up". The restore mode suggests a new folder, is disabled until a backup is chosen, restores, and shows **Open restored library**. |

**Review fixes ([PR #26](https://github.com/PantaKoda/MyBooksLibrary/pull/26), review of `87aadf2`):**
- **Dismissed while a job ran:** Escape or a click outside closed the dialog during a backup or restore. Reopening it with the other menu item then showed that job under the other mode's fields. Now `closePolicy` is `NoAutoClose` while a job runs, and both menu items show the running job's mode with its fields as they were.
- **A second restore the same day:** it was refused because the suggested "MyBooksLibrary restored *date*" folder already existed and was not empty. The suggestion is now the first free name ("… (2)", …), as for backups.
- **Tests:** `tst_backupcontroller` checks the close policy and the reopened mode while a backup runs, and restores twice the same day into the suggested folders. With each fix undone, its test fails.
- **Verification after the fixes:** `pwsh scripts/verify.ps1` Release and `-Configuration Debug` both **passed**, 31/31 tests.

**UI evidence:** `docs/images/m10-backup-dialog.png` (UI.md).

**M10 gate** (after parts 1–3 are merged):
- **Backup and restore verified:** part 1's tests, and this window's.
- **A clean packaged runtime with models, SQL and the reader:** part 2's `package.ps1` checks.
- **Responsive shutdown and recovery:** closing waits for running work without blocking the GUI thread. This now includes a backup or restore.
- **Dependency notices:** part 2.
- **Still open:** the owner's manual check of the package on a clean machine.

**Next action:** merged with [PR #26](https://github.com/PantaKoda/MyBooksLibrary/pull/26). M10 is complete, apart from the owner's manual check of the package on a clean machine. M11 (native macOS and Linux) needs native SDK packages first.

## M09 — Export

M09 is split in three:
- **Part 1** (domain + A1 + SDK boundary): the export core.
- **Part 2** (A2 + A4): export records and durable export jobs on the SDK worker.
- **Part 3** (presentation): the Export dialog and the state it shows.

### Part 1: the export core (domain, A1, SDK boundary)

**Scope:**
- **`domain/export.*`:** `buildExportPlan` turns a book's effective contents (edits included) into bookmarks.
  - Entries with a confirmed page become bookmarks.
  - Others are **omitted**, with the reason ("No page was found for it.", "More than one possible page; none was chosen.").
  - An entry whose parent has no bookmark goes to its nearest kept ancestor or the top level, recorded as a **promotion**. An uncertain level goes to the top level, recorded in **`uncertainLevels`** (not a promotion: it has no parent to move from).
  - Entries the user removed are not bookmarks, and are counted rather than listed as missing.
  - `complete()` is true only when nothing was omitted or moved.
  - It fails when the page count is unknown, a page is outside the book, or nothing can be a bookmark.
- **`storage/exportdestination.*` (A1):**
  - Accepted: an absolute `.pdf` path in an existing folder.
  - Never allowed: anywhere inside the library folder, however it is written (case, `..`, short names and links are resolved with `std::filesystem::canonical`, for the folder and again for an existing file), and never a managed source or an imported original under any name (`std::filesystem::equivalent`, so hard links are caught), even when replacing.
  - Also refused: an existing file with other names (hard links), which may be an internal file such as the catalog; a library folder that cannot be resolved (fails closed); and on Windows a `:` in the name (a stream inside another file).
  - An existing file is refused (Duplicate) unless replacing is asked for.
  - `suggestedExportPath` gives a sanitized "*title* (bookmarked).pdf" that does not collide.
- **`processing/bookexporter.h` and `processing/sdk/sdkbookexporter.*` (SDK boundary):**
  - The plan becomes a `pdfbookmark::BookmarkPlan` bound to the source digest and page count, with omissions and promotions kept; `validate_plan` checks it, and `apply` writes the copy (replacing an existing file only when asked).
  - The result reports `committed` exactly as the SDK does, with the verification (outline items, pages, structure matches, source unchanged) and the plan JSON to keep.
  - A cancel counts only before the commit. Failures carry the SDK's error code and a plain-language message.

**Touched paths:** `src/domain/export.*`, `src/storage/exportdestination.*`, `src/processing/bookexporter.h`, `src/processing/sdk/sdkbookexporter.*`, `CMakeLists.txt`, `tests/domain/`, `tests/storage/`, `tests/processing/`, `tests/CMakeLists.txt`, `docs/`.

| Command | Result |
| --- | --- |
| `pwsh scripts/verify.ps1` (Release) and `-Configuration Debug` | **Passed** in both configurations: whitespace and text checks, guard tests, configure, build, **25/25 tests** (ctest), and the application smoke checks (SDK call, FTS5, Qt PDF coexistence, shutdown during analysis, non-ASCII paths). Verified on the working tree based on `c182ec3`. |
| `tst_exportplan` (new, 4 cases) | **Bookmarks:** a parent listed after its child, page 0, trimmed titles, and a complete plan.<br>**Recorded choices:** an ambiguous entry and one without a page are omitted with their reasons. Their children go to the nearest kept ancestor or the top level, recorded as promotions. An uncertain level goes to the top level, recorded. Removed entries are counted but not listed as omitted.<br>**Refused:** an unknown page count, a page outside the book, and nothing to bookmark. |
| `tst_exportdestination` (new, 5 cases, real files) | **Accepted:** a new `.PDF` outside the library, and a non-ASCII name.<br>**Refused inside the library:** the root, `derivatives/`, `files/`, through `..`, and in another letter case.<br>**Refused even when replacing:** the imported original, and a hard link to a managed source outside the library. An unrelated file can be replaced when asked, and is otherwise refused (Duplicate).<br>**Refused names:** an empty path, a relative path, `.txt`, a missing folder, and a folder named `.pdf`.<br>**Suggested names:** sanitized, with " (2)" on collision, and "Book" when nothing is left. **With the protected-file check removed, the test fails.** |
| `tst_sdkbookexporter` (new, 3 cases, real SDK on `contents-book.pdf` with a non-ASCII name) | **Committed copy:** 4 bookmarks (a child before its parent, page 0, non-ASCII titles), reopened with 27 pages; the structure matches, the output digest matches the file, and the source digest is unchanged.<br>**Existing output:** refused (`OutputExists`), and replaced when asked.<br>**Refused, nothing written:** the source as output (even when replacing, and the source is unchanged), a plan bound to other bytes (`InputChanged`), and an invalid plan (`InvalidPlan`, with issues). A cancel before the write writes nothing. |

**Review fixes (PR #21, review of `c904ef0`):**
- **Must fix, uncertain levels:** a book with any entry of uncertain level could not be exported. The plan sent it to the SDK as a promotion from no parent to no parent, which `validate_plan` refuses (`InvalidPlan`). Now it is listed in `ExportPlan::uncertainLevels`, and `complete()` still counts it. New `tst_sdkbookexporter::plansBuiltFromContentsAreAccepted` sends plans from `buildExportPlan` in nine shapes (no promotion, each kind of promotion, uncertain levels, blank titles, all together) through the real SDK, and all commit.
- **Low, aliases of internal files:** a hard link to `library.sqlite` outside the library was accepted when replacing. Now an existing file with more than one name is refused, an existing name is resolved and checked against the library folder, an unresolvable library folder refuses, and a `:` in the name is refused on Windows. Covered in `tst_exportdestination`.
- **Control runs:** with the old behaviour put back in a build, `tst_exportplan`, `tst_sdkbookexporter` (the reviewer's exact `InvalidPlan` issue on the "uncertain level" shape) and `tst_exportdestination` (the catalog hard link accepted) fail.
- **Verification after the fixes:** `pwsh scripts/verify.ps1` Release and `-Configuration Debug` both **passed**, 25/25 tests.

**Found while testing:** the SDK's `validate_plan` reads an all-zero digest as missing (`MissingInputDigest`), so the test's wrong-digest case uses a non-zero digest to reach `InputChanged`.

**Not in this part:** export records and jobs (part 2), and the window (part 3).

**Next action:** merged ([PR #21](https://github.com/PantaKoda/MyBooksLibrary/pull/21), merge `acc2b89`).

### Part 2: export records and jobs (A2, A4)

**Scope:**
- **Schema 8 (A2):**
  - `jobs` is rebuilt to accept the `export` kind, which succeeds without a run. Rows are copied in order.
  - `exports` records each export job: its destination, whether replacing was asked for, the contents it came from, the application plan as JSON, and what was written.
- **Catalog (`catalog/exports.*`):**
  - `enqueueExport` builds the plan from the effective contents, with edits, and queues the job and record in one transaction. It refuses a trashed book, a book with no contents or nothing to bookmark, and a second open export of the same book.
  - `finishExportJob`: a committed copy always closes `succeeded` / `written`.
  - `exportRecord`, `bookExports`, and `protectedFiles`.
  - The job queue claims exports first. Recovery closes a running export as `interrupted` and never requeues it; `committed` stays unknown. Restore does not resume exports.
- **Coordinator (A4):**
  - `enqueueExport(book, destination, replaceExisting)` validates the destination (A1) with every managed source and imported original protected, then queues the job. Signals: `exportQueued`, `exportRefused`, `exportFinished`.
  - The worker validates the destination **again right before the write** (the PR #21 review note), then calls `BookExporter::exportCopy` with the job's cancel flag.
  - A committed copy is recorded as written whatever came after the commit. `stop()` before the commit gives `interrupted`, not requeued; a user or trash cancel gives `cancelled`. SDK refusals are recorded with stable outcomes.
- **Presentation, to stay correct with the new kind:**
  - The book list ignores export jobs; they never change a book's metadata or contents state.
  - The job queue names them ("Bookmarked copy", "Writing the bookmarked copy…", "Copy saved"), explains an interrupted export, and offers no Retry: an export is asked for again with a destination (part 3). `LibraryController::retryJob` ignores exports.
  - The window does not start exports yet; the SDK exporter is wired in part 3.

**Touched paths:** `src/domain/{jobs.h,codes.cpp,export.*}`, `src/catalog/{migrations.cpp,jobs.*,catalog_internal.h,exports.*}`, `src/processing/processingcoordinator.*`, `src/presentation/{booklistmodel,joblistmodel,librarycontroller}.cpp`, `CMakeLists.txt`, `tests/`, `docs/`.

| Command | Result |
| --- | --- |
| `pwsh scripts/verify.ps1` (Release) and `-Configuration Debug` | **Passed** in both configurations: whitespace and text checks, guard tests, configure, build, **28/28 tests** (ctest), and the application smoke checks. Verified on the working tree based on `acc2b89`. |
| `tst_exports` (new, 8 cases, A2) | **Requests:** the plan comes from the edited revision; the record keeps the run and revision, and survives a restart with its omissions and uncertain levels. Refused: no contents, nothing to bookmark, a second open export, an empty destination, a trashed book, an unknown book, and exports through `enqueueJob`.<br>**Queue:** exports are claimed before metadata and contents.<br>**Committed copies:** written even after a cancel and the trash; cannot be closed twice.<br>**Not written:** cancelled while queued reads as not committed; a failure keeps its outcome; "succeeded" without a commit is refused; a metadata job is not closed as an export.<br>**Restart:** a running export becomes `interrupted`, is not requeued and stays "not known"; a queued export stays queued.<br>**Trash:** ends exports; restore does not resume them; the `trashed` reason is kept.<br>**Protected files:** every book, trashed ones included. |
| `tst_exportjobs` (new, 8 cases, A4 with a fake exporter) | **Written on the worker thread:** the managed source, the destination (non-ASCII), the plan from the effective contents; the record says committed with the output digest; the source is unchanged.<br>**Refused requests queue nothing:** inside the library, the managed source and the imported original (even when replacing), an existing file, `.txt`, and a book without contents.<br>**Checked again before the write:** a file that appeared after the request is kept and the job fails `output_exists` without an SDK call.<br>**A cancel after the commit** is recorded as written; **before the commit**, nothing is written.<br>**`stop()` during an export:** `interrupted`, not committed, not requeued in the next session.<br>**SDK refusals** keep their issues (`invalid_plan`); **no exporter:** `unsupported`. |
| `tst_migrations::version7CatalogGainsExports` (new) | A schema 7 catalog upgrades: jobs are kept in order (the latest per book and kind unchanged), an export can succeed without a run, a metadata job cannot, and a second open job or an unknown kind is refused. |
| `tst_exportplan::planSurvivesJson` (new) | The stored plan keeps nodes (page 0, parents), omissions, a promotion to the top level, uncertain levels (non-ASCII) and removals; malformed JSON is refused. |
| `tst_jobmodels` (new) | Export jobs have their own words in the queue, no Retry, and an interrupted export explains itself. They never change the book list's processing state. |

**Control runs:** with each guard removed in one build, the matching test fails:
- `BookListModel` taking export jobs: `tst_jobmodels::exportsDoNotChangeTheBookList`;
- a committed copy closed as cancelled: `tst_exports::aCommittedCopyIsRecordedWhateverCameAfter`;
- no second destination check: `tst_exportjobs::theDestinationIsCheckedAgainBeforeTheWrite`;
- exports requeued on recovery: `tst_exports::restartDoesNotRequeueAnExport`.

**Review fixes (PR #22, review of `cbd2000`):**
- **Should fix, replacing a file nobody confirmed:** a queued export with "replace" could run later, in the same session behind an analysis or in the next one, and overwrite whatever file was at the path by then.
  - Now the request records the confirmed file's size and modification time in `exports.replace_size` and `replace_modified`, or that none was there.
  - Before the write, a new or different file is kept, and the job fails `output_exists` without an SDK call.
  - New `tst_exportjobs::onlyTheConfirmedFileIsReplaced`, the reviewer's scenario: the file is changed and the library restarts before the export runs. It also covers a file that appears where none was, and an unchanged confirmed file, which is replaced as agreed. New `tst_exports::theConfirmedFileIsRecorded`.
  - **Control:** without the check, the later document is replaced (`committed` true) and the test fails.
  - **Verification after the fix:** `pwsh scripts/verify.ps1` Release and `-Configuration Debug` both **passed**, 28/28 tests.
- **Note for part 3:** an export asked for while an analysis runs waits for it. The dialog should say "Waiting".

**Found while testing:** a helper in the new test looped over `db(...).value()` of a temporary `Result` (a use-after-free, which crashed only in Release). It is fixed in the test, and no product code has the pattern.

**Not in this part:** the Export dialog, the controller wiring of `SdkBookExporter`, and showing export records (part 3).

**Next action:** merged ([PR #22](https://github.com/PantaKoda/MyBooksLibrary/pull/22), merge `f2bd6b4`).

### Part 3: the Export dialog (presentation)

**Scope:**
- **`ExportController`** (`src/presentation/exportcontroller.*`), owned by `LibraryController` as `exporter`:
  - **Preview**, loaded on the database thread: the number of bookmarks, a summary, one note per entry left out or moved with its reason, a suggested name in Documents, and the book's last export. A book in Trash, one without analyzed contents, or a build without the exporter shows the reason instead.
  - **`exportTo(path or URL)`** asks the coordinator for the copy and follows its job: Waiting, Writing, Cancelling, then Saved, NotSaved or Cancelled.
  - **An existing file** moves to NeedsReplace, and only `confirmReplace()` replaces it.
  - **Stale results ignored:** previews are tagged, so a newer `prepare()` wins; the result of a request made for another book is ignored.
- **`ExportDialog.qml`** (`qml/export/`), from the inspector's **More → Save a copy with bookmarks…**:
  - the preview and a "Save as" field with **Choose…** (a native save dialog whose own overwrite prompt is off, so replacing is always confirmed in the dialog);
  - the progress and result, **Show folder**, **Cancel saving**, and Close, which leaves a copy being saved running.
  - The part 2 review note is covered: it says "Waiting for the current work to finish…" while an analysis runs.
- **Wiring:**
  - `LibraryController::setExporter`; `main.cpp` gives it `sdk::SdkBookExporter`.
  - `ProcessingCoordinator::exportRefused` gains `fileExists`, so a refusal because the file exists asks to replace rather than failing.

**Touched paths:** `src/presentation/{exportcontroller.*,librarycontroller.*}`, `src/processing/processingcoordinator.*`, `qml/export/ExportDialog.qml`, `qml/inspector/BookInspectorPane.qml`, `Main.qml`, `main.cpp`, `CMakeLists.txt`, `tests/`, `docs/`.

| Command | Result |
| --- | --- |
| `pwsh scripts/verify.ps1` (Release) and `-Configuration Debug` | **Passed** in both configurations: whitespace and text checks, guard tests, configure, build, **29/29 tests** (ctest), and the application smoke checks (the window loads with the new dialog). Verified on the working tree based on `f2bd6b4`. |
| `tst_exportcontroller` (new, 5 cases, **real SDK** analysis and export, non-ASCII names) | **Preview:** bookmarks, summary ("not changed"), notes when not complete, suggested name.<br>**Saved** through the job queue, from a file URL; a new file with a different digest.<br>**Same name again:** NeedsReplace; declining keeps the file, and confirming replaces it. The next preview names the last copy.<br>**Refused with reasons:** the library folder, an empty name, a book in Trash (no export), an unknown book.<br>**The real `ExportDialog.qml`:** the suggested name arrives with the preview, the summary shows, and **Save copy** writes the file and shows "Saved as".<br>**Partial coverage** (synthetic book): the exact "Left out" and "Top level" notes. Without SDK processors, "not available". |
| `tst_exportjobs`, `tst_inspectorpane`, `tst_librarycontroller` | Pass, with the changed `exportRefused` signal. |

**Review fixes (PR #23, review of `799f747`):**
- **Enter in the path field** ran Save even when Save was disabled, for example while the window closes. It now does only what the Save button allows.
- **Reopening the dialog while that book's copy waits or is being written** showed a still line. Now the preview returns the open export, and the dialog follows it as if started there: progress, result, and **Cancel saving**. The job is read once more after following, so a change in between is not missed.
- **At the window's minimum size** (480×360), asking to replace pushed the buttons out of the dialog. The preview (title, summary, notes, last copy) now scrolls, and the path, progress and buttons stay visible.
- **New tests:**
  - in `tst_exportcontroller::theDialogPreviewsAndSaves`, Enter with `enabledForUse` false does nothing, and with it true it saves;
  - `theDialogFitsASmallWindow`: 480×360 while asking to replace, and every button is inside the dialog;
  - `reopeningFollowsACopyBeingSaved`: a copy queued with processing stopped, the dialog reopened, then Waiting, Cancel, and not saved.
- **Controls:** with each fix undone in one build, the matching test fails. At the small size, Save ends at y 388, below the dialog's 344.
- **Verification after the fixes:** `pwsh scripts/verify.ps1` Release and `-Configuration Debug` both **passed**, 29/29 tests.

**UI evidence:** `docs/images/m09-export-dialog.png` (UI.md), captured by `tst_exportcontroller` with the Windows platform and an example destination.

**M09 gate:**
- **Explicit validated plan to a new PDF:** the preview, then Save, through validation (part 1) and the job (part 2).
- **Every managed source protected:** parts 1 and 2, with refusals shown here.
- **Partial coverage visible:** the summary and a note per entry.

**Next action:** merged ([PR #23](https://github.com/PantaKoda/MyBooksLibrary/pull/23), merge `1dbbe0c`). M09 is complete.

## M08 — Organization

M08 is split in two:
- **Part 1** (A2 + A3): collections and trash races in the catalog and search.
- **Part 2** (presentation):
  - collections in the window, and moving books to them;
  - moving to Trash and restoring, with a Trash view;
  - restoring a duplicate import that is in Trash;
  - stopping a trashed book's running job at once.

### Part 2: organizing in the window (presentation)

**Scope:**
- **`LibraryController`:**
  - views `showLibrary`, `showCollection` and `showTrash` (`view`, `viewCollectionId`, `viewTitle`), with the library and Trash counts;
  - collection commands, `moveToTrash`, `restoreFromTrash` and `restoreTrashedDuplicates`, with `organizeError`.
  - **Trash** also raises the running job's cancel flag, so its SDK call stops early. **Restore** wakes the worker (both from the PR #19 review notes).
- **Refresh** loads the view's books and the collections, and keeps title and state lookups for every book (`BookListModel::setKnownBooks`), so the Activity list and search still know books outside the view.
- **New `CollectionListModel`**, and **`SearchController::setCollection`**.
- **`BookInspector::inTrash`**. The pane's **More** menu gains Add to collection, Remove from the collection, and Move to Trash; a trashed book shows **Restore**.
- **New `LibrarySidebar.qml`**; `Main.qml` gains the sidebar, the view heading, empty-state texts per view, and the restore offer for duplicates in Trash.

**Touched paths:** `src/presentation/`, `qml/`, `Main.qml`, `CMakeLists.txt`, `tests/presentation/`, `tests/CMakeLists.txt`, `docs/`.

| Command | Result |
| --- | --- |
| `pwsh scripts/verify.ps1` (Release) and `-Configuration Debug` | VERIFY PASSED in both; 21/21 `ctest` suites; all smoke checks. `tst_librarycontroller` and `tst_librarysidebar` passed 10 times in a row |
| `tst_librarycontroller::collectionsAndViews` (new) | **Collections:** create; a duplicate name refused with a message; add; the collection view lists its book with its name as the heading; search follows it (1 result, 2 in the library).<br>**Trash:** the trashed book leaves the library and the collection, is listed in Trash "In Trash since …", keeps its title for the Activity list, and cannot be added to a collection. Restore brings it back into its collection.<br>**Rename and delete:** a rename changes the heading; deleting the shown collection returns to the library and keeps the books. |
| `tst_librarycontroller::trashStopsTheRunningJobAndRestoreResumesIt` (new) | Trashing a book whose extraction blocks until cancelled makes the controller idle ("Book moved to Trash"). Restoring starts the extraction again without a restart, and the title is published. **With the cancel flag not raised, the test fails** (the call keeps running). |
| `tst_librarycontroller::aDuplicateInTrashCanBeRestored` (new) | Importing a file whose book is in Trash offers one restore; restoring moves it back to the library. |
| `tst_librarysidebar` (new, real QML) | New collection through the dialog; switching to Trash and back; a duplicate name shows the reason; delete after confirming. No QML warnings. |
| `tst_inspectorpane` | Still passes, with no QML warnings, after the More menu gained the collections submenu |
| `appMyBooksLibrary --library C:\MBL-demo-Contents --inspect-first --screenshot docs/images/m08-library-sidebar.png` (Release, real SDK) | Exit 0. The sidebar shows Library (1), Collections ("No collections yet."), New collection… and Trash (0); the schema 6 demo library upgraded to 7 on open. |

**Review fixes (self-review of `1adf26c`, 9 findings):**
1. **Selection:** when the selected book left the list (Trash, another view, out of the shown collection), the rows changed without a model reset. ListView kept the same index, so the list highlighted another book while the inspector and every action still targeted the old one. The list is now `qml/library/BookListView.qml`. It finds the selected book again after every row change, or clears the selection, and it does not drive the inspector while search results are shown.
2. **Refresh cost:** collection rows are filtered from the books already loaded, using `catalog::collectionBookIds`, instead of recomputing every member's summary.
3. **Lookups:** `titleOf` and `processingStateOf` use the all-books hash first, which also holds the newest values.
4. **Notifications:** `viewChanged` is emitted only when the view or its heading changes, not on every refresh.
5. **Messages:** refusal messages are chosen per command, so a book command never says "Enter a name for the collection".
6. **Keyboard:** collection rows open their menu with the Menu key or Shift+F10; F2 renames, Delete asks to delete, and Enter or Space shows the collection (AGENTS.md §9).
7. **Plain text:** collection names are plain text in the sidebar, the Add to collection items and the delete dialog.
8. **Activity titles** are refreshed on every snapshot, including one skipped because the view changed.
9. **Tests:** a collection row is clicked, and the Add to collection submenu is tested.

| Command | Result |
| --- | --- |
| `tst_booklistview` (new, real QML) | After trashing the selected book, and after switching to Trash and back, the list's current row, its selected ID and the inspector agree. While search drives the inspector, the list leaves it alone. **With the old reset-only resync, it fails:** index 1 highlights another book while the selection and inspector still name the trashed one. |
| `tst_librarysidebar` (extended) | Clicking the collection row shows it. F2 opens rename with the name filled in; Delete opens the confirmation. A name with markup stays plain text (`textFormat` PlainText) in the row and the dialog. |
| `tst_inspectorpane::addToCollectionMenuFollowsTheCollections` (new) | The submenu lists the collections by name as they are created and deleted; choosing one asks for that collection. |
| `tst_organization` | `collectionBookIds` returns the members; an unknown collection gives NotFound |
| `pwsh scripts/verify.ps1` (Release) and `-Configuration Debug` | VERIFY PASSED in both; 22/22 `ctest` suites; all smoke checks |

**CI failure on `e8b91b3`** ([run 36528739974](https://github.com/PantaKoda/MyBooksLibrary/actions/runs/36528739974)): `tst_inspectorpane::correctionDialogSavesAndSurvivesRefreshes` failed with "Maximum amount of warnings exceeded".
- **Cause:** the native Windows controls style calls the Windows theme API for every control, and offscreen that fails with an "OpenThemeData() failed" warning each time. Each run was close to Qt Test's 2,000-warning limit, and the slower CI run went over.
- **Fix:** the QML tests (`tst_inspectorpane`, `tst_searchresultsview`, `tst_librarysidebar`, `tst_booklistview`, `tst_readerpane`) now use the Basic style unless `QT_QUICK_CONTROLS_STYLE` is set. They log at most a few warnings: the missing font directory, and in `tst_readerpane` the existing "Cannot open:" when a document source is cleared. `tst_inspectorpane` went from 17.8 s on CI to 2.2 s locally. The application still uses the native style.

**Not verified by hand:** the right-click and press-and-hold menus on collections with a real mouse, and full keyboard-only use of the window.

**Next action:** merged; M08 is complete (permanent deletion remains open). Next: M09.

### Part 1: collections and trash races (A2 + A3)

**Scope:**
- **Schema 7:** `collections`, `collection_books` and `books.trashed_at`.
- **`catalog/collections.*`:** create, rename, delete, add, remove, list, a collection's books, and a book's collections.
- **Trash** ends the book's jobs and records the time. **Restore** ends any stale open job and queues new jobs for components without published results.
- **`finishJob`** keeps the outcome `trashed`.
- **Search:** `SearchRequest.collection` filters by collection.
- **Domain:** `CollectionId`, `CollectionSummary` and `BookSummary::trashedAt`.

**Touched paths:** `src/domain/`, `src/catalog/`, `src/search/searchindex.cpp`, `CMakeLists.txt`, `tests/catalog/`, `tests/CMakeLists.txt`, `docs/`.

| Command | Result |
| --- | --- |
| `pwsh scripts/verify.ps1` (Release) and `-Configuration Debug` | VERIFY PASSED in both; 20/20 `ctest` suites; all smoke checks |
| `tst_organization` (new, 7 cases) | **Collections:** membership only (no asset or book added), a book in two collections, adding twice, removing, and a restart. Names: trimmed, empty refused, case-insensitive duplicates refused on create and rename, renaming to itself in another case allowed. Unknown collection or book gives NotFound, all or nothing. Deleting a collection keeps its books.<br>**Trash:** trashed books keep their memberships but are not listed or counted, cannot be added, record `trashedAt`, and come back on restore.<br>**Search** within a collection.<br>**Jobs:** trash cancels queued jobs with `trashed`; restore queues only the missing components, with current generations, and is idempotent.<br>**Race:** trashed and restored while a job runs, the new request is not blocked; the stale result is refused and ends as `trashed`; the new job runs and publishes. A queued job left by an older trash is ended on restore. Passed 30 times in a row (`--repeat until-fail:30`). |
| `tst_migrations::version6CatalogGainsCollections` (new) | A schema 6 catalog with a trashed book upgrades; the book stays trashed with no trash time, can be restored and added to a collection |
| `tst_processingcoordinator::trashedWhileRunningIsNotPublished` (existing) | Still passes: after `finishJob` kept the reason, the job ends `cancelled` / `trashed`. The first run of the change failed here with outcome `cancelled`, which led to that fix. |
| Mutations, each reverted | No search filter: `searchWithinACollection` fails. Restore not ending stale open jobs: `restoreEndsJobsLeftOpenByAnOlderTrash` fails. |

**Found while testing:**
- A test helper iterated over `db(...).value()` of a temporary: a use-after-free (heap corruption) that showed up as a garbled outcome. Fixed in the test.
- One race test depended on the order of two jobs created in the same millisecond. It now selects the metadata job explicitly.
- **CI found a second order dependence** (run 36460887004 on `5a20667`). `collectionsAreMembershipOnly` expected a collection's books in import order, but books imported in the same millisecond are ordered by their random ID. That passed locally and failed on the faster CI runner. The test now compares the titles as a set; it passed 50 times in a row (`--repeat until-fail:50`). The product ordering is unchanged: stable, and the same as the library list.

**Review fix (PR #19 review of `f3f6a7c`):**
- *Should settle:* restore queued work for every component **without a published result**, not for **what the trash stopped**. So (a) an extraction that had failed before the trash was retried on its own, and (b) a queued rerun, cancelled by the trash, was dropped when an older result existed.
- Restore now resumes each kind with a job ended as `trashed` since the trash time, or any such job for books trashed before schema 7. The trash time is read before it is cleared.

| Command | Result |
| --- | --- |
| `tst_organization::restoreResumesExactlyWhatTheTrashStopped` (new) | (a) The failed metadata job stays failed and only the queued contents job is resumed. (b) The queued rerun is resumed. A later trash and restore with nothing running queues nothing. With the old rule restored, the test fails at (a), as the review reproduced. |
| `pwsh scripts/verify.ps1` (Release) and `-Configuration Debug` | VERIFY PASSED in both; 20/20 `ctest` suites; all smoke checks. `tst_organization` passed 50 times in a row (Debug) |

**Not in this part:** the window (part 2), and permanent deletion of trashed books. For part 2, from the review:
- after a restore, the window must wake the processing worker (`start()`); otherwise the resumed jobs wait for the next launch;
- trash must raise the running job's cancel flag so its SDK call stops early.

**Next action:** merged. Then M08 part 2.

## M07 — Corrections and reruns

M07 is split in three:
- **Part 1** (merged): metadata corrections in the inspector, and reruns started by the user.
- **Part 2a** (A2 + A3): contents edits in the catalog and search.
- **Part 2b** (presentation): editing the contents and reconciling in the inspector.

### Part 2b: editing contents and reconciling in the inspector (presentation)

**Scope:**
- **`BookInspector`:**
  - contents commands `renameEntry`, `setEntryPage`, `clearEntryPage`, `indentEntry`, `outdentEntry`, `removeEntry`, `restoreEntry` and `addEntryAfter`, plus `keepContentsEdits` and `useAnalyzedContents`;
  - `canIndent` and `canOutdent`;
  - the properties `contentsEdited`, `contentsNeedReconciliation`, `contentsEditText` and `contentsError`.
  - Each change carries the run and revision on screen. A stale one is refused with a message, and the contents reload.
- **Summary and notes** leave out removed entries and count edits.
- **`TocTreeModel`:** roles `removed` and `editedText`; state and details say what the user changed, and a page or level the user set replaces the analysis's reasons (review note of PR #17); `indexOfEntry(key)`.
- **`BookListModel`:** "(edited)" and "(edited; a new analysis to review)".
- **`BookInspectorPane.qml`:**
  - the edit bar on the selected entry, with an entry dialog for rename, page and add;
  - the banner, with Keep my edits / Use the new analysis, or Discard my edits… after a confirmation;
  - removed entries struck through;
  - the edited entry made current again after the tree is rebuilt.

**Touched paths:** `src/presentation/`, `qml/inspector/BookInspectorPane.qml`, `tests/presentation/`, `docs/`.

| Command | Result |
| --- | --- |
| `pwsh scripts/verify.ps1` (Release) and `-Configuration Debug` | VERIFY PASSED in both; 19/19 `ctest` suites; all smoke checks |
| `tst_librarycontroller::contentsEditsFromTheInspector` (new) | **Page reasons:** after "Set page", the analysis's "No printed page number matched." is gone, and "You set its page to 5." and "page set by you" are shown.<br>**Refused before saving:** pages 0, 21, `x` and empty in a 20-page book ("Enter a page number from 1 to 20."), a blank title, and another book.<br>**Levels:** Indent goes under the entry above; Outdent returns; `canIndent` and `canOutdent` are right.<br>**Add, remove, restore:** the summary counts without removed entries, and the notes list removed entries.<br>**Stale view:** a change made elsewhere first gives "The contents changed while you were editing…", nothing is saved, and the new contents show.<br>**Newer analysis:** a different analysis shows the reconciliation text; Keep and then Use the new analysis both work. |
| `tst_inspectorpane::contentsEditingInThePane` (new, real QML) | Renaming the **second** entry through the dialog keeps it current after the rebuild. A page outside the book keeps the dialog open with the reason. The banner appears; Discard, after confirming, shows the analysis again. |
| Mutations, each reverted | Showing the analysis's page reasons for a page the user set: `contentsEditsFromTheInspector` fails. Not reselecting the edited entry: `contentsEditingInThePane` fails. The first version of the pane test used a one-entry book and did not catch this; it now edits the second entry. |
| `all_qmllint`; QML warnings in the pane tests | None. A binding loop in the new discard dialog, seen in the test output, was fixed. |
| `appMyBooksLibrary --library C:\MBL-demo-Contents --import contents-book.pdf --inspect-first --screenshot docs/images/m07-contents-editing.png` (Release, real SDK) | Exit 0. The edit bar is shown under the entry's reasons; Indent and Outdent are unavailable for the first top-level entry. |

**Not verified by hand:** clicking through the edit bar and dialogs with the mouse and keyboard, and screen readers.

**Next action:** merged; M07 is complete. Next: M08.

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

**Review fixes (PR #17 review of `4b5263b`):**
1. *Should fix:* an entry added after a removed entry copied its `removed` flag and was silently hidden. A new entry is now hidden only when its **parent** is removed.
2. *Should fix:* `BookInspector` rebuilt its contents tree only when the run changed, so an edit, keep or discard (same run) left the old entries shown. It now rebuilds when the run **or the revision** changes.
3. *Consider:* `toc_edit_revisions.base_run_id` was `ON DELETE CASCADE`, so deleting a run would have silently deleted the edits based on it. It is now `ON DELETE NO ACTION`, checked at the end of the statement: deleting such a run fails, and deleting the book still removes everything through `book_id`. (Migration 6 is not released yet, so it is changed in place.)

| Command | Result |
| --- | --- |
| `tst_tocedits::addedEntriesAreHiddenOnlyUnderARemovedParent` (new) | Adding after a removed "Index" gives a visible, counted, searchable entry. Adding among the sub-entries of a removed parent hides the new entry with them, and restoring the parent brings it back. |
| `tst_tocedits::editsOutliveAttemptsToDeleteTheirRun` (new) | Deleting a run that a revision is based on fails, and the revision stays. Deleting the book removes the revisions, entries and runs. |
| `tst_librarycontroller::inspectorFollowsEditedContents` (new) | The inspector tree shows the renamed entry after an edit on the same run, keeps the edits after a changed rerun, and shows the analysis again after a discard. |
| Controls | With all three fixes reverted, each of the three new tests fails, as the review reproduced. |
| `pwsh scripts/verify.ps1` (Release) and `-Configuration Debug` | VERIFY PASSED in both; 19/19 `ctest` suites; all smoke checks |

**Not in this part:** the inspector UI for editing and reconciling (part 2b), and export plans from edited contents (M09). Also for part 2b, from the review:
- the tree, `contentsSummaryOf` and `contentsNotesOf` still show and count removed entries, flagged but not distinguished;
- an entry the user gave a page keeps the analysis's "no page found" reasons in its evidence.

**Next action:** merged. Then M07 part 2b.

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

## SDK 0.4.0 update (2026-10-03)

**Why:** the owner asked for the new engine in the app. PDFMegine released SDK **0.4.0** (tag `v0.4.0` = `cd46ea8`): every printed ISBN in the metadata result (PR #8), version 0.4.0 for that C++ layout change (PR #12), the OCR models' licence in every package (PR #10, issue #6), OpenCV built without Media Foundation (PR #11, issue #7), and documentation of `analyze_book()` report fields (PR #9, issue #5).

| Item | Value |
| --- | --- |
| Release asset | `pdfbookmark-sdk-0.4.0-win64.zip`, SHA-256 `cfe9df4eb470b1ac34347e3672982bffa905195b0ac858e5985d4d2d6816577c` (matches the release's digest), unpacked to `…\Dev\pdfbookmark-sdk\0.4.0\`; 0.3.0 kept |
| Header changes from 0.3.0 | `metadata/metadata.hpp`: `IsbnForm`, `IsbnFormat`, `IsbnValue`, `MetadataResult::isbns`, `form_name`, `format_name`; comments in `pdfbookmark.hpp` and `pdfbookmark.h`; `version.hpp`. All other headers byte-identical |
| Runtime changes | `opencv_world500.dll` is now `libopencv_world500.dll` (Release and Debug); `pdfbookmark.dll`, `qpdf30.dll`, `jpeg62.dll`, `z.dll` rebuilt; `licenses/PaddleOCR-PP-OCR-models.txt` added. SDK folder 292 MB instead of 430 MB |
| Header / loaded version | `0.4.0` / `0.4.0` (Debug, Release and the package) |

**Changes:** `find_package(pdfbookmark 0.4)`; the CI SDK version and digest (`.github/actions/setup/action.yml`); the AGENTS.md baseline; the CLAUDE.md SDK brief path; `package.ps1` (OpenCV's DLL name, only the SDK's DLLs staged, the models' licence from the SDK, the Media Feature Pack note only when needed); `third_party/licenses` without the PaddleOCR copy; BUILDING.md, READER.md, RELEASING.md, USER_GUIDE.md, the `verify.ps1` example, DECISIONS.md, releases/UNRELEASED.md. No application code changes: the app does not read `isbns` yet.

**Verification** (Windows 11, Qt 6.11.2 MSVC2022 64-bit, Visual Studio 2026, SDK 0.4.0):

| Command | Result |
| --- | --- |
| `pwsh scripts/verify.ps1 -Configuration Release -Clean -SdkDir <sdk 0.4.0>` | VERIFY PASSED: 34/34 tests, app smoke checks; `sdk.header_version=0.4.0`, `sdk.loaded_version=0.4.0` |
| `pwsh scripts/verify.ps1 -Configuration Debug -Clean -SdkDir <sdk 0.4.0>` | VERIFY PASSED: 34/34 tests, app smoke checks; header and loaded version 0.4.0 |
| `pwsh scripts/package.ps1 -SdkDir <sdk 0.4.0> -SkipZip` | First run **failed**: the reused `build\package-release` still held 0.3.0's `opencv_world500.dll`, which was staged and had no known licence. After staging only the SDK's DLLs: PACKAGE PASSED (270.6 MB). No Media Foundation import reported; `NOTICE.txt` names the models' licence from the SDK and has no Media Feature Pack note. Outside the repository: `--sdk-check` (0.4.0), `--sqlite-check`, `--reader-check` on the text PDF, `--reader-check --require-ocr` on the scanned fixture (4 pages OCR'd, 58.4 s alone, peak 2.4 GB, viewer and SDK concurrently with 0 differing pages, cancel while viewing), and the window: import, read, export, close (bookmarked copy written) |
| `cmake … -DPDFBOOKMARK_SDK=<sdk 0.3.0>` on this branch | Refused at configure: the version found (0.3.0) is not compatible with the requested "0.4" |

**Not verified:** the app on a Windows N edition without the Media Feature Pack; reading real books again (only the fixtures were analysed; 0.4.0 changes no TOC code). `PDFBOOKMARK_SDK` in the owner's user environment still points at 0.3.0, and the Qt Creator build folders cache an older SDK path (0.2.0): with `find_package(… 0.4)` they now stop at configure until they point at the 0.4.0 folder.

**Next:** storing and showing ISBNs (catalog migration, `SdkMetadataExtractor`, inspector), as a separate PR if wanted.

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
