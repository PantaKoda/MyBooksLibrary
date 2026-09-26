# Library window (presentation)

## Structure

| Piece | Where | Role |
| --- | --- | --- |
| Composition root | `main.cpp`, `src/app/libraryroot.*` | Resolves the library folder, creates the `LibraryController`, passes it to `Main.qml` as the required property `library`, and opens the library. |
| `LibraryController` | `src/presentation/librarycontroller.*` | The library session. It opens the library (lock, migrations, `ImportService::recover`) and imports files on a **one-thread worker pool**. It delivers progress (throttled to about 10 updates a second) and per-file results through queued invocations, and reloads the book list through `QFuture::then(this, …)`, so models change only on the GUI thread. |
| `BookListModel` | `src/presentation/booklistmodel.*` | GUI-owned list of copied `BookSummary` values. Roles: `bookId`, `title`, `titleFromFileName`, `contributors`, `processingState`. |
| `Main.qml` | repository root | The list-first window. QML only reads properties and calls `importUrls`, `cancelImports` and `refresh`. There is no SQL, file or SDK work in QML. |

`mbl_presentation` is a static QML module (`MyBooksLibrary.Presentation`), so `Main.qml` uses typed `LibraryController`/`BookListModel` and `qmllint` checks its member accesses. Both types are uncreatable from QML.

## Behaviour

- **Opening:** "Opening the library…" with a busy indicator. The result of recovering interrupted imports is shown in the status bar ("1 interrupted import completed"). If the library cannot be opened (for example it is already open in another instance), the reason is shown.
- **Import:** use "Import PDFs…" (a native file dialog for multiple PDFs) or drop files on the window.
  - Files queue and import one at a time off the GUI thread, with "n of m", a per-file progress bar and Cancel.
  - The batch summary ("2 imported, 1 already in the library.") and per-file problems (duplicates, duplicates in Trash, non-PDFs, failures) are listed under it.
  - File-dialog URLs are converted with `toLocalFile()`.
- **Book list:**
  - It shows the effective title, or the file name with "From the file name", and the processing state ("Imported · not analyzed yet" until M04).
  - Extracted text is rendered as plain text.
  - The selection follows the book ID across refreshes.
  - Empty state: "No books yet. Choose "Import PDFs…" or drop PDF files here."
- **Closing:** closing while work runs cancels imports, shows "Finishing before closing…" and closes when the controller is idle. There is no blocking wait on the GUI thread.

## Not yet

Search field (M06), inspector and contents (M05), job queue for metadata/TOC (M04), restoring a duplicate from Trash (M08), and keyboard shortcuts beyond list navigation.

## Screenshots (Release build, `--screenshot`, library at `C:\MBL-demo\Library`)

Empty library:

![Empty library](images/m03-empty-library.png)

After importing two PDFs and a duplicate:

![Imported books](images/m03-imported-books.png)
