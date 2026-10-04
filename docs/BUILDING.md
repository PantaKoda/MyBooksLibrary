# Building MyBooksLibrary

## Requirements

| Item | Value |
| --- | --- |
| Qt | 6.11.2, kit **Desktop Qt 6.11.2 MSVC2022 64bit**. MinGW cannot use the SDK's C++ API. |
| Compiler | MSVC x64 (Visual Studio 2026), C++17 |
| pdfbookmark SDK | 0.4.x (`find_package(pdfbookmark 0.4)`), installed folder containing `include/`, `lib/cmake/pdfbookmark/`, `bin/`, `share/` |

## Telling CMake where the SDK is

`CMakeLists.txt` has no machine-specific default. Use one of these (first match wins):

1. A nonempty `PDFBOOKMARK_SDK` cache variable: `-DPDFBOOKMARK_SDK=<sdk folder>`, a `CMakeUserPresets.json` in the repository root (ignored by `.gitignore`; the shared `CMakePresets.json` would stay tracked), or the Qt Creator build settings (**Projects → Build → CMake → Initial Configuration**).
2. The `PDFBOOKMARK_SDK` environment variable. It is used whenever the cached value is empty, including after a configure that failed because nothing was set, so setting the variable and re-running configure in the same build folder works.

Configuration fails with a clear message when neither is set. A nonempty cached value is never replaced by the environment variable; to switch SDKs in an existing build folder, pass `-DPDFBOOKMARK_SDK=...` again.

## Switching SDK versions

Install each SDK version in its own folder (for example `…\pdfbookmark-sdk\0.4.0\`) instead of overwriting the old one, so you can switch back. Then:

1. Point `PDFBOOKMARK_SDK` at the new folder. In an existing build folder, pass `-DPDFBOOKMARK_SDK=…` again, or change it under Qt Creator's **Projects → Build → CMake → Current Configuration** and run CMake.
2. **Rebuild from clean**: delete the build folder, or use Qt Creator's **Build → Clear CMake Configuration** and then **Rebuild All**. The C++ API passes option structs by value, and its binary interface is not guaranteed between versions (0.2.0 changed the option structs' size). Objects compiled against older headers must not be linked with the new DLL.
3. Check with `appMyBooksLibrary --sdk-check`: `sdk.header_version` and `sdk.loaded_version` must be equal.

## Qt Creator

Open `CMakeLists.txt` with the MSVC 64-bit kit, set `PDFBOOKMARK_SDK` as above, then build and run. `pdfbookmark_deploy_runtime` copies the SDK DLLs and `models/` next to the executable on every build. The tests are built into the same folder, so the ones that call the SDK wait for the app's deployment (`mbl_use_sdk_runtime` in `tests/CMakeLists.txt`) instead of copying again: several copies into one folder at once raced and failed the build.

## Command line

From an "x64 Native Tools" Visual Studio prompt, with `PDFBOOKMARK_SDK` set:

```bat
C:\Qt\6.11.2\msvc2022_64\bin\qt-cmake -S . -B build\cli-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPDFBOOKMARK_SDK="%PDFBOOKMARK_SDK%"
cmake --build build\cli-debug
```

Qt Creator's bundled tools can be used if CMake/Ninja are not on `PATH`: `C:\Qt\Tools\CMake_64\bin` and `C:\Qt\Tools\Ninja`.

Running a development build outside Qt Creator needs Qt's DLLs on `PATH` (for example `C:\Qt\6.11.2\msvc2022_64\bin`). The Windows package (below) needs nothing on `PATH`.

## Running the app

`appMyBooksLibrary` opens the library folder chosen by (first match wins):

1. `--library <dir>`;
2. the `MYBOOKSLIBRARY_ROOT` environment variable;
3. the library last opened from the app (the setting `library/last`, on Windows under `HKCU\Software\MyBooksLibrary\MyBooksLibrary`), unless it is the default one. It is opened only if it still exists, never created again;
4. the default `%LOCALAPPDATA%\MyBooksLibrary\MyBooksLibrary\Library` (Qt's `AppLocalDataLocation` plus `Library`).

The folder is created on first use, except for the remembered library. Only one running app may open a library at a time; a second one shows "already open". A backup folder (one holding `backup.json`) is never opened as a library.

`--existing-library` opens only an existing library there and never creates one: a missing folder, a folder without `library.sqlite`, or a backup fails with the reason in the window. `--remember` stores the library as `library/last` once it has opened, and never if it fails. **Library → Open library…**, **Open the default library** and **Open restored library** start the app with `--remember`; a shortcut with only `--library` leaves the remembered library alone. Scripts (`verify.ps1`, `package.ps1`) always pass `--library`, so they never read or change it.

Development options: `--import <pdf>` (repeatable) queues files once the library is open, and `--screenshot <png>` saves the window when the app is idle and then quits. They are used for smoke runs and PR screenshots. `--color-scheme light|dark` shows the window light or dark, whatever Windows is set to, and `--accent <id>` in one accent (`lapis`, `teal`, `violet`, `rose`, `graphite`, `windows`); with either option the user's Appearance choice is neither read nor stored. `-style <name>` (Qt's own option) tries another Qt Quick Controls style; the app's is FluentWinUI3 (UI.md, "Theme").

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
pwsh scripts/verify.ps1 -SdkDir C:\Dev\pdfbookmark-sdk\0.4.0              # Release, build\verify-release
pwsh scripts/verify.ps1 -Configuration Debug -Clean                        # SDK from PDFBOOKMARK_SDK
```

`-QtDir` defaults to `QT_ROOT_DIR`, then `C:\Qt\6.11.2\msvc2022_64`.

`-Clean` deletes the build folder only if the script can identify it as this project's build output: a folder under `<repo>\build\` (not `build\` itself), or a folder outside the repository whose `CMakeCache.txt` names this repository as its source. The path is normalized first, so `..` cannot escape `build\`. The repository, its ancestors, other folders inside it, and the SDK and Qt folders (and anything inside them) are refused before anything is deleted. `-ValidateOnly` stops after this check and the clean; the guard tests use it against a disposable fake repository.

## Windows package (`scripts/package.ps1`)

```powershell
pwsh scripts/package.ps1 -SdkDir C:\Dev\pdfbookmark-sdk\0.4.0
```

Qt's licence text comes from a pinned copy in `third_party/licenses/` (see its README), because CI's Qt has no `Licenses` folder; `-QtLicenseFile` overrides it. The OCR models' licence (PaddleOCR, Apache-2.0) comes from the SDK, which ships it since 0.4.0 ([PantaKoda/PDFMegine#6](https://github.com/PantaKoda/PDFMegine/issues/6)); the script refuses to package the models without it.

It builds `appMyBooksLibrary` (Release, no tests) in `build\package-release`, then writes `build\package\MyBooksLibrary\` and `build\package\MyBooksLibrary-<version>-win64.zip`. The package holds:

| Part | Where it comes from |
| --- | --- |
| `appMyBooksLibrary.exe`, the pdfbookmark DLLs, `models\` | The build folder, where `pdfbookmark_deploy_runtime` placed the SDK's runtime and OCR models. |
| Qt DLLs, `platforms\`, `sqldrivers\qsqlite.dll`, `qml\` (including `QtQuick\Pdf`) | `windeployqt --release --qmldir qml`. It ships only the SQLite driver, and leaves out software OpenGL, the D3D and DXC shader compilers (Qt Quick's Direct3D 11 backend uses precompiled shaders), QML debugging plugins and translations. |
| `vcruntime140*.dll`, `msvcp140*.dll`, … | The Visual C++ runtime, app-local, from `VCToolsRedistDir`. |
| `USER_GUIDE.md` | `docs/USER_GUIDE.md`: what every feature does, for users, and the limitations. |
| `NOTICE.txt`, `licenses\` | Qt's licence text (`C:\Qt\Licenses\LICENSE` by default, or `-QtLicenseFile`), the SBOM of **every Qt module whose files are shipped**, listing its third-party components (for example PDFium in Qt PDF), the SDK's `share\doc\pdfbookmark\licenses`, and the OCR models' licence. Each shipped Qt file is mapped to its module and each SDK DLL to its licence files. An unmapped file or a missing licence stops the script, so no notice goes missing silently. |

It then checks **what the package leaves to Windows**. It runs `dumpbin /dependents` on every shipped binary. An import that is neither shipped nor a known Windows component stops the script (delay-loaded imports only warn), which checks "runs on a clean machine" here, not only on a clean machine.

**Windows N editions:** Media Foundation (`mf.dll`, `mfplat.dll`, `mfreadwrite.dll`) is missing from Windows "N" and "KN" editions unless the Media Feature Pack is installed. SDK 0.3.0's `opencv_world500.dll` imported it directly; SDK 0.4.0's `libopencv_world500.dll` is built without it ([PantaKoda/PDFMegine#7](https://github.com/PantaKoda/PDFMegine/issues/7)), and the scan finds no shipped binary that imports it. If one ever does again, the script reports it and `NOTICE.txt` says that N editions need the Media Feature Pack.

Then it **checks the package from a copy outside the repository**, with `PATH` reduced to Windows' own folders, no Qt variables and the real platform, each run with a time limit:
- `--sdk-check`, `--sqlite-check` (FTS5);
- `--reader-check` on a text PDF and, with `--require-ocr`, on `image-only.pdf`, which uses the packaged OCR models;
- the window: `--import`, `--read-page 15`, `--export-first` (a copy with bookmarks saved to a non-ASCII name through the Export dialog's session), and `--close`.

It prints `PACKAGE PASSED` and exits 0 only if everything passed. It deletes only its own fixed output folders under `build\` and its own temporary copy.

## Continuous integration

`.github/workflows/ci.yml` runs on pull requests to `main` and on pushes to `main`. Its single job, **`build-and-test`**, is the required check. It runs on `windows-2025-vs2026` (MSVC from Visual Studio 2026, the toolset the SDK is built with):

1. sets up Qt and the SDK through the local composite action `.github/actions/setup`, which the release workflow uses too:
   - Qt 6.11.2 `win64_msvc2022_64` with the `qtpdf` extension (open-source packages, via `jurplel/install-qt-action`/aqtinstall, cached). aqtinstall comes from a pinned commit of its main branch, because release 3.3.0 cannot read Qt 6.11's repository layout;
   - the public pdfbookmark SDK release (version and SHA-256 pinned in the action; cached; never committed) into `.deps/`;
2. runs `scripts/verify.ps1 -Configuration Release`;
3. runs `scripts/package.ps1 -SkipZip`: the Windows package and its checks, as a release does.

The workflow has read-only permissions and uses no secrets, and third-party actions are pinned to commit SHAs. Newer runs for the same PR cancel older ones. The long OCR coexistence runs (`--require-ocr`) are not part of CI or `verify.ps1` (see READER.md); the package's own OCR check is a short one.

To move to a new SDK release, update `PDFBOOKMARK_SDK_VERSION` and `PDFBOOKMARK_SDK_SHA256` in `.github/actions/setup/action.yml`, together with `find_package(pdfbookmark …)`.

**Releases:** `.github/workflows/release.yml` publishes the Windows package on the Releases page when a `vX.Y.Z` tag is pushed on `main`; see docs/RELEASING.md.

## Test fixtures

`tests/fixtures/make_fixtures.py` generates small synthetic PDFs using only the Python standard library. The output is deterministic. Personal books must never be committed; `.gitignore` allows only `tests/fixtures/*.pdf`.
