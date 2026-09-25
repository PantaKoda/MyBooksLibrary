# Implementation progress

One active milestone at a time. Status values: **NotStarted**, **InProgress**, **LocallyVerified**, **AwaitingReview**, **Merged**, **Blocked**.

| Step | Status | Branch / PR | Notes |
| --- | --- | --- | --- |
| M00 Baseline | Merged | `feat/m00-baseline` / [PR #1](https://github.com/PantaKoda/MyBooksLibrary/pull/1), merge `46de94a` | See below |
| M01 Feasibility | InProgress (part 1 of 2 AwaitingReview; part 2 Blocked) | Part 1: `feat/m01-a3-fts5-probe` / [PR #2](https://github.com/PantaKoda/MyBooksLibrary/pull/2) | FTS5 part verified locally. Qt PDF part blocked: module not installed |
| M02–M11 | NotStarted | | |

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

### Part 2: Qt PDF availability and coexistence (reader) — Blocked

`C:\Qt\6.11.2\msvc2022_64` has no `Qt6Pdf` module (`lib/cmake/Qt6Pdf` and `bin/Qt6Pdf*.dll` are absent). Installing it through the Qt Maintenance Tool needs owner approval. Independent work (M02) can continue meanwhile.

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
- **Qt PDF is not installed** in `C:\Qt\6.11.2\msvc2022_64` (no `Qt6Pdf*`). M01 needs it; installing it requires owner approval through the Qt Maintenance Tool.
- **QSQLITE is present** (`plugins/sqldrivers/qsqlite.dll`). The FTS5 probe is part of M01.
- Outside Qt Creator, Qt DLLs must be on `PATH` to run the executable. This will be addressed by packaging (M10).

### Next action

Merged as PR #1. Continued in M01.
