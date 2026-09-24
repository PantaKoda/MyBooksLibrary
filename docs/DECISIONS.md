# Decisions

Significant changes, newest last. Each entry lists **Change / Why / Assumptions**, plus **Removed / Verified** when relevant.

## 2026-09-24 — M00: SDK location without a personal default

- **Change:** `PDFBOOKMARK_SDK` is a cache path initialised from the `PDFBOOKMARK_SDK` environment variable. Configuration fails with an explanatory message when it is unset.
- **Why:** AGENTS.md forbids committing personal absolute paths. An explicit failure is clearer than a `find_package` error about a missing package.
- **Assumptions:** Existing build folders already cache the path. The Qt Creator folder `build/Desktop_Qt_6_11_2_MSVC2022_64bit_Debug` has `PDFBOOKMARK_SDK:PATH=…/pdfbookmarkSdk`, so the current setup keeps working.
- **Removed:** The hard-coded personal SDK path default and the leftover `# ← added` comments.
- **Verified:** A fresh `build/cli-debug` configure with `-DPDFBOOKMARK_SDK=…` built and ran. See IMPLEMENTATION_PROGRESS.md, M00.

## 2026-09-24 — M00: SDK boundary module and `--sdk-check` harness

- **Change:** Added `src/processing/sdk/sdkinfo.{h,cpp}`, which reports the SDK identity and runs a blocking PDF probe (`read_pdf_identity` and `extract_metadata`). `main.cpp` gains a windowless `--sdk-check [<pdf>]` mode.
- **Why:** The M00 gate requires an actual SDK call from the application with the SDK identity recorded. The probe keeps SDK headers inside `src/processing/sdk/`, which is the planned A4 boundary.
- **Assumptions:** In `--sdk-check` mode no GUI exists, so running the blocking call on the main thread is acceptable. GUI code must still use a worker thread (A4, M04). Qt value types are used at the boundary. Paths go through `toStdWString()` on Windows and UTF-8 elsewhere.
- **Verified:** The fixture and a Greek/ü non-ASCII path resolved the title. A missing file exits with 1 and the SDK's message.

## 2026-09-24 — M00: Synthetic PDF fixtures

- **Change:** `tests/fixtures/make_fixtures.py` (standard library only) generates `title-page.pdf`, which is committed. `.gitignore` keeps ignoring `*.pdf` except `tests/fixtures/*.pdf`.
- **Why:** Checks need a real PDF without committing personal books or adding a PDF-generation dependency.
- **Assumptions:** Python 3 is available to regenerate fixtures. Regeneration is deterministic (SHA-256 `26f29980…0d195f`).
