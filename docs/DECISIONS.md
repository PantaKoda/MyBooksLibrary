# Decisions

Significant changes, newest last. Each entry lists **Change / Why / Assumptions**, plus **Removed / Verified** when relevant.

## 2026-09-24 — M00: SDK location without a personal default

- **Change:** `PDFBOOKMARK_SDK` is a cache path. A nonempty cached value wins; an empty one is filled from the `PDFBOOKMARK_SDK` environment variable (with `FORCE`). Configuration fails with an explanatory message when both are empty. `/CMakeUserPresets.json` is git-ignored for personal presets.
- **Why:** AGENTS.md forbids committing personal absolute paths. An explicit failure is clearer than a `find_package` error about a missing package.
- **Assumptions:** Existing build folders already cache the path. The Qt Creator folder `build/Desktop_Qt_6_11_2_MSVC2022_64bit_Debug` has `PDFBOOKMARK_SDK:PATH=…/pdfbookmarkSdk`, so the current setup keeps working.
- **Removed:** The hard-coded personal SDK path default and the leftover `# ← added` comments.
- **Verified:** A fresh `build/cli-debug` configure with `-DPDFBOOKMARK_SDK=…` built and ran. The review fix (PR #1, finding P2) was checked with four configure sequences; see IMPLEMENTATION_PROGRESS.md, M00.
- **Review fix (PR #1):** The first version used `set(... "$ENV{...}" CACHE ...)`. A failed first configure left an empty cache entry, which `set(CACHE)` never overwrites, so setting the environment variable afterwards did not help.

## 2026-09-24 — M00: SDK boundary module and `--sdk-check` harness

- **Change:** Added `src/processing/sdk/sdkinfo.{h,cpp}`, which reports the SDK identity and runs a blocking PDF probe (`read_pdf_identity` and `extract_metadata`). `main.cpp` gains a windowless `--sdk-check [<pdf>]` mode.
- **Why:** The M00 gate requires an actual SDK call from the application with the SDK identity recorded. The probe keeps SDK headers inside `src/processing/sdk/`, which is the planned A4 boundary.
- **Assumptions:** In `--sdk-check` mode no GUI exists, so running the blocking call on the main thread is acceptable. GUI code must still use a worker thread (A4, M04). Qt value types are used at the boundary. Paths go through `toStdWString()` on Windows and UTF-8 elsewhere.
- **Verified:** The fixture and a Greek/ü non-ASCII path resolved the title. A missing file exits with 1 and the SDK's message.

## 2026-09-24 — M00: Synthetic PDF fixtures

- **Change:** `tests/fixtures/make_fixtures.py` (standard library only) generates `title-page.pdf`, which is committed. `.gitignore` keeps ignoring `*.pdf` except `tests/fixtures/*.pdf`.
- **Why:** Checks need a real PDF without committing personal books or adding a PDF-generation dependency.
- **Assumptions:** Python 3 is available to regenerate fixtures. Regeneration is deterministic (SHA-256 `26f29980…0d195f`).

## 2026-09-24 — M01: FTS5 feasibility probe through QSQLITE

- **Change:** Added `src/infrastructure/sqlitecapabilities.{h,cpp}`, the `--sqlite-check` app mode and `tests/infrastructure/tst_sqlitecapabilities`. The probe opens a private in-memory QSQLITE connection. It creates a temporary FTS5 table with `unicode61 remove_diacritics 2`, inserts rows and checks `MATCH`, `bm25()`, diacritic folding and prefix queries.
- **Why:** AGENTS.md section 7 requires proving FTS5 through the actual driver, because the version string is not enough. The probe is reusable later as a startup capability check.
- **Assumptions:** The Qt 6.11.2 msvc2022_64 QSQLITE plugin bundles its own SQLite (3.53.4, `ENABLE_FTS5`), so no separate SQLite dependency is needed.
- **Verified:** See IMPLEMENTATION_PROGRESS.md, M01.

## 2026-09-24 — M01: `mbl_core` static library and the test layout

- **Change:** Non-GUI, non-SDK application code builds into the static library `mbl_core` (links `Qt6::Sql`, exports the `src/` include path and `/utf-8`). The app and the tests link it. `tests/CMakeLists.txt` provides `mbl_add_test()`: a console-subsystem Qt Test executable registered with CTest, with Qt's `bin` prepended to `PATH`. The `MBL_BUILD_TESTS` option is ON by default.
- **Why:** AGENTS.md section 3 asks for ordinary internal library targets, and the tests must exercise the same code as the app without the SDK DLLs.
- **Assumptions:** The SDK boundary (`src/processing/sdk/`) stays in the app target for now. If it needs tests, it will get its own target that links the SDK.
- **Removed:** The app target's own `src` include path and `/utf-8` option; both are now inherited from `mbl_core`.

## 2026-09-24 — M01: FTS5 query syntax must be escaped (input for A3)

- **Change:** None to behaviour. A test records the baseline: binding raw user text `C++` to `MATCH` is an FTS5 syntax error, while the quoted string `"HTTP/2"` is a valid phrase query.
- **Why:** Binding values prevents SQL injection but not FTS5 syntax errors. A3 (M02/M06) must compile user queries into quoted FTS5 terms.
- **Assumptions:** The escaping design (quoting, doubling `"`, prefix handling) belongs to A3 and is not decided here.

## 2026-09-25 — M01: Qt PDF linked; reader coexistence harness

- **Change:** The app links `Qt6::Pdf`. `appMyBooksLibrary --reader-check <pdf> [<rounds> [no-view]]` (`src/reader/readercheck.*`) checks Qt PDF rendering, the `QtQuick.Pdf` QML module, and coexistence with concurrent SDK metadata and analysis. It covers identical results, cancellation, destroying the viewer during analysis, and non-ASCII paths. On Windows, a diagnostics-only vectored exception handler prints the code, thread and module stack of fatal native exceptions. `sdkinfo` gains `probeAnalysis()` with the automatic-analysis policy. Two fixtures were added: `contents-book.pdf` (printed TOC) and `image-only.pdf` (forces OCR).
- **Why:** AGENTS.md section 9 requires actual coexistence evidence because both libraries embed PDFium.
- **Assumptions:** The harness uses a plain `std::thread`, not the A4 worker design (M04). It never blocks the GUI thread on the worker; on timeout it exits with code 3. The exception handler is a diagnostic and does not recover from anything.
- **Verified:** See READER.md. Text PDFs pass everywhere, including the packaged build. OCR with concurrent viewing terminates the process in Qt PDF's allocator (`0xE0000008`), which is recorded as a blocker.

## 2026-09-25 — M01: Coexistence blocker, not worked around

- **Change:** None in behaviour. The failure is documented with three options (upstream memory work, helper process, pause viewing during OCR).
- **Why:** Choosing between them changes the architecture or the SDK. That is the owner's decision (AGENTS.md sections 3 and 12).
- **Assumptions:** Import, catalog and search work (M02–M05) does not depend on the viewer and can continue.

## 2026-09-25 — M01 review fixes (PR #4): trustworthy coexistence evidence

- **Change:**
  1. The SDK probes record model identity, OCR attempts and pages with completed OCR. `--require-ocr` turns zero completed OCR pages into NOT_EXERCISED. `--no-models` provides the missing-models case.
  2. The QtQuick.Pdf import moved to the registered `qml/reader/ReaderCheckView.qml`.
  3. Cancellation and shutdown use a progress-callback handshake, so the request is made during active work. Cancellation must be observed; otherwise the result is FAIL, or NOT_EXERCISED if the race was lost.
  4. Rounds are compared by a semantic snapshot digest (`sdksnapshot.*`), not by counts.
  5. A Qt-only control, a persistent-viewer mode, and process and system commit sampling were added.
  6. The SDK boundary became the static library `mbl_sdk`, so it can be unit-tested (`tst_sdksnapshot`). Verdict rules live in `mbl_core` (`tst_checkverdict`).
- **Why:** The review showed that the first harness could pass without exercising OCR or cancellation, compared only counts, and hid a QML import from deployment. It also asked for controls before any reader architecture is chosen.
- **Assumptions:** NOT_EXERCISED counts as not passed. The snapshot excludes diagnostics and counters. The harness remains a diagnostic tool; product threading follows A4 (M04).
- **Verified:** See READER.md, "With the corrected harness", and IMPLEMENTATION_PROGRESS.md (M01 part 2).

## 2026-09-25 — M01: OCR and viewing reassessed as a memory-pressure risk

- **Change:** The earlier "blocker" is reframed. With the corrected harness, OCR with churn and with persistent viewing both passed (0/2 crashes, against 3/3 earlier). System commit peaked at about 52 of 57.2 GB during OCR, while the Qt-only controls stayed at about 30 MB.
- **Why:** The evidence points to SDK OCR memory pressure on the whole machine, not to a collision between the two PDFium copies. A helper process would not protect the viewer from that.
- **Assumptions:** The cause of the earlier crashes is unproven (no commit data or dump at the time). The risk stays open and is tracked in READER.md. The primary recommendation is upstream OCR memory reduction.

## 2026-09-25 — M01 re-review fix (PR #4): unambiguous snapshot encoding

- **Change:** `sdksnapshot.cpp` encodes strings as quoted, escaped values, absent optionals as `null`, and composites in brackets. It builds lines by concatenation instead of chained `QString::arg`.
- **Why:** Re-review of `bd61dc0` found that `TitleValue{"A|B","C"}` and `{"A","B|C"}`, and a missing subtitle and the text `-`, encoded identically, so a changed field could pass the semantic comparison. Contributor separators and line breaks had the same weakness. Chained `arg()` would also re-substitute `%N` inside document text.
- **Verified:** The new `encodingHasNoCollisions` test fails on the previous encoding (at the reviewer's example) and passes now. ctest 7/7.

## 2026-09-25 — M02: Domain contracts in `src/domain/`

- **Change:** Added application value contracts: typed UUID IDs (`BookId`, `AssetId`, `RunId`), `Result`/`Status` with error codes, metadata (status per field, ordered contributors with roles, Auto/Value/Cleared overrides, `effectiveMetadata`), TOC entries (hierarchy and destination states, printed label kept apart from the zero-based page), book/asset/run identity, publish tickets, and search request/response types. Stable text codes for every stored enum live in `codes.cpp`.
- **Why:** AGENTS.md section 6 requires contracts with stable IDs, revisions and explicit optionals before any views are wired.
- **Assumptions:** Metadata evidence and candidates stay in the raw SDK report for now (`RunIdentity.reportPath`). Normalising them into rows is M04 work.

## 2026-09-25 — M02: Database thread, library lock and migrations

- **Change:** `infrastructure::DatabaseExecutor` owns one thread and one QSQLITE connection (WAL, foreign keys, busy timeout). `catalog::Library` adds a `QLockFile` writer lock and runs versioned migrations (`PRAGMA user_version`, one transaction per migration, refusing newer schemas).
- **Why:** AGENTS.md section 6 requires one database thread, pass-by-copy values, foreign keys, versioned non-destructive migrations and a single-process writer lock.
- **Assumptions:** `Library::open` blocks while migrating. The composition root (M03) must call it off the GUI thread. The executor's destructor waits for its thread; it runs only at library shutdown after queued work. The lock's stale time is 0, so a crashed process's lock is reclaimed as soon as its PID is gone.

## 2026-09-25 — M02: Schema version 1 and transactional publication

- **Change:** Tables for assets, books, metadata runs/contributors, overrides, TOC runs/entries, plus A3's FTS5 projections (see CATALOG.md). Publication verifies generation, lifecycle and source digest, then writes run, active pointer, revision and projection in one transaction. Trash bumps both generations.
- **Why:** AGENTS.md sections 1 (rules 5–7), 6 and 7: states kept separate, every TOC entry kept, overrides preserved, stale or trashed results refused, and index changes sharing the catalog transaction.
- **Assumptions:** A `known_parent` entry with a missing, self or cyclic parent is stored as `unknown` instead of rejecting the whole TOC. Rule 6 (keep every entry) outweighs rejecting the run. A resolved destination beyond a known page count is refused as invalid input.

## 2026-09-25 — M02: Search projections and query compiler

- **Change:** `search_books` and `search_toc` FTS5 tables use `unicode61 remove_diacritics 2 tokenchars '+#'`. `search::compileQuery` quotes every term, supports phrases and trailing-`*` prefixes, and treats operators as text. Ranking has two tiers and never compares ranks across indexes (see SEARCH.md).
- **Why:** AGENTS.md section 7. The M01 test showed that raw user text breaks FTS5, and treating `+#` as token characters keeps `C++`/`C#` distinct from `C`.
- **Assumptions:** Indexing the file-name fallback title helps find poorly named PDFs. It is shown with `displayTitleFromFileName` and never stored as metadata. Chapter hit details are copied into `search_toc` so that A3 queries do not read A2 tables.

## 2026-09-25 — M02 review fixes (PR #3)

- **Change:**
  1. `publishToc` rejects `TocAnalysis.outcome` ≠ `RunIdentity.outcome` instead of silently storing only the run's value.
  2. Search reads display titles for contents-only matches in one pass over `search_books`, instead of one lookup per book.
  3. `DatabaseExecutor` no longer sets `journal_mode`. `Library::open` runs the read-only `checkCompatible` first, then enables WAL, then migrates.
- **Why:** Review findings on PR #3:
  1. The partial or no-TOC outcome could be lost.
  2. The lookup was O(books × matches) on the only database thread, because `book_id` is UNINDEXED in FTS5.
  3. WAL mode is persistent, so a refused newer catalog was being modified, contrary to the refusal guarantee.
- **Assumptions:** A single table read per search is acceptable at the expected library sizes (400 contents-only matches: 14 ms in Debug). An indexed book-to-rowid projection can replace it if profiling shows a need.
- **Verified:** These regression tests fail on the previous code and pass now:
  - `enablesForeignKeysWithoutPersistentChanges`;
  - `newerSchemaIsRefusedUnchanged` (DELETE-mode catalog stays byte-identical, with no `-wal`/`-shm` files);
  - `tocOutcomeRoundTripsAndMismatchIsRejected` (including after restart).

  `manyContentsOnlyMatchesUseOneTitleRead` (400 books) checks results, order and pagination and logs the elapsed time. It does not count scans or assert latency, so it would also pass on the previous, slower implementation. The fix itself was verified by code inspection and the 14 ms measurement.

## 2026-09-25 — SDK baseline moves to pdfbookmark 0.2.0

- **Change:** `find_package(pdfbookmark 0.2 CONFIG REQUIRED)`. AGENTS.md records SDK 0.2.0 (tag `v0.2.0` = `93d9128`, fix PR #2 `eeb977c`). The probes expose `ocr_threads` (default 0 = automatic). The SDK is installed side by side under `…\pdfbookmark-sdk\<version>\`.
- **Why:** 0.2.0 fixes the OCR memory and throughput problem found in M01 (PDFMegine issue #1). The probes set `ocr_threads`, which 0.1 headers lack. Requiring 0.2 also stops a build folder from silently staying on the old SDK, whose option structs have a different size.
- **Assumptions:** Automatic OCR threads (8 here) stay the default. `ocr_threads=4` kept the GUI slightly smoother (7 ms against 10 ms maximum turn gap) but made OCR about 40% slower, so choosing it is left to the M04 job design. Existing build folders, including the owner's Qt Creator folder, must point at the new SDK and be rebuilt from clean.
- **Verified:** See IMPLEMENTATION_PROGRESS.md, "SDK 0.2.0 update".

## 2026-09-25 — Reader check: view destroyed before its document; unexplained Qt Quick crash recorded

- **Change:** `ReaderCheckView.qml` hosts `PdfMultiPageView` in a `Loader`. The check's default teardown deactivates the view, processes events for 200 ms, then destroys the document. `--qml-cycles` and `--qml-naive-teardown` allow stress runs.
- **Why:** One package run crashed (`0xC0000005`) on a Qt Quick worker thread during that check's teardown. The hypothesis was that page-image loads were still using the document. 1,500 naive cycles did not reproduce it, so the hypothesis is unconfirmed.
- **Assumptions:** The ordered teardown is a reasonable default but not a proven fix. The M06 reader adapter must own document lifetime explicitly and needs its own stress test with a visible window.

## 2026-09-26 — M03 part 1: import protocol with recorded phases

- **Change:** Added A1 `src/storage/` (`LibraryLayout`, `copyVerified`, `ImportService` with `recover()`), A2 `catalog/imports.*`, and migration 2 (`import_operations`). `registerBook` shares its insert with `completeImport` through `catalog_internal.h` and now reports `ErrorCode::Duplicate`. See STORAGE.md.
- **Why:** AGENTS.md sections 1 and 5 require verified managed copies, unchanged originals, exact-SHA-256 deduplication, and recovery of interrupted operations without losing or duplicating files.
- **Assumptions:**
  - A same-filesystem `QFile::rename` is the install commit.
  - A `QSaveFile` commit plus a re-read digest is the verification.
  - A `copying` operation is abandoned on recovery rather than resumed, because its source may have changed. The user imports it again.
  - Orphaned managed files are reported, never deleted.
  - Files that don't contain `%PDF-` in their first KiB are refused before any record is made.
  - Import runs on a worker thread; wiring it to the GUI is M03 part 2.
- **Verified:** `tst_importservice` (15 cases, including a crash after each phase with a restart) and `tst_migrations` (v1 → v2 upgrade with data). ctest 8/8 in Debug and Release, and repeated 3 times.

## 2026-09-26 — M03 part 1 review fixes (PR #6)

- **Change:**
  1. Migration 2's constraints now require `sha256`, `byte_size` and `asset_id` only for `verified`/`registered`, and `sha256`, `byte_size` and `book_id` for `duplicate`. `catalog::closeImportAsDuplicate` stores them. Every transition's result is checked, and a failure to record an outcome is reported as Failed.
  2. Recovery removes a verified stage only after a good installed copy exists or the bytes are catalogued. Installation or registration errors leave the operation open (`RecoveryReport::deferred`) for a retry. A wrong-digest destination is replaced by the good stage.
  3. `commitMove` (MoveFileExW write-through; no copy, no replace) replaces `QFile::rename`, which can fall back to copy and delete.
  4. `import_operations.book_id` is `ON DELETE RESTRICT`.
- **Why:** Review of `2d74ae4`:
  1. Ordinary duplicates failed the CHECK constraint silently, and restarts then mislabelled them as abandoned.
  2. Recovery could delete a good verified copy before installing it.
  3. The atomic-rename assumption did not hold for `QFile::rename`.
  4. `SET NULL` contradicted the `registered` CHECK.
- **Assumptions:** Migration 2 was edited in place because it is unreleased (PR #6 not merged). Import history blocks permanent deletion of its book until M08 decides how history is kept or removed. An install failure during a live import still closes the operation, because the external original is available to import again.
- **Verified:** `tst_importservice` 18 cases. Mutations: the old constraint fails both duplicate tests and the crash-recovery cases that reach a duplicate close; "remove stage before install" fails both new recovery tests. ctest 8/8 in Debug and Release, and repeated 3 times in each.

## 2026-09-26 — M03 re-review fix (PR #6): unreadable is not damaged

- **Change:** `storage::checkDigest` returns Missing, Match, Mismatch or Unreadable. Recovery defers (keeps files and the `verified` phase) when no copy is confirmed good and one could not be read. It fails an operation only when both copies are confirmed missing or mismatched, and never replaces an unreadable destination.
- **Why:** Re-review of `e12e5dc`: `sha256OfFile` returning nullopt on an open or read error was treated as a digest mismatch. A transient sharing or I/O failure could therefore delete the only verified copy and close the import as Failed.
- **Verified:** `recoveryDefersUnreadableCopy` (staged-only and installed-only rows; Windows `FILE_SHARE_DELETE`-only lock) fails on the previous `importservice.cpp` and passes now. `tst_importservice` 21 cases; ctest 8/8 in Debug and Release, and repeated 3 times in each.

## 2026-09-26 — Hosted CI: `build-and-test` on Windows

- **Change:** `.github/workflows/ci.yml` runs one job, `build-and-test`, on `windows-2025-vs2026`, for pull requests to `main` and pushes to `main`. It runs the whitespace and text checks (`tools/check_text_files.py`), installs Qt 6.11.2 `win64_msvc2022_64` + `qtpdf`, downloads the public pdfbookmark SDK 0.2.0 (SHA-256 pinned), builds Release with Ninja, runs `ctest`, and runs `--sdk-check`, `--sqlite-check` and a text-PDF `--reader-check`. Permissions are read-only, no secrets are used, actions are pinned to commit SHAs, and newer runs for the same PR cancel older ones.
- **Why:** The owner asked for CI once both repositories were public. Local verification alone cannot be tied to every PR head.
- **Assumptions:**
  - The runner image's MSVC (19.51 observed) must match the SDK's toolset, because the SDK's C++ API crosses the DLL boundary; PDFMegine builds the SDK on the same image.
  - aqtinstall comes from a pinned main-branch commit (`076e165`), because release 3.3.0 cannot read Qt 6.11's per-architecture repository layout. Switch to 3.4.0 or later when released.
  - The OCR coexistence runs stay local because of run time.
- **Verified:** Runs 36252774137 (failed at the Qt install: aqtinstall 3.3.0) and 36252887138 (passed in 3.6 minutes, 8/8 tests, smoke checks passed; re-run passed in 2.5 minutes with both caches hit).

## 2026-09-26 — `scripts/verify.ps1` shared by local verification and CI; Orca workflow adopted

- **Change:** The owner's AGENTS.md section 13 edits are committed verbatim (Orca/Codex review rounds, merge authorization, `.pr-notes/` drafts). A separate commit reconciles them with hosted CI: `verify.ps1` runs locally and in CI, PR verification includes the CI run link, and merging also requires `build-and-test` to pass on the reviewed head. `scripts/verify.ps1` is the single entry point (text checks, build, `ctest`, smoke checks), and CI now runs it instead of separate steps, which also removed `ilammy/msvc-dev-cmd`.
- **Why:** The owner wants both hosted CI and local verification by implementer and reviewer, and the review workflow needs one command whose result means the same everywhere.
- **Assumptions:** CI verifies GitHub's merge-test commit for a PR (`refs/pull/N/merge`), which the script prints. The `orca` CLI is not available in this session, so review rounds are started from an Orca-hosted session.
- **Verified:** Locally, `pwsh scripts/verify.ps1 -SdkDir …\0.2.0 -Clean` gave `VERIFY PASSED` in 39 s, and a bad `-SdkDir` gave `VERIFY FAILED`, exit 1. In CI, run 36253866960 at head `50fb8f3` (merge commit `5fae0b9`) passed in 1 min 48 s.
- **Superseded:** The Orca workflow part was dropped; see the next entry. `verify.ps1` and CI stay.

## 2026-09-26 — Orca review workflow dropped; original GitHub flow restored

- **Change:** AGENTS.md is restored to the `main` version: the section 13 GitHub flow with review by a separate ChatGPT session, and merges by the owner or with the owner's explicit authorization for a named PR. `docs/agents/pr-reviewer.md` is removed. No `orca` commands are used.
- **Why:** The owner decided to stop using Orca after its `worker-start` refused the reviewer effort the workflow required (Orca 1.4.212 rejected `ultra` and `max` for `gpt-6-astra`), so no review round could start.
- **Removed:** Orca setup, review-round task specs, the Orca `worker_done` report, and the Orca-specific merge gate.
- **Assumptions:** `scripts/verify.ps1` stays the verification entry point for developers, reviewers and CI; that does not depend on Orca.

## 2026-09-26 — `verify.ps1`: tests always built, empty suites fail, guarded `-Clean`

- **Change:** `verify.ps1` configures with `-DMBL_BUILD_TESTS=ON` and runs `ctest --no-tests=error`. `-Clean` normalizes the build path and deletes only a folder under `<repo>\build\` or an outside folder whose `CMakeCache.txt` names this repository as its source; the repository, its ancestors, other repository folders, and the SDK and Qt folders are refused before any step runs. It deletes with `-LiteralPath`. The new `-ValidateOnly` switch stops after that check. `tools/test_verify_guards.ps1` exercises the guard against a disposable fake repository with sentinel files, and `verify.ps1` runs it, so CI covers it.
- **Why:** PR #8 review: a reused build folder configured with `MBL_BUILD_TESTS=OFF` let `verify.ps1` print `VERIFY PASSED` with no tests, because `ctest` exits 0 when it finds none. `-Clean` would also recursively delete any `-BuildDir`, including the repository or the SDK.
- **Verified:** The guard tests pass (9 refused, 3 cleaned); with the guard call removed they fail. A build folder configured with `MBL_BUILD_TESTS=OFF` and then passed to `verify.ps1` ran all 8 tests, and its cache read `ON` afterwards. On an empty test folder `ctest` exits 0 without `--no-tests=error` and 8 with it.

## 2026-09-26 — M03 part 2: composition root and library window

- **Change:**
  - `main.cpp` is the composition root. It resolves the library folder (`--library`, then `MYBOOKSLIBRARY_ROOT`, then `AppLocalDataLocation/Library`), creates `presentation::LibraryController` and passes it to `Main.qml` as a required property.
  - The controller opens the library, runs `ImportService::recover()` and imports files on a one-thread `QThreadPool`. Results reach the GUI only through queued invocations and `QFuture::then(this, …)`.
  - `Main.qml` is replaced by the list-first library window. See UI.md.
  - `mbl_presentation` is a static QML module (`MyBooksLibrary.Presentation`), with `Q_IMPORT_QML_PLUGIN` in `main.cpp`.
  - The development options `--import` and `--screenshot` drive smoke runs.
- **Why:** The M03 gate requires a usable book list. AGENTS.md sections 1 (rule 9), 3 and 9 require a small composition root, no work in QML or on the GUI thread, GUI-thread model updates and a responsive close.
- **Assumptions:**
  - One worker thread serves opening, recovery and imports. SDK jobs get their own worker in M04, and file copying stays bounded separately.
  - Closing waits for the controller to become idle after cancelling. Cancellation is checked between 1 MiB chunks.
  - The library folder's default location is not yet configurable in the UI. The organization and application names are both "MyBooksLibrary".
- **Removed:** The Qt Creator template content of `Main.qml`.

## 2026-09-26 — M04 part 1: durable metadata jobs

- **Change:**
  - Schema 3 adds `jobs` (one open job per book and kind) and `metadata_field_details`.
  - `ProcessingCoordinator` runs one SDK call at a time on its own one-thread pool and does all catalog work on the database thread.
  - A job captures its request generation and source digest at enqueue. `completeMetadataJob` publishes, stores details, fills the page count and marks the job succeeded in one transaction, and only while the job is still `running`.
  - Reports are written before publication and removed when publication is refused; recovery removes those a stopped process left behind.
  - The SDK sits behind `MetadataExtractor`, so the coordinator is in `mbl_core` and tested with a fake.
- **Why:**
  - AGENTS.md section 8: persist jobs before running, one SDK operation at a time, reject stale, trashed or mismatched results, keep corrections made during a run, recover interrupted jobs explicitly.
  - Section 4: keep candidates, evidence and reasons, and never accept ambiguity automatically.
  - Section 5: recover staged reports.
- **Assumptions:**
  - A cancel requested while the SDK finishes wins over the result.
  - An interrupted job is requeued once, under a new generation.
  - Failed jobs are not retried automatically.
  - The model identity is empty, stored as `''`, when OCR models are unavailable.
  - Evidence is stored as JSON per field, because it is only displayed, never queried.
- **Verified:** See IMPLEMENTATION_PROGRESS.md, M04 part 1.

## 2026-09-27 — M04 part 1 review fixes (PR #9): shutdown keeps work, retry during cancel, cancel/claim race

- **Change:**
  - `ProcessingCoordinator::stop()` is the close path. It raises the flags and claims nothing more. A job it stops without a completed result becomes `interrupted` and is requeued at once (`catalog::interruptJob`, which shares its logic with `recoverJobs`). Queued jobs stay queued. `cancelAll()` stays for an explicit user "Cancel all". The destructor calls `stop()`.
  - A `cancel_requested` job no longer counts as pending. The v3 partial index `jobs_one_open` and `openJobFor` cover only `queued` and `running`, so a retry during a cancel queues a new job under a new generation.
  - Cancel flags moved to a `CancelFlags` registry shared with database tasks. `cancelJob` and `cancelAll` raise, or create, the flag in the task that records `cancel_requested`.
  - `runLoop` catches everything that `processNext` lets escape.
- **Why:**
  - PR #9 review: the documented close (`cancelAll` → idle → destroy) permanently cancelled all pending work, so the queue survived only crashes.
  - A retry during a cooperative cancel was absorbed by the dying job.
  - A cancel recorded after the worker's claim did not reach the SDK call.
  - AGENTS.md section 8 asks for exceptions to be handled at the worker boundary.
- **Assumptions:**
  - Migration 3 is unreleased (PR #9 is not merged), so it is edited in place rather than followed by a v4. A development catalog created from the earlier branch head keeps the wider index. A retry during a cancel there fails with a constraint error; delete such a test library.
  - An interrupted job is requeued immediately rather than left `running` for `recover()`, so the catalog never shows a job as running when no worker runs it.
- **Verified:** New tests `stopKeepsWorkForRestart`, `stopDoesNotDiscardACompletedResult`, `destructionDuringAJobInterruptsIt` (replacing `destructionDuringAJobCancelsIt`), `retryWhileCancellingIsNotLost`, `cancelRacingTheClaimReachesTheSdk`, `cancelAllCancelsRunningAndQueued` and `extractorExceptionFailsTheJob`. Reverting each fix makes its tests fail (see IMPLEMENTATION_PROGRESS.md). The `runLoop` catch was checked by inspection only.

## 2026-09-27 — M04 part 2: metadata jobs in the application

- **Change:**
  - `catalog::completeImport` queues the new book's metadata job in the import transaction.
  - `LibraryController` takes a `MetadataExtractor` from the composition root. It owns the `ProcessingCoordinator`, runs job recovery before starting it, and exposes `JobListModel` with cancel, retry and cancel-all commands.
  - `BookListModel` shows each book's state from its catalog summary plus its latest metadata job. `BookSummary` gains `extractedTitleStatus`, so an ambiguous title is not reported as "not found".
  - Closing calls `prepareToClose()`, which uses `ProcessingCoordinator::stop()`.
- **Why:**
  - The M04 gate: durable queue, extraction with models, independent publication, cancellation and restart persistence.
  - AGENTS.md sections 5 (pending jobs registered with the book), 8 (queued delivery, indeterminate progress, a responsive close) and 9 (nonblocking job queue with cancel and retry).
- **Assumptions:**
  - Retry is offered only for failed or cancelled jobs with no newer job of that book. A succeeded extraction is not rerun from the UI until corrections and reruns (M07).
  - Presentation text keeps the project's `(s)` plural form until translations are added.
  - The extractor is optional, so presentation tests without the SDK still run. Jobs then stay queued and are shown as waiting.
- **Verified:** See IMPLEMENTATION_PROGRESS.md, M04 part 2.

## 2026-09-27 — M04 part 2 review fixes (PR #10): linear latestJobs, complete open jobs, coalesced refresh

- **Change:**
  - `catalog::latestJobs` uses `ROW_NUMBER() OVER (PARTITION BY book_id, kind ORDER BY created_at DESC, rowid DESC)` instead of a correlated subquery.
  - `LibraryController::refresh()` also loads every open job (`listJobs(db, true, -1)`; a negative limit means no limit).
  - Refreshes are coalesced: one runs at a time, and at most one more is scheduled.
- **Why:** PR #10 review.
  - The subquery was quadratic (no index covers `(book_id, kind, created_at)`) and ran on every refresh: the reviewer measured 16.6 s at 3000 books × 3 jobs, blocking the database thread.
  - Only the 100 newest jobs were loaded, so a larger backlog was undercounted ("100 book(s) waiting" for 150) and partly not cancellable.
  - Every import and publication queued a full reload, and closing waited for all of them.
- **Assumptions:**
  - No v4 index: the window function avoids a migration, and SQLite 3.53 through QSQLITE supports it.
  - All open jobs are held in the activity model. That is bounded by the library size, and finished rows are still capped at 200.
- **Verified:** `tst_catalog::latestJobsPicksTheNewestPerBookAndKind`, `tst_librarycontroller::pendingCountIncludesTheWholeBacklog` and `refreshesAreCoalesced`. Each fails with its fix reverted: the old query took 8.0 s against a 3 s guard in Debug; without the open jobs `pendingCount` is 100 instead of 150; without coalescing, 20 reloads ran instead of 2.

## 2026-09-27 — SDK baseline moves to pdfbookmark 0.3.0

- **Change:**
  - `find_package(pdfbookmark 0.3 CONFIG REQUIRED)`.
  - CI downloads `pdfbookmark-sdk-0.3.0-win64.zip`, checked against SHA-256 `998508d4…957e744`.
  - AGENTS.md records tag `v0.3.0` = `bd46d90` (PR #4, merge `dfcd6f8`).
  - CLAUDE.md imports the 0.3.0 SDK brief.
- **Why:**
  - 0.3.0 adds `analyze_book()`, which runs metadata and TOC analysis on one session and OCRs each page once (PDFMegine issue #3). M05 will use it for the import's processing.
  - Requiring 0.3 keeps a build from silently picking up 0.2 headers that lack it.
- **Assumptions:**
  - The change is additive: only `engine/book.hpp`, the facade's `using` lines, the C API additions and `version.hpp` differ from 0.2.0. No application code changes.
  - SDK 0.2.0 stays installed next to 0.3.0 for comparison.
- **Verified:** See IMPLEMENTATION_PROGRESS.md, "SDK 0.3.0 update".

## 2026-09-27 — M05 part 1: contents jobs, one SDK call per book

- **Change:**
  - Import queues a contents job next to the metadata job.
  - When a book has both waiting, the coordinator claims both and runs `analyze_book` once. The metadata is published from the early callback; the contents after the call. Each job keeps its own generation, publication, cancel and outcome.
  - The call's cancel flag is a group flag, raised only when every job still needing it is cancelled (or at shutdown).
  - `ContentsAnalyzer` and `sdk::SdkContentsAnalyzer`; `sdk::normalizeContents`.
  - Schema 4 adds per-entry evidence and per-run parse, coverage, blocker, stop-reason and plan columns.
  - `completeTocJob` records the page count before publishing, so destinations are checked against it.
  - A refused publication is a cancellation only if the job is no longer running; an invalid result fails as `publish_failed`.
  - Analysis progress (stage, pages) is throttled and shown in the activity list and footer.
- **Why:**
  - M05 gate: the full parsed TOC and its mapping and evidence persist, with unresolved and omitted entries kept.
  - PDFMegine issue #3 and AGENTS.md section 8: run metadata first and publish it independently, then the TOC; `analyze_book` reads and OCRs each page once.
  - AGENTS.md section 4: keep hierarchy and destination states, join by entry ID, store plans and readiness separately.
- **Assumptions:**
  - Two jobs sharing one call (rather than one combined job kind) keep the existing contracts: separate generations and reruns, per-kind cancel and retry, no rebuild of the `jobs` table.
  - Pairing serves books one at a time. A large import therefore shows titles book by book, not all titles first; that is the cost of reading each page once.
  - Evidence is stored as JSON per entry because it is displayed, never queried.
  - The SDK's analysis stage names are undocumented, so the UI shows pages read, not stage names.
- **Verified:** See IMPLEMENTATION_PROGRESS.md, M05 part 1.

## 2026-09-27 — M05 part 2: book inspector and contents tree

- **Change:**
  - `BookInspector` (presentation) loads the selected book on the database thread and applies only the newest load.
  - `TocTreeModel` shows the catalog contents as a tree built in two passes.
  - `BookInspectorPane.qml` sits beside the book list, with "Title and authors" and "Contents" tabs.
  - `BookDetails` gains the active run IDs.
  - The book list's selected row uses the highlight text colour.
  - A development flag `--inspect-first` selects the first book and entry for screenshots.
- **Why:**
  - M05 gate: unresolved and omitted entries remain visible after restart.
  - AGENTS.md section 9: show the application's TOC (not the PDF's bookmarks) in a tree that allows a parent after its child and detects invalid references and cycles; explain uncertainty in plain language with technical detail available; display physical pages as index + 1.
- **Assumptions:**
  - A cycle's members go to the top level and an entry below them stays attached, so the tree is always finite and nothing is invented.
  - The metadata candidates are capped at five (the SDK lists them best first), because a noisy title page produced about 25.
  - Plural wording avoids "(s)" where the count is visible ("No page found: 1 of 5.").
  - Chapter navigation is left to M06; the pane shows the pages it will use.
- **Verified:** See IMPLEMENTATION_PROGRESS.md, M05 part 2.

## 2026-09-27 — M06 part 1: search in the application

- **Change:**
  - `SearchController` and `SearchResultsModel` (presentation) and `SearchResultsView.qml`.
  - The toolbar gets a search field and scope, Ctrl+F and Esc; the results replace the book list while a query is entered.
  - `BookListModel::processingStateOf` supplies each result's processing state.
  - Development flag: `--search <text>`.
- **Why:** AGENTS.md section 7: debounce; paginate; tag requests so stale results never replace a newer query; honest empty-state wording; show whether a book's metadata and contents are pending, partial or unavailable; include unresolved and omitted entries; never guess a page. M06 gate: a chapter-only search finds a book.
- **Assumptions:**
  - 250 ms debounce, 50 books per page, five chapter hits per book (the A3 default).
  - The query re-runs after every book-list refresh, so results follow processing; refreshes are coalesced, which bounds the cost.
  - Opening a chapter comes in part 2, with the reader.
- **Verified:** See IMPLEMENTATION_PROGRESS.md, M06 part 1.

## 2026-09-27 — M06 part 2: embedded reader, chapter navigation, reading position

- **Change:**
  - `reader::ReaderController` (presentation library) owns the reading session and document lifetime.
  - `ReaderPane.qml` holds one `PdfDocument` and a `Loader`-held `PdfMultiPageView`; the view is released before the document changes.
  - Schema 5 adds `reading_positions`, via `catalog::setReadingPosition` and `readingPosition`.
  - Entry points:
    - the inspector's Read, Open chapter, and Show contents page;
    - Open and Contents page on search hits;
    - double-click in the book list.
  - Development flag: `--read-page <n>`.
- **Why:**
  - M06 gate: a resolved hit opens the correct page, an unresolved hit offers evidence, and this survives a restart.
  - AGENTS.md section 9: reading position persistence; distinguish "Open chapter" from "Show source TOC page"; never guess a page.
  - READER.md: own document lifetime explicitly and stress opening and closing books.
- **Assumptions:**
  - The reader replaces the library view (a single window), so the layout is simple; a split reading view can come later.
  - The position is a physical page index, saved one second after paging stops and at once on switch, close or quit.
  - For an unresolved entry, the source contents page is the evidence offered. Exact text highlighting is not possible, because the SDK gives no complete geometry (AGENTS.md section 4).
- **Verified:** See IMPLEMENTATION_PROGRESS.md, M06 part 2.

## 2026-09-28 — M06 part 2 review fixes (PR #15): quit through the reader, release on real destruction

- **Change:**
  - `Main.qml`'s `onClosing` defers while a book is open, closes it through `ReaderController::close()`, and closes the window once the reader is closed and nothing is busy.
  - `ReaderController::attachView(QObject*)` replaces `viewReleased()`: the controller counts registered views and continues (queued) only after each has emitted `QObject::destroyed`.
  - Development options: `--close`, and `--read-page` without `--screenshot`.
- **Why:** The review measured that quitting with a book open destroyed the `PdfDocument` before its `PdfMultiPageView`, the teardown order READER.md associates with the M01 crash. The `Loader` only schedules the old view for deletion, so reporting the release when the `Loader` drops its item relied on event ordering.
- **Assumptions:** A view that is detached but not yet destroyed may still use its document, so it must be waited for. The window stays open until the view is destroyed, which normally takes one event-loop turn.
- **Removed:** `ReaderController::viewReleased()` and the pane's `onItemChanged` / `onViewActiveChanged` release reporting.
- **Verified:** See IMPLEMENTATION_PROGRESS.md, M06 part 2, "Review fixes".

## 2026-09-28 — M07 part 1: metadata corrections and reruns in the inspector

- **Change:**
  - `BookInspector` gains correction commands for Value, Leave empty (Cleared) and Use the document's value (Auto), with validation, `saving` and `correctionError`. Title and subtitle are separate fields.
  - `LibraryController` gains `rerunMetadata` and `rerunContents`.
  - New `MetadataFieldEditor.qml`, in a dialog; the pane has a "More" menu for the reruns.
  - Catalog override years must be between 1 and 9999.
- **Why:** AGENTS.md sections 6 and 7 and the M07 gate: Auto/Value/Cleared persist, a cleared value never falls back, edits during analysis survive, and stale results cannot replace current data. The catalog already guaranteed these, but the user could not reach them.
- **Assumptions:**
  - Every correction names its book explicitly, so one can never land on a book selected while the dialog was open.
  - The editor lives in a dialog, on a copy of the field taken when it opened. A refresh caused by processing recreates the field list and would otherwise destroy what the user is typing.
  - Reruns keep the shown results and the corrections until the new run publishes (a new generation per enqueue). A rerun asked for while one is waiting or running returns the open job instead of starting another.
  - TOC edits come in part 2. They need their own schema, and SDK entry IDs are not stable across reruns (AGENTS.md section 6).
- **Verified:** See IMPLEMENTATION_PROGRESS.md, M07 part 1.

## 2026-09-28 — M07 part 2a: contents edits as revisions based on a run

- **Change:**
  - Schema 6 stores contents edits as immutable numbered revisions, each a full entry list based on one TOC run; `books.active_toc_revision_id` selects the one in effect.
  - New `catalog/tocedits.*`: `editToc`, `keepTocEdits`, `useAnalyzedToc` and `tocRevisions`.
  - `publishToc` carries edits to a new run whose entries are the same in content, and otherwise leaves the book needing reconciliation.
  - `bookDetails` and the search projection use the effective contents.
- **Why:** AGENTS.md §6: TOC edits belong to an asset, run and entry revision; SDK entry IDs are not stable across reruns; preserve earlier edited results and require explicit reconciliation; never transfer edits by position or title similarity alone. §7: the index follows edits in the same transaction. M07 gate: edits persist, edits made during analysis survive, and stale results cannot replace current data.
- **Assumptions:**
  - **Full lists, not deltas:** a revision stores the whole entry list, not a list of changes. Contents are small, and a full list can be read, searched and later exported without replaying anything.
  - **Keys:** entries keep the key they had in the book's first revision (the SDK ID of that run), so parents and later edits refer to something stable. Added entries get `added-<uuid>`.
  - **Carrying over:** edits move to a new run only when every entry is the same in title, printed label, page, source page and structure. With a full match, pairing entries by position is not a guess. Otherwise the user reconciles; the edited contents stay in effect meanwhile, so a rerun never silently replaces the user's work.
  - **Stale edits:** a caller must pass the run and revision it showed. A newer run or edit makes the edit fail with `StaleGeneration` instead of overwriting.
  - **Removal hides, never deletes:** a removed entry and its sub-entries stay in the revision, are not searched, and can be restored. A sub-entry cannot be restored under a removed parent.
- **Verified:** See IMPLEMENTATION_PROGRESS.md, M07 part 2a.

## 2026-09-28 — M07 part 2b: editing contents and reconciling in the inspector

- **Change:**
  - `BookInspector` gains the contents-edit commands, the reconciliation commands and their state.
  - `TocTreeModel` shows removed entries and what the user changed.
  - `BookInspectorPane.qml` gains an edit bar on the selected entry, an entry dialog, a banner for edited contents, and Discard after a confirmation.
- **Why:** M07 gate (TOC edits persist and survive reanalysis; stale results cannot replace current data) and AGENTS.md §6 (explicit reconciliation), for the user. §9: show the reasons for uncertainty in understandable language, so a page the user set is no longer explained with the analysis's reasons for "no page".
- **Assumptions:**
  - **Levels by Indent and Outdent**, relative to the entry above, instead of a parent picker. That covers fixing a wrong level, the common case, without a second tree to choose from.
  - **Stale views are refused.** Each change carries the run and revision the tree shows; a change based on older contents is refused, the contents are reloaded, and the user repeats the action.
  - **Removed entries stay in the tree**, struck through, so they can be restored. Search, the summary and the list's count leave them out.
  - **Reconciliation shows counts, not a diff.** The banner gives the newer analysis's entry count; comparing the two entry by entry is left for later ("Not yet" in UI.md).
- **Verified:** See IMPLEMENTATION_PROGRESS.md, M07 part 2b.

## 2026-09-28 — M08 part 1: collections, and trash that ends and restores work

- **Change:**
  - Schema 7 adds collections (membership only) and `books.trashed_at`, with the new `catalog/collections.*`.
  - Trash ends the book's jobs in its own transaction; restore ends stale open jobs and queues new jobs for components without results.
  - `finishJob` keeps the outcome `trashed`.
  - Search can be limited to a collection.
- **Why:** AGENTS.md §6 (Trash is reversible: hide from search, invalidate pending generations, prevent late completion from restoring anything; restoration rebuilds visibility) and the M08 gate (collections without duplicate files; queue, cancellation and restore races handled). §9: library and collection navigation.
- **Assumptions:**
  - **Trash ends jobs in the catalog, not only at claim time.** Otherwise a job running at trash time stays `running` with a stale generation, and the one-open-job-per-kind rule would make a restore reuse it: its result is refused, and the book would be left without results or a job. Now it is `cancel_requested` and does not count as open.
  - **The trash reason survives the worker.** The worker ends such a job as a cancellation; `finishJob` keeps outcome `trashed`, so the activity list says why.
  - **Restore resumes exactly what the trash stopped** (review of PR #19): kinds with a job ended as `trashed` since the trash time. A rule of "no published result" would retry an extraction that had failed before the trash, which AGENTS.md §8 leaves failed until the user asks, and would drop a queued rerun of a book that already had results.
  - **Collection names are unique regardless of ASCII case** (SQLite `NOCASE`). Non-ASCII names differing only in case are allowed; this is documented, not hidden.
  - **Searching a collection filters after matching**, so the ranking tiers stay as SEARCH.md describes.
- **Verified:** See IMPLEMENTATION_PROGRESS.md, M08 part 1.

## 2026-09-29 — M08 part 2: organizing in the window

- **Change:**
  - The book list shows a view: the library, a collection or Trash. A sidebar switches views and manages collections.
  - The inspector's More menu adds a book to a collection, removes it from the shown collection, or moves it to Trash; a trashed book offers Restore.
  - Importing a file already in Trash offers to restore that book.
- **Why:** AGENTS.md §9 (library and collection navigation), §5 (offer restoration for a duplicate already in Trash), §6 (reversible Trash), and the M08 gate. From the PR #19 review: a trashed book's running SDK call must stop early, and restored work must start without a restart.
- **Assumptions:**
  - **Actions on the shown book:** book actions apply to the book in the inspector, so they work the same from the list and from search results. Selecting several books at once is not supported yet.
  - **Lookups know every book:** rows show the view, but titles and states are looked up over every book (active and trashed), so the Activity list and search results stay labelled in any view.
  - **Search follows a collection:** it searches the collection when one is shown, otherwise the library. Trashed books are not indexed, so the Trash view is browsed rather than searched.
  - **No confirmation for Trash:** moving a book to Trash is reversible, so it asks nothing. Deleting a collection asks, even though its books stay.
- **Verified:** See IMPLEMENTATION_PROGRESS.md, M08 part 2.
