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
