# MyBooksLibrary: instructions for coding agents

A Qt Quick (QML) desktop application for Windows that uses the **pdfbookmark** library to add bookmarks to PDF books from their printed table of contents, and to read book metadata (title, authors, edition, years).

## Goals

<!-- Owner: describe what the app should do, e.g. screens, workflows, what "library" means here. -->
- TODO (owner)

## Read before working

1. **This file.**
2. **The pdfbookmark SDK brief**, which has the rules for using the library: `C:/Users/mouli/Desktop/Dev/pdfbookmarkSdk/share/doc/pdfbookmark/AGENTS.md`
3. As needed, next to that brief:
   - `API.md`, the library guide;
   - `examples/qt-quick/`, a working Qt Quick integration (a `Bookmarker` QML_ELEMENT with a worker thread, progress, cancel and plan editing). Start from it rather than writing the integration from scratch.
   - `JSON_FORMATS.md`, only if you read or write the library's JSON files.

The SDK is a read-only dependency. **Never edit files in `pdfbookmarkSdk/`.** If the library seems to need a change, stop and tell the owner. It is developed in a separate repository (`PantaKoda/PDFMegine`).

## Project facts

| Item | Value |
| --- | --- |
| Qt | 6.11.2, kit **Desktop Qt 6.11.2 MSVC2022 64bit** (MinGW cannot use the library's C++ API) |
| Compiler | MSVC from Visual Studio 2026, C++17 |
| Build | CMake (`CMakeLists.txt`), built and run from Qt Creator |
| Executable target | `appMyBooksLibrary` |
| QML module URI | `MyBooksLibrary` (import it in QML to use C++ types registered with `QML_ELEMENT`) |
| pdfbookmark SDK | `C:/Users/mouli/Desktop/Dev/pdfbookmarkSdk` (v0.1.0), found through the `PDFBOOKMARK_SDK` cache variable |
| Runtime files | `pdfbookmark_deploy_runtime(appMyBooksLibrary)` copies the library DLLs and OCR models next to the exe on every build |
| UI design | `importedcontent/` is exported from Qt Design Studio / Figma. Keep generated design files separate from hand-written logic. |

## How to work in this project

- **C++ in `src/`, QML in `qml/`**, once there is more than `main.cpp` and `Main.qml`. List every file in `qt_add_qml_module` (`SOURCES` / `QML_FILES`).
- **Library calls never run on the GUI thread.** Follow the example's `Bookmarker` pattern: one worker thread, and results returned through queued invocations.
- **UI text** goes through `qsTr()` / `tr()`. Show page numbers to users as `index + 1`.
- **Never write to or overwrite the user's PDF.** The library refuses anyway. Output goes to a new file.
- **Log decisions.** For every non-trivial change, addition or removal, add an entry to `docs/DECISIONS.md` with **Change / Why / Assumptions**, plus **Removed** and **Verified** when relevant. Keep documentation in `docs/`, organised, not scattered.
- **Ask before** downloading anything, adding a dependency, or changing the Qt version or kit.
- **Don't commit** build output (`build/`), personal PDF books, or the SDK.
- **Verify** by building in Qt Creator (or with CMake, below) and running the app. Say what you actually ran; don't assume it works.

## Command-line build (optional)

From an "x64 Native Tools" Visual Studio developer prompt:
```bat
C:\Qt\6.11.2\msvc2022_64\bin\qt-cmake -S . -B build\cli-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build\cli-debug
build\cli-debug\appMyBooksLibrary.exe
```
