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

## Verification (`scripts/verify.ps1`)

`scripts/verify.ps1` is the single verification entry point for developers, reviewers and CI. In order, it runs:

1. `git diff --check` over every tracked file, and `tools/check_text_files.py` (UTF-8, no stray control characters);
2. `tools/test_verify_guards.ps1`, the regression tests for the `-Clean` guard described below;
3. configure (always with `-DMBL_BUILD_TESTS=ON`, so a reused build folder cannot silently drop the tests) and build (Ninja) of the chosen configuration;
4. `ctest --no-tests=error`, so an empty test suite fails instead of passing;
5. the application checks `--sdk-check`, `--sqlite-check` and `--reader-check` on the text-PDF fixture (`QT_QPA_PLATFORM=offscreen`).

It finds MSVC itself (through `vswhere`/`vcvars64` when `cl.exe` is not on `PATH`), along with CMake and Ninja (Qt's `Tools` folder or Visual Studio's). It prints the verified commit and exits 0 only if every step passed.

```powershell
pwsh scripts/verify.ps1 -SdkDir C:\Dev\pdfbookmark-sdk\0.2.0              # Release, build\verify-release
pwsh scripts/verify.ps1 -Configuration Debug -Clean                        # SDK from PDFBOOKMARK_SDK
```

`-QtDir` defaults to `QT_ROOT_DIR`, then `C:\Qt\6.11.2\msvc2022_64`.

`-Clean` deletes the build folder only if the script can identify it as this project's build output: a folder under `<repo>\build\` (not `build\` itself), or a folder outside the repository whose `CMakeCache.txt` names this repository as its source. The path is normalized first, so `..` cannot escape `build\`. The repository, its ancestors, other folders inside it, and the SDK and Qt folders (and anything inside them) are refused before anything is deleted. `-ValidateOnly` stops after this check and the clean; the guard tests use it against a disposable fake repository.

## Continuous integration

`.github/workflows/ci.yml` runs on pull requests to `main` and on pushes to `main`. Its single job, **`build-and-test`**, is the required check. It runs on `windows-2025-vs2026` (MSVC from Visual Studio 2026, the toolset the SDK is built with):

1. installs Qt 6.11.2 `win64_msvc2022_64` with the `qtpdf` extension (open-source packages, via `jurplel/install-qt-action`/aqtinstall, cached). aqtinstall comes from a pinned commit of its main branch, because release 3.3.0 cannot read Qt 6.11's repository layout;
2. downloads the public pdfbookmark SDK release (version and SHA-256 pinned in the workflow; cached; never committed) into `.deps/`;
3. runs `scripts/verify.ps1 -Configuration Release`.

The workflow has read-only permissions and uses no secrets, and third-party actions are pinned to commit SHAs. Newer runs for the same PR cancel older ones. The long OCR coexistence runs (`--require-ocr`) are not part of CI or `verify.ps1`; see READER.md.

To move to a new SDK release, update `PDFBOOKMARK_SDK_VERSION` and `PDFBOOKMARK_SDK_SHA256` in the workflow together with `find_package(pdfbookmark …)`.

## Test fixtures

`tests/fixtures/make_fixtures.py` generates small synthetic PDFs using only the Python standard library. The output is deterministic. Personal books must never be committed; `.gitignore` allows only `tests/fixtures/*.pdf`.
