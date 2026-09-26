# Implementation progress

One active milestone at a time. Status values: **NotStarted**, **InProgress**, **LocallyVerified**, **AwaitingReview**, **Merged**, **Blocked**.

| Step | Status | Branch / PR | Notes |
| --- | --- | --- | --- |
| M00 Baseline | Merged | `feat/m00-baseline` / [PR #1](https://github.com/PantaKoda/MyBooksLibrary/pull/1), merge `46de94a` | See below |
| M01 Feasibility | Merged: part 1 [PR #2](https://github.com/PantaKoda/MyBooksLibrary/pull/2) (merge `f126ad0`); part 2 [PR #4](https://github.com/PantaKoda/MyBooksLibrary/pull/4) (merge `e9d8be2`) | `feat/m01-a3-fts5-probe`, `feat/m01-reader-qtpdf-coexistence` | Qt PDF coexists; OCR memory-pressure and Qt Quick teardown risks tracked in READER.md |
| M02 Contracts/persistence | Merged | `feat/m02-a2-catalog-persistence` / [PR #3](https://github.com/PantaKoda/MyBooksLibrary/pull/3), merge `b643446` | See below |
| SDK 0.2.0 update | Merged | `chore/m01-sdk-0.2.0` / [PR #5](https://github.com/PantaKoda/MyBooksLibrary/pull/5), merge `4f87975` | See "SDK 0.2.0 update" |
| M03 Import/library shell | Merged: part 1 [PR #6](https://github.com/PantaKoda/MyBooksLibrary/pull/6) (merge `515ff43`); part 2 [PR #7](https://github.com/PantaKoda/MyBooksLibrary/pull/7) (merge `f39b141`) | `feat/m03-a1-managed-import`; `feat/m03-presentation-library-shell` | See "M03" |
| M04 Metadata jobs | Part 1 AwaitingReview ([PR #9](https://github.com/PantaKoda/MyBooksLibrary/pull/9), headless jobs); part 2 NotStarted (presentation) | `feat/m04-a4-metadata-jobs` | See "M04" |
| M05–M11 | NotStarted | | |

## M04 — Metadata jobs

### Part 1: durable queue, SDK extraction and publication (A4 + A2 + A1)

**Scope:** schema 3 (`jobs`, `metadata_field_details`); `catalog/jobs.*` (enqueue, claim, cancel, finish, transactional completion, restart recovery); `storage/reportstore.*` (immutable reports and removal of unpublished ones); `processing::ProcessingCoordinator` with the `MetadataExtractor` interface; `sdk::SdkMetadataExtractor` and `sdk::normalizeMetadata`. The application does not use them yet (part 2).

**Touched paths:** `src/domain/jobs.h`, `src/domain/ids.h`, `src/domain/metadata.h`, `src/domain/codes.cpp`, `src/catalog/jobs.*`, `src/catalog/catalog.cpp` (shared publication helpers; `''` for empty required run text), `src/catalog/catalog_internal.h`, `src/catalog/migrations.cpp`, `src/storage/reportstore.*`, `src/processing/`, `CMakeLists.txt`, `tests/processing/`, `tests/catalog/tst_migrations.cpp`, `tests/CMakeLists.txt`, `docs/`.

| Command | Result |
| --- | --- |
| `pwsh scripts/verify.ps1 -SdkDir <sdk 0.2.0>` (Release `-Clean`) and `-Configuration Debug` | VERIFY PASSED in both; 11/11 `ctest` suites; the new suites passed `--repeat until-fail:10`. CI `build-and-test` passed for head `a323e9c` (run 36274652262) |
| `tst_processingcoordinator` (fake extractor) | 16 cases, 0 failures:<br>- publishes from the worker, signals on the owner thread;<br>- report, details and page count stored;<br>- open job reused without a new generation;<br>- cancel while running, and cancel while the SDK completes anyway: nothing published, report removed;<br>- title override and cleared year set during the run survive;<br>- trash and a newer request during the run: not published;<br>- failure keeps the earlier result;<br>- digest mismatch fails;<br>- restart: running → interrupted + requeued and completed, cancel requested → cancelled, idempotent;<br>- recovery removes only unpublished reports;<br>- a trashed book's queued job never runs;<br>- metadata jobs before TOC jobs;<br>- destroying the coordinator mid-job cancels it |
| `tst_sdkmetadataextractor` (real SDK 0.2.0) | 5 cases: normalization (ambiguous not promoted, contributor order, separate years, details); `title-page.pdf` → "Practical Library Engineering", 3 pages, report kind `pdfbookmark.metadata`, models used; pre-set cancel → cancelled; `image-only.pdf` without models completes without a title; end-to-end job through the coordinator publishes |
| `tst_migrations` | `version2CatalogGainsJobs`: a schema 2 catalog upgrades and its book can be queued |
| Mutation: completion no longer requires `running`, worker ignores the flag | `cancelWhileTheSdkCompletesDoesNotPublish` fails (job `succeeded`); restored |

**Found and fixed:** publication failed with "NOT NULL constraint failed: metadata_runs.model_identity" when the model identity was empty (no OCR models), because a null `QString` binds as SQL NULL.

**Limitations:** not yet wired into the application, and no job UI (part 2). TOC jobs are not implemented (M05). There is no automatic retry: a failed job stays failed until the user asks again. `recover()` must be called before `start()`.

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
