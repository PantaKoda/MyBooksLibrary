# Library window (presentation)

## Structure

| Piece | Where | Role |
| --- | --- | --- |
| Composition root | `main.cpp`, `src/app/libraryroot.*` | Resolves the library folder, creates the `LibraryController`, gives it the SDK metadata extractor (`sdk::SdkMetadataExtractor`) and whether OCR models were found, passes it to `Main.qml` as the required property `library`, and opens the library. |
| `LibraryController` | `src/presentation/librarycontroller.*` | The library session. It opens the library (lock, migrations, `ImportService::recover`) and imports files on a **one-thread worker pool**. It delivers progress (throttled to about 10 updates a second) and per-file results through queued invocations, and reloads the book list through `QFuture::then(this, …)`, so models change only on the GUI thread. |
| `LibraryController` and processing | same | Once the library is open, it creates the `ProcessingCoordinator` (docs/PROCESSING.md), runs job recovery, and then starts it. Coordinator signals update the models on the GUI thread: `jobChanged` updates one row, and `metadataPublished` reloads the book list. `refresh()` loads books, each book's latest job and the recent jobs in one database task. |
| `BookListModel` | `src/presentation/booklistmodel.*` | GUI-owned list of copied `BookSummary` values. Roles: `bookId`, `title`, `titleFromFileName`, `contributors`, `processingState`. The processing state combines what the catalog holds with the book's latest metadata job. |
| `JobListModel` | `src/presentation/joblistmodel.*` | GUI-owned activity list, newest first, capped at 200 finished rows. Roles: `jobId`, `bookId`, `bookTitle`, `kindText`, `state`, `stateText`, `detail`, `running`, `canCancel`, `canRetry`. Properties: `pendingCount` and a one-line `summary`. Updates older than the row shown (by `updatedAt`) are ignored, so a late snapshot cannot undo a newer state. |
| `Main.qml` | repository root | The list-first window. QML only reads properties and calls `importUrls`, `cancelImports`, `refresh`, `cancelJob`, `retryJob`, `cancelAllJobs` and `prepareToClose`. There is no SQL, file or SDK work in QML. |

`mbl_presentation` is a static QML module (`MyBooksLibrary.Presentation`), so `Main.qml` uses typed `LibraryController`/`BookListModel` and `qmllint` checks its member accesses. Both types are uncreatable from QML.

## Behaviour

- **Opening:** "Opening the library…" with a busy indicator. The result of recovering interrupted imports is shown in the status bar ("1 interrupted import completed"). If the library cannot be opened (for example it is already open in another instance), the reason is shown.
- **Import:** use "Import PDFs…" (a native file dialog for multiple PDFs) or drop files on the window.
  - Files queue and import one at a time off the GUI thread, with "n of m", a per-file progress bar and Cancel.
  - The batch summary ("2 imported, 1 already in the library.") and per-file problems (duplicates, duplicates in Trash, non-PDFs, failures) are listed under it.
  - File-dialog URLs are converted with `toLocalFile()`.
- **Metadata extraction:** every imported book gets a metadata job, queued in the import transaction. Jobs run one at a time and start once recovery has run.
  - The footer shows "Reading title and authors: *book*" and "*n* books waiting", with an indeterminate busy indicator. The SDK reports no metadata progress, so no percentage is shown.
  - **Activity** opens a panel listing each job's book, task and state ("Waiting", "Reading the first pages…", "Done", "Failed: …", "Cancelled", "Interrupted when the application closed; queued again"). It has **Cancel** for waiting and running jobs, **Retry** for failed or cancelled jobs that have no newer job, and **Cancel all**.
  - When OCR models were not found, the panel says that title pages that are scanned images cannot be read.
- **Book list:**
  - It shows the effective title, or the file name with "From the file name", and the processing state:
    - "Waiting to read title and authors" and "Reading title and authors…";
    - "Metadata ready";
    - "Title uncertain: several candidates" (ambiguous; never accepted automatically);
    - "No title found in the document";
    - "Title and authors could not be read" and "Metadata extraction cancelled".
    A published result stays shown while a rerun waits, runs or fails.
  - Extracted text is rendered as plain text.
  - The selection follows the book ID across refreshes.
  - Empty state: "No books yet. Choose "Import PDFs…" or drop PDF files here."
- **Closing:** closing while work runs calls `prepareToClose()`, which cancels imports and **stops** processing. Stopping is not cancelling: the running extraction is interrupted and requeued, and waiting jobs stay queued, so the next start continues them. The window shows "Finishing before closing…" and closes when the controller is idle. There is no blocking wait on the GUI thread. The SDK stops at its next checkpoint, so an OCR page in progress finishes first: closing took 6.8 s during OCR of `image-only.pdf` in Release.

## Not yet

Search field (M06), inspector with metadata evidence and contents (M05/M07), contents jobs (M05), restoring a duplicate from Trash (M08), and keyboard shortcuts beyond list navigation.

## Screenshots (Release build, `--screenshot`, library at `C:\MBL-demo\Library`)

Empty library:

![Empty library](images/m03-empty-library.png)

After importing two PDFs and a duplicate:

![Imported books](images/m03-imported-books.png)

After importing the three test PDFs with the real SDK and OCR models (`--activity` shows the panel). "Title uncertain" means the SDK reported several candidates:

![Metadata extracted](images/m04-metadata-extracted.png)
