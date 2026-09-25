# Reader feasibility (M01 part 2)

## Modules

Qt 6.11.2 msvc2022_64 now has Qt PDF: `Qt6Pdf`, `Qt6PdfQuick` and `Qt6PdfWidgets` (CMake packages and DLLs), plus the QML module `QtQuick.Pdf` (`PdfDocument`, `PdfMultiPageView`, `PdfScrollablePageView`, `PdfPageView`). The app links `Qt6::Pdf`. The QML module is loaded at runtime by the check below.

## Two PDFium copies in one process

| Component | PDFium |
| --- | --- |
| Qt PDF | Compiled into `Qt6Pdf.dll`. It imports no `pdfium.dll`, and no `FPDF_*` functions are exported (`dumpbin /exports` finds one incidental match). |
| pdfbookmark SDK | `pdfium.dll` (432 exported `FPDF` symbols), imported by `pdfbookmark.dll` |

The two copies have separate global state, so neither library's initialisation or internal locking covers the other. Qt's image plugin `imageformats/qpdf.dll` and the SDK's `qpdf30.dll` differ in name and location and do not collide.

## `appMyBooksLibrary --reader-check <pdf> [<rounds>]`

This runs without a window and prints `check.<name>=PASS|FAIL <detail>`. The exit code is the number of failures (3 if an SDK call exceeds 300 s). The SDK runs on a plain worker thread. The GUI thread never blocks on it: it keeps rendering and processing events.

| Check | What it does |
| --- | --- |
| `qtpdf_render` | Loads the PDF with `QPdfDocument`, renders every page and records a digest per page. |
| `sdk_alone` | `read_pdf_identity` + `extract_metadata` + `analyze` (AsPrinted, `allow_partial=false`, no flattening, models set) on the worker: the reference results. |
| `concurrent_view_and_analysis` | Repeats the SDK work `rounds` times on the worker while the GUI thread repeatedly creates, loads, renders and destroys `QPdfDocument`s. Every SDK result and every page digest must equal the references. |
| `cancel_while_viewing` | Starts analysis, sets the cancel flag while the viewer renders, and records how the call ended and how long after the request. |
| `shutdown_during_analysis` | Destroys the viewer and requests cancel while analysis runs; the GUI thread keeps serving events until the worker returns. |
| `qml_pdf_module` | Instantiates `PdfDocument` and `PdfMultiPageView` from `QtQuick.Pdf` and waits for `Ready`. |
| `non_ascii_path` | Copies the PDF under `Βιβλία ü/Τίτλος ü.pdf`; Qt PDF renders identically and the SDK reports the same digest. |

Fixtures (`tests/fixtures/make_fixtures.py`):
- `title-page.pdf`: 3 text pages.
- `contents-book.pdf`: 27 text pages with a printed contents page. The SDK parses and resolves all 5 entries.
- `image-only.pdf`: 4 scan-like pages with no text layer, which forces SDK page rendering and OCR.

Results are recorded in IMPLEMENTATION_PROGRESS.md (M01 part 2).

## Results (2026-09-25, Windows 11, Ryzen 9 5900X, 32 GB RAM)

| Run | Result |
| --- | --- |
| Debug, `title-page.pdf`, 3 rounds | 7/7 PASS |
| Debug, `contents-book.pdf`, 10 rounds | 7/7 PASS. SDK: plan_ready, 5 parsed, 5 resolved, identical in every round. |
| Release package outside the build tree (`windeployqt` + SDK runtime, `PATH` = System32 only), `contents-book.pdf`, 20 rounds | 7/7 PASS; cancellation observed (`cancelled`) |
| Release, `image-only.pdf` (OCR), 2 rounds, `no-view` | 7/7 PASS. Rounds took 284 s and were identical; cancel honoured **18.9 s** after the request. |
| Release, `image-only.pdf` (OCR), 1 round, viewing concurrently | **Process terminated, 3 of 3 runs**: exception `0xE0000008` on the **GUI thread inside `Qt6Pdf.dll`** during `QPdfDocument::render`, while process private memory was ~8.98 GB. |

`0xE0000008` is the out-of-memory termination code used by Chromium's allocator, which PDFium uses: an allocation failed inside Qt PDF's embedded PDFium and the whole process was terminated. The SDK's own run completes on the same input when Qt PDF is idle.

### Memory profile of SDK OCR (Release, `image-only.pdf`, 4 pages, sampled every 3 s)

- Private memory rises to about **4.5 GB** within seconds of the first OCR call and stays there between calls (likely resident OCR models).
- During each `extract_metadata` / `analyze` call it peaks at about **8.9 GB**, then returns to about 4.5 GB.
- Throughput is about 18 s per scanned page in both Debug and Release builds (similar numbers for a 12-page variant: 3 min 15 s in Release).
- System commit was 37.7 GB of a 55.9 GB limit with the app not running. The exact failing allocation could not be observed from outside, because peaks between samples may be higher.

## Blocker (recorded, not resolved)

**Embedded Qt PDF viewing is not safe while SDK OCR runs in the same process on this machine.** In-app page navigation (M06) depends on it. Options for the owner:

1. **Upstream (PDFMegine):** reduce and bound OCR memory (resident ~4.5 GB, peak ~8.9 GB for 4 pages), and check cancellation between smaller units of work (latency currently up to one OCR page, about 19 s). The SDK must not be edited here.
2. **Application:** run SDK work in a separate helper process owned by A4, so its memory cannot take down the viewer. This changes the architecture (AGENTS.md prefers in-process modules) and needs owner approval.
3. **Interim mitigation:** pause viewer rendering while an OCR job runs. This avoids the crash but blocks reading during processing.

Text-layer PDFs (no OCR) show no problem, including 20 concurrent rounds in the packaged build.
