# Building MyBooksLibrary

## Requirements

| Item | Value |
| --- | --- |
| Qt | 6.11.2, kit **Desktop Qt 6.11.2 MSVC2022 64bit**. MinGW cannot use the SDK's C++ API. |
| Compiler | MSVC x64 (Visual Studio 2026), C++17 |
| pdfbookmark SDK | 0.2.x (`find_package(pdfbookmark 0.2)`), installed folder containing `include/`, `lib/cmake/pdfbookmark/`, `bin/`, `share/` |

## Telling CMake where the SDK is

`CMakeLists.txt` has no machine-specific default. Use one of these (first match wins):

1. A nonempty `PDFBOOKMARK_SDK` cache variable: `-DPDFBOOKMARK_SDK=<sdk folder>`, a `CMakeUserPresets.json` in the repository root (ignored by `.gitignore`; the shared `CMakePresets.json` would stay tracked), or the Qt Creator build settings (**Projects → Build → CMake → Initial Configuration**).
2. The `PDFBOOKMARK_SDK` environment variable. It is used whenever the cached value is empty, including after a configure that failed because nothing was set, so setting the variable and re-running configure in the same build folder works.

Configuration fails with a clear message when neither is set. A nonempty cached value is never replaced by the environment variable; to switch SDKs in an existing build folder, pass `-DPDFBOOKMARK_SDK=...` again.

## Switching SDK versions

Install each SDK version in its own folder (for example `…\pdfbookmark-sdk\0.2.0\`) instead of overwriting the old one, so you can switch back. Then:

1. Point `PDFBOOKMARK_SDK` at the new folder. In an existing build folder, pass `-DPDFBOOKMARK_SDK=…` again, or change it under Qt Creator's **Projects → Build → CMake → Current Configuration** and run CMake.
2. **Rebuild from clean**: delete the build folder, or use Qt Creator's **Build → Clear CMake Configuration** and then **Rebuild All**. The C++ API passes option structs by value, and 0.2.0 changed their size. Objects compiled against older headers must not be linked with the new DLL.
3. Check with `appMyBooksLibrary --sdk-check`: `sdk.header_version` and `sdk.loaded_version` must be equal.

## Qt Creator

Open `CMakeLists.txt` with the MSVC 64-bit kit, set `PDFBOOKMARK_SDK` as above, then build and run. `pdfbookmark_deploy_runtime` copies the SDK DLLs and `models/` next to the executable on every build.

## Command line

From an "x64 Native Tools" Visual Studio prompt, with `PDFBOOKMARK_SDK` set:

```bat
C:\Qt\6.11.2\msvc2022_64\bin\qt-cmake -S . -B build\cli-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPDFBOOKMARK_SDK="%PDFBOOKMARK_SDK%"
cmake --build build\cli-debug
```

Qt Creator's bundled tools can be used if CMake/Ninja are not on `PATH`: `C:\Qt\Tools\CMake_64\bin` and `C:\Qt\Tools\Ninja`.

Running outside Qt Creator needs Qt's DLLs on `PATH` (for example `C:\Qt\6.11.2\msvc2022_64\bin`) until a deployed package exists.

## Running the app

`appMyBooksLibrary` opens the library folder chosen by (first match wins):

1. `--library <dir>`;
2. the `MYBOOKSLIBRARY_ROOT` environment variable;
3. the default `%LOCALAPPDATA%\MyBooksLibrary\MyBooksLibrary\Library` (Qt's `AppLocalDataLocation` plus `Library`).

The folder is created on first use. Only one running app may open a library at a time; a second one shows "already open".

Development options: `--import <pdf>` (repeatable) queues files once the library is open, and `--screenshot <png>` saves the window when the app is idle and then quits. They are used for smoke runs and PR screenshots.

## SDK baseline check

`appMyBooksLibrary --sdk-check [<pdf>]` runs without a window. It prints the SDK header and loaded versions and whether OCR models were found. Given a PDF, it also prints the PDF's SHA-256 and page count and runs `extract_metadata()`. It exits with 0 on success and 1 on an SDK error.

The executable uses the Windows GUI subsystem, so redirect or pipe its output:

```bat
build\cli-debug\appMyBooksLibrary.exe --sdk-check tests\fixtures\title-page.pdf > sdk-check.txt
```

## SQLite check

`appMyBooksLibrary --sqlite-check` runs without a window. It creates and queries a temporary FTS5 table through Qt's QSQLITE driver, then prints the SQLite version, compile options and each capability. It exits with 0 when search prerequisites (driver, FTS5, `bm25()`) are met and 1 otherwise. Redirect or pipe its output, as with `--sdk-check`.

## Tests

Tests use Qt Test and CTest. They are built by default; turn them off with `-DMBL_BUILD_TESTS=OFF`.

```bat
ctest --test-dir build\cli-debug --output-on-failure
```

CTest puts Qt's `bin` folder on `PATH` for each test (CMake 3.22 or later). Code shared by the app and the tests lives in the static library `mbl_core`, which contains no GUI and no SDK code.

On this Windows setup, the Qt Test plain-text logger prints nothing to a console or pipe, although the tests run and set their exit code. CTest therefore runs every test with `-o -,junitxml`, so `--output-on-failure` shows the failing assertion. When running a test by hand, use `tst_x.exe -o -,junitxml` or `-o result.txt,txt`.

## Test fixtures

`tests/fixtures/make_fixtures.py` generates small synthetic PDFs using only the Python standard library. The output is deterministic. Personal books must never be committed; `.gitignore` allows only `tests/fixtures/*.pdf`.
