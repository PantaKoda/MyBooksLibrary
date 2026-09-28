# Reader feasibility (M01 part 2)

## Modules

Qt 6.11.2 msvc2022_64 now has Qt PDF: `Qt6Pdf`, `Qt6PdfQuick` and `Qt6PdfWidgets` (CMake packages and DLLs), plus the QML module `QtQuick.Pdf` (`PdfDocument`, `PdfMultiPageView`, `PdfScrollablePageView`, `PdfPageView`). The app links `Qt6::Pdf`.

The QtQuick.Pdf import lives in the registered QML file `qml/reader/ReaderCheckView.qml`. `qmlimportscanner`, and therefore `windeployqt --qmldir .`, sees it there and deploys `Qt6PdfQuick.dll` and `qml/QtQuick/Pdf`. An import inside a C++ string would be invisible to the scanner.

## Two PDFium copies in one process

| Component | PDFium |
| --- | --- |
| Qt PDF | Compiled into `Qt6Pdf.dll`. It imports no `pdfium.dll`, and no `FPDF_*` functions are exported (`dumpbin /exports` finds one incidental match). |
| pdfbookmark SDK | `pdfium.dll` (432 exported `FPDF` symbols), imported by `pdfbookmark.dll` |

The two copies have separate global state, so neither library's initialisation or internal locking covers the other. Qt's image plugin `imageformats/qpdf.dll` and the SDK's `qpdf30.dll` differ in name and location and do not collide.

## `appMyBooksLibrary --reader-check`

```
appMyBooksLibrary --reader-check <pdf> [--rounds N] [--view churn|persistent|none]
                  [--require-ocr] [--no-models] [--no-control] [--timeout S]
```

This runs without a window. It prints `check.<name>=PASS|FAIL|NOT_EXERCISED <detail>`, plus flushed `sample.<phase>` memory lines every 2 s during long phases, so the data survives a crash. The exit code is the number of checks that did not pass (2 for bad arguments, 3 if an SDK call times out). The SDK runs on a plain worker thread, and the GUI thread never blocks on it.

**NOT_EXERCISED** means the scenario the check exists for did not happen. It is never a pass. The verdict rules are in `src/reader/checkverdict.*` and are unit-tested, including with an "ignores cancel" double.

| Check | What it does |
| --- | --- |
| `qtpdf_render` | Loads the PDF with `QPdfDocument`, renders every page and records a digest per page. |
| `sdk_alone` | `read_pdf_identity` + `extract_metadata` + `analyze` (AsPrinted, `allow_partial=false`, no flattening) on the worker: the reference results. It records model identity, OCR attempts and **pages with completed OCR**. With `--require-ocr`, zero completed OCR pages gives NOT_EXERCISED and the run stops. |
| `qt_only_control` | The same viewing workload for `rounds ×` the reference duration, without SDK work: separates viewer behaviour from coexistence. |
| `concurrent_{churn,persistent}_view_and_sdk` / `sdk_rounds_without_view` | SDK rounds on the worker while the GUI thread views. `churn`: each turn loads, renders every page of, and closes a new `QPdfDocument`. `persistent`: one open document renders the next page each turn. Every round must match the reference **semantic snapshot** (next section), and every rendered page must match its reference digest. |
| `cancel_while_viewing` | The worker pauses inside its first progress callback until the GUI thread has set the cancel flag, so the request is provably made during active analysis. PASS only when the SDK reports cancellation; normal completion after the request is FAIL; a lost race is NOT_EXERCISED. |
| `shutdown_during_analysis` | Same handshake: the viewer is destroyed and cancel requested while analysis is active, and the GUI thread keeps serving events until the worker returns. |
| `qml_pdf_module` | Creates `ReaderCheckView` (`PdfDocument` + `PdfMultiPageView`) and waits for `Ready`. Reports the QML import paths and the loaded `pdfquickplugin`/`Qt6PdfQuick` paths. |
| `non_ascii_path` | Copies the PDF under `Βιβλία ü/Τίτλος ü.pdf`; Qt PDF renders identically and the SDK reports the same digest and metadata snapshot. |

### Semantic snapshot (`src/processing/sdk/sdksnapshot.*`)

A canonical text of the observable results, compared by SHA-256:
- every metadata field's status and value, with ordered contributors and roles, and alternatives with their evidence pages;
- the searched pages;
- the analysis outcome, search and evidence pages, and the chosen candidate;
- every parsed entry (ID, order, title, printed reference, hierarchy);
- every mapping (status, page, method, alternative pages);
- the plan (readiness, blockers, nodes with destinations, omissions, promotions).

Diagnostics, stop reasons, reason text, OCR counters and acquired page text are excluded.

The encoding is unambiguous:
- every string is a quoted, escaped value (`"` and `\` escaped, control characters and line breaks as `\uXXXX`);
- an absent optional is the bare word `null`;
- records and lists are bracketed (`{...}`, `[...]`);
- lines are built by concatenation, so document text is never used as a format string.

`tst_sdksnapshot` checks that shifting one or all destinations with unchanged counts changes the digest, and that transient fields do not. It also checks that separator, absent-value, quote, backslash, line-break and `%N` cases cannot collide (PR #4 re-review).

Fixtures (`tests/fixtures/make_fixtures.py`):
- `title-page.pdf`: 3 text pages.
- `contents-book.pdf`: 27 text pages with a printed contents page. The SDK parses and resolves all 5 entries.
- `image-only.pdf`: 4 scan-like pages with no text layer, which forces SDK page rendering and OCR.

## Results

Windows 11 Pro 10.0.26200, Ryzen 9 5900X, 32 GB RAM; system commit limit 57.2 GB. The machine's commit charge was 42–43 GB before each run because of other applications.

### With the corrected harness (after review of `d07c0af`)

| Run | Result |
| --- | --- |
| Debug, `title-page.pdf --rounds 5` | 8/8 PASS; cancel observed 8 ms after the request, during active work |
| Debug, `contents-book.pdf --rounds 5` | 8/8 PASS; identical semantic digests across rounds; cancel observed after 18 ms |
| Release, `image-only.pdf --require-ocr --no-models` (E3) | `sdk_alone=NOT_EXERCISED` (4 pages OCR-skipped, `search_incomplete`), stops; exit 1 |
| Release, `image-only.pdf --require-ocr --view churn` (E1) | **8/8 PASS**. OCR completed on 4/4 pages; reference run 151.5 s. Qt-only control: 13,001 load/render/close cycles over 151.5 s, process private peak **31 MB**. Concurrent: 10,438 cycles (41,752 pages) during OCR, 0 differing; semantic digests equal; process private peak **8,915 MB**, system commit peak **51,875 / 57,248 MB**, minimum available physical memory **2,674 MB**. Cancel observed 290 ms after the request. |
| Release, `image-only.pdf --require-ocr --view persistent` (E2) | **8/8 PASS**. Control: 54,025 renders, peak 27 MB. Concurrent: 54,526 renders during OCR, 0 differing; process peak 8,916 MB, system commit peak 51,134 / 57,248 MB, min available physical 4,048 MB. Cancel observed after 294 ms. |
| Fresh `windeployqt --release --qmldir .` package; `PATH`=System32 only; `QML_IMPORT_PATH`, `QML2_IMPORT_PATH`, `QT_PLUGIN_PATH` unset; `contents-book.pdf --rounds 20`, `--sqlite-check`, `--sdk-check` | 8/8 PASS, exit 0, exit 0. `pdfquickplugin.dll` and `Qt6PdfQuick.dll` loaded **from the package folder**; all import paths inside the package; models found in the package. |

### Earlier runs with the first harness (`aca11ca`)

`image-only.pdf`, 1 round, churn viewing during OCR, Release: the **process was terminated in 3 of 3 runs** with exception `0xE0000008` on the GUI thread inside `Qt6Pdf.dll` during `QPdfDocument::render`, at ~8.98 GB process private memory. `0xE0000008` is the out-of-memory termination code of the Chromium allocator that PDFium uses. The same OCR work without viewing passed. System commit was not recorded in those runs.

### Memory profile of SDK OCR (`image-only.pdf`, 4 pages)

- Within each OCR-using call, process private memory rises to about 4.5 GB within seconds and then to about **8.9 GB**. It returns to tens of MB after the call. The earlier reading of "4.5 GB resident between calls" was a mid-call plateau.
- On this machine that raises the **system** commit charge by about 9 GB, to about 51–52 GB of the 57.2 GB limit, and leaves as little as 2.7 GB of physical memory available.
- Throughput is about 18 s per scanned page (151 s for metadata plus analysis of 4 pages).

## Interpretation and remaining risk

- **No evidence of a collision between the two PDFium copies.** Tens of thousands of renders ran during OCR with identical results, in both viewing patterns, and the Qt-only controls stayed flat.
- **The earlier crashes are consistent with system memory pressure** from the SDK's OCR footprint: an allocation failed in whichever component asked at the wrong moment, and that was Qt PDF. This is not proven, because commit was not recorded when they happened and no dump was taken. The 3/3-then-0/2 pattern suggests a narrow margin that depends on what else is running.
- **Risk to track (not solved here):** on a 32 GB machine with ordinary background load, one OCR job brings the system close to its commit limit. In-process viewing during OCR worked in these runs but has little headroom.

Options for the owner, reassessed:
1. **Upstream (PDFMegine):** reduce and bound OCR memory (about 9 GB peak for 4 pages at the default 300 dpi raster). This is the direct fix for the pressure.
2. **Helper process for SDK work:** isolates SDK crashes from the viewer. It does **not** protect the viewer from system-wide memory pressure, so on its own it does not address this risk.
3. **Pause viewing during OCR:** only a mitigation to verify. It avoids concurrent allocation in this process but not other processes' pressure.

Not verified: Qt Quick rendering in a visible window (only type instantiation), long books with many OCR pages, lower-memory machines, and macOS/Linux.

## SDK 0.2.0 re-verification (2026-09-25)

All checks were repeated against SDK 0.2.0. The full before and after table is in IMPLEMENTATION_PROGRESS.md, "SDK 0.2.0 update".

- The same 4-page OCR workload is 2.6× faster (57.7 s against 151.5 s) and peaks at 2.4 GB instead of 8.9 GB.
- System commit during OCR with viewing peaked at 46.5 of 57.2 GB (51.9 before).
- Cancellation took 21–28 ms.
- Viewing during OCR passed in every run: churn, persistent, 4 OCR threads, and the package.

With about 11 GB of commit headroom left, the **memory-pressure risk above is much reduced but not eliminated**. It grows with larger pages and with other applications running. The SDK brief now documents about 2.3 GB and 7 s per page at 300 dpi as observed peaks, not limits.

### GUI responsiveness

`TurnGaps` records the largest interval between GUI event-loop turns.
- Persistent viewer rendering one page per turn: 4 ms on its own, 10 ms during OCR with automatic threads (8), 7 ms with `--ocr-threads 4`. With 4 threads, OCR took 81 s instead of 58 s.
- Churn viewer (a whole document per turn): 19 ms on its own, 34 ms during OCR.

### Teardown and an unexplained Qt Quick crash

In one package run (text PDF, no OCR), the process ended with `0xC0000005` on a Qt Quick worker thread during teardown of `qml_pdf_module`. The stack was Qt6Core thread start → Qt6Quick → Qt6Gui, with no SDK module. It did not recur in 50 further package runs or in 1,500 naive create/destroy cycles (`--qml-cycles 300 --qml-naive-teardown`, 5 processes).

The check now destroys the view before its document (a `Loader` in `ReaderCheckView.qml`), but that is not a proven fix. **M06 must own document lifetime explicitly and stress-test opening and closing books in a visible window.**

## SDK 0.3.0 re-verification (2026-09-27)

All `--reader-check` checks pass against SDK 0.3.0 in Release and Debug (clean builds): render, SDK alone, concurrent viewing, cancel while viewing, shutdown during analysis, QML PDF module and non-ASCII path. 0.3.0 adds only `analyze_book()`; the other headers are unchanged. See IMPLEMENTATION_PROGRESS.md, "SDK 0.3.0 update".

## The embedded reader (M06)

`reader::ReaderController` owns the reading session and the **document lifetime**. `qml/reader/ReaderPane.qml` shows it with one `PdfDocument` that lives as long as the pane, and a `PdfMultiPageView` in a `Loader`.

- The view exists only while the controller allows it (`viewActive`) and the document is ready.
- The pane registers each view it creates with the controller (`attachView()`). To switch books or close, the controller first sets `viewActive` to false. It changes or clears the document source only after every registered view has emitted `QObject::destroyed`, and then only from a queued call, after the destruction has finished. A view that the `Loader` has merely detached and scheduled for deletion (`deleteLater`) still holds the document, so the guarantee does not depend on event ordering. **The document is never reloaded or closed under a live view.**
- **Quitting with a book open** goes through the same path: `Main.qml`'s `onClosing` refuses the first close while the reader is open, calls `prepareToClose()` (which saves the reading position) and `reader.close()`, and closes the window once the reader is closed and nothing is busy. Without this, the engine's teardown destroyed the `PdfDocument` before the `PdfMultiPageView` (measured in the PR #15 review), which is the teardown order behind the M01 crash below.
- Opening is tagged: a newer open, or a close, drops a load in flight.
- Pages are physical indices; the UI shows index + 1. The requested page is clamped to the page count.
- `PdfMultiPageView.goToPage()` scrolls only once the view has a size, but it changes `currentPage` either way. In the window, the reader gets its size only as a book opens, so a requested page stays **pending** until the view is laid out (`ReaderView.show()` in `ReaderPane.qml`). Without this, the Release app showed page 1 while reporting page 15.
- The reading position (`reading_positions`, schema 5) is saved one second after paging stops, and at once when switching books, closing, or quitting. Reopening a book resumes there.

**Stress results:**
- `tst_readerpane` runs the real pane with Qt PDF on imported copies of `contents-book.pdf` and `title-page.pdf`. It opens the requested physical page, and in 25 rounds switches between the two books at varying pages, closing and reopening every fifth round.
- Every open checks the real scroll position, not just the view's `currentPage`. One case opens while the pane has no size and lays it out afterwards.
- It passed 20 times in Debug and 20 in Release offscreen (`ctest --repeat until-fail:20`), and 10 times in a visible window (`QT_QPA_PLATFORM=windows`), without a crash, both before and after that fix.
- It runs offscreen in CI.

The M01 teardown crash (above) is therefore addressed by construction and by this stress, not only by the check's `Loader`.
