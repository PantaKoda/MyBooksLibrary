# Implementation progress

One active milestone at a time. Status values: **NotStarted**, **InProgress**, **LocallyVerified**, **AwaitingReview**, **Merged**, **Blocked**.

| Step | Status | Branch / PR | Notes |
| --- | --- | --- | --- |
| M00 Baseline | AwaitingReview | `feat/m00-baseline` / [PR #1](https://github.com/PantaKoda/MyBooksLibrary/pull/1) | See below |
| M01 Feasibility | NotStarted | | Known blocker: Qt PDF is not installed (see M00 findings) |
| M02–M11 | NotStarted | | |

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

The Qt Creator build folder was not rebuilt in this step. CI is not configured.

### Findings

- **SDK deploy-script warning (report to PDFMegine):** Every build prints a CMake dev warning from `lib/cmake/pdfbookmark/pdfbookmarkDeployRuntime.cmake:12`: "Invalid escape sequence `\.`" in the regex `/pdfbookmarkd?\.dll$`, under policy CMP0010. Deployment still succeeds. The SDK is read-only here, so this needs a fix upstream.
- **Qt PDF is not installed** in `C:\Qt\6.11.2\msvc2022_64` (no `Qt6Pdf*`). M01 needs it; installing it requires owner approval through the Qt Maintenance Tool.
- **QSQLITE is present** (`plugins/sqldrivers/qsqlite.dll`). The FTS5 probe is part of M01.
- Outside Qt Creator, Qt DLLs must be on `PATH` to run the executable. This will be addressed by packaging (M10).

### Next action

Owner review of the M00 PR. Then M01: an FTS5 probe through QSQLITE, plus the Qt PDF availability and coexistence check (blocked until the Qt PDF module is installed).
