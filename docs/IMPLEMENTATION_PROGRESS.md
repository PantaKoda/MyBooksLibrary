# Implementation progress

One active milestone at a time. Status values: **NotStarted**, **InProgress**, **LocallyVerified**, **AwaitingReview**, **Merged**, **Blocked**.

| Step | Status | Branch / PR | Notes |
| --- | --- | --- | --- |
| M00 Baseline | Merged | `feat/m00-baseline` / [PR #1](https://github.com/PantaKoda/MyBooksLibrary/pull/1), merge `46de94a` | See below |
| M01 Feasibility | Part 1 Merged ([PR #2](https://github.com/PantaKoda/MyBooksLibrary/pull/2), merge `f126ad0`); part 2 AwaitingReview (review fixes applied) | Part 2: `feat/m01-reader-qtpdf-coexistence` / [PR #4](https://github.com/PantaKoda/MyBooksLibrary/pull/4) (draft) | Qt PDF available and coexisting in all corrected runs; OCR memory-pressure risk tracked in READER.md |
| M02 Contracts/persistence | Merged | `feat/m02-a2-catalog-persistence` / [PR #3](https://github.com/PantaKoda/MyBooksLibrary/pull/3), merge `b643446` | See below |
| M03–M11 | NotStarted | | |

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
