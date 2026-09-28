# Catalog (A2) and persistence

## Library folder

| Path | Owner | Purpose |
| --- | --- | --- |
| `library.sqlite` (+ `-wal`, `-shm`) | A2 | Catalog. `foreign_keys = ON` and `busy_timeout = 5000` per connection; WAL journaling (persistent) is enabled only after the schema check. |
| `library.lock` | A2 | Single-process writer lock (`QLockFile`, stale only when the owning process is gone). |
| `files/`, `reports/`, `derivatives/`, `staging/`, `cache/` | A1 | Managed files (M03 onward). The catalog stores their paths relative to the root. |

`catalog::Library::open(root)` creates the folder, takes the lock and opens the catalog on the database thread. It then checks the schema read-only (`checkCompatible`), enables WAL and applies migrations. The executor itself changes nothing persistent. An unsupported newer catalog is therefore refused before any write, and its file stays byte-identical. A second open of the same root, in this or another process, fails with `LibraryLocked`. Destroying the `Library` closes the connection and then releases the lock.

## Threading

`infrastructure::DatabaseExecutor` owns one `QThread` and the only QSQLITE connection to the catalog. Work is posted as `task(QSqlDatabase&)` and runs in submission order. Callers receive a `QFuture` of a **copied value**. `QSqlQuery` objects and the connection never leave that thread. GUI code must continue from the future (for example with `QFuture::then(context, …)`) rather than calling `result()`. Tests and the windowless modes may block on `result()`.

## Schema (version 7)

| Table | Holds |
| --- | --- |
| `assets` | Managed file identity: UUID, unique SHA-256, size, optional page count, unique relative managed path. |
| `books` | One per asset: lifecycle (`active`/`trashed`), `revision`, per-component request generations, active metadata and TOC run IDs, and original file name and path (provenance). |
| `metadata_runs`, `metadata_run_contributors` | Every metadata run, with run identity (source digest, SDK version, model identity, options, outcome, report path) and typed fields with status. Values are stored only for `resolved` fields. Contributors are ordered and have roles. |
| `metadata_overrides`, `metadata_override_contributors` | User overrides. No row means **Auto**. `value` holds the user's value; `cleared` means deliberately empty. |
| `toc_runs`, `toc_entries` | Every TOC run and **every parsed entry**: run-scoped SDK ID, order, title, hierarchy state and parent, printed label, destination state and page, source TOC page, and whether the entry is in the export plan. |
| `search_books`, `search_toc` | A3's FTS5 projections (derived; see SEARCH.md). |
| `jobs` (schema 3) | Durable processing jobs: book, kind (`metadata`/`toc`), state (`queued`, `running`, `cancel_requested`, `succeeded`, `failed`, `cancelled`, `interrupted`), the request generation and source SHA-256 captured at enqueue, attempt, outcome, error, the published run and timestamps. At most one pending (`queued` or `running`) job per book and kind (partial unique index); a `cancel_requested` job does not block a new request. A succeeded job names its run. See PROCESSING.md. |
| `toc_entries.evidence_json`, `toc_runs.parse_complete`, `search_covered_document`, `plan_blockers_json`, `stop_reasons_json`, `plan_json` (schema 4) | Per contents entry: source pages, hierarchy and destination reasons, uncertain printed label, resolution method, alternative pages, omission reason (a JSON object; `{}` for entries of earlier runs). Per contents run: parse completeness and search coverage (NULL when unknown), plan blockers and stop reasons (JSON arrays), and the SDK plan JSON. |
| `toc_edit_revisions`, `toc_edit_entries`, `books.active_toc_revision_id` (schema 6) | The user's contents edits. Each revision is immutable, numbered per book and **based on one TOC run**. Its full entry list holds:<br>- `entry_key`, stable across the book's revisions (parents refer to it);<br>- the base run's `base_sdk_entry_id`, if any;<br>- the same content columns as `toc_entries`;<br>- `edits_json`, the fields the user changed (`title`, `page`, `level`, `added`);<br>- `removed` (kept, not searched).<br>`books.active_toc_revision_id` is the revision in effect (NULL: the active run's entries). Earlier revisions are kept. |
| `collections`, `collection_books`, `books.trashed_at` (schema 7) | User collections: a name, unique regardless of ASCII case, and membership rows (collection, book, when added). **Membership only**: a book in several collections is one book with one managed file. `books.trashed_at` records when a book was moved to Trash (UTC; NULL when active or trashed before schema 7). |
| `reading_positions` (schema 5) | Per book: the zero-based physical page where it was last read, and when. Removed with the book. `catalog::setReadingPosition` checks the page against the page count when it is known. |
| `metadata_field_details` (schema 3) | Per metadata run and field: evidence, alternative candidates and reasons as JSON, normalized from the SDK report so the UI can explain a value or an ambiguity without the raw report. |
| `import_operations` (schema 2) | One row per import attempt: source path, name, size and modification time; phase (`copying`, `verified`, `registered`, `duplicate`, `failed`, `cancelled`, `abandoned`); SHA-256, size and reserved asset ID once verified; the registered or existing book; the error. Open rows (`copying`, `verified`) are recovered at startup (STORAGE.md). |

Constraints enforce the invariants: one book per asset, one asset per SHA-256, a destination page present **iff** its state is `resolved` (page 0 is valid), a parent only for `known_parent`, and unique SDK entry IDs within a run.

## Migrations

Schema versions: **1** is the catalog, metadata, contents and search projections (M02). **2** adds `import_operations` (M03). **3** adds `jobs` and `metadata_field_details` (M04). **4** adds the contents evidence and plan columns (M05), with `ALTER TABLE … ADD COLUMN` only. **5** adds `reading_positions` (M06). **6** adds the contents edit revisions (M07). **7** adds collections and the trash time (M08). Earlier catalogs upgrade in place (`tst_migrations::version1CatalogUpgradesWithDataIntact`, `version2CatalogGainsJobs`, `version3ContentsRunsLoadAfterUpgrade`, `version4CatalogGainsReadingPositions`, `version5CatalogGainsEditedContents`, `version6CatalogGainsCollections`).

Migrations live in `catalog/migrations.cpp`. `PRAGMA user_version` records the applied version. Each migration runs in its own transaction together with its `user_version` update:

- A failing migration is rolled back. The catalog stays at its previous version, and nothing is recreated or discarded.
- A catalog whose version is **newer** than the application supports is refused (`SchemaTooNew`) without modification.
- Released migrations are never edited. Changes are added as new versions.

## Publication rules

- `catalog::completeImport` inserts the asset, the book, its projection and its first queued metadata and contents jobs, and marks the import `registered`, in **one transaction**. It fails with `Duplicate`, changing nothing, if the SHA-256 is already catalogued. `registerBook` also reports `Duplicate` (previously `InvalidArgument`).
- `requestMetadataRun` / `requestTocRun` increment that component's generation and return a `PublishTicket` (book, generation, asset SHA-256).
- `publishToc` requires `TocAnalysis.outcome == RunIdentity.outcome` (otherwise `InvalidArgument`). The outcome is stored once and read back unchanged.
- `publishMetadata` / `publishToc` succeed only when all of these hold: the ticket's generation is current (`StaleGeneration` otherwise), the book is not trashed (`Trashed`), and the ticket's and run's digests equal the asset's (`SourceMismatch`). The new run, the active-run pointer, the revision bump and the search projection are written in **one transaction**.
- `enqueueJob` starts a new request generation together with its queued job; `completeMetadataJob` publishes a job's result, stores its field details, fills an unknown page count and marks the job succeeded in **one transaction**, refused unless the job is still running (PROCESSING.md). `recoverJobs` closes jobs a stopped process left open.
- `completeTocJob` publishes a contents job's result like `completeMetadataJob` (PROCESSING.md); `claimQueuedJob` claims a book's queued job of one kind so the worker can serve it in the same SDK call.
- Empty required run-identity text (for example no model identity without OCR models) is stored as `''`, never NULL.
- Metadata and TOC generations are independent, so neither publication disturbs the other component or the user's overrides.
- A TOC `known_parent` entry whose parent is missing, itself or part of a cycle is stored as `unknown`; no parent is invented. Parents may appear after their children.
- **Trash** increments both generations, records the time, removes the search rows and ends the book's jobs, in one transaction: queued jobs become `cancelled` / `trashed`, and a running job becomes `cancel_requested` with outcome `trashed`. Its result is refused, and `finishJob` keeps the reason when the worker ends it. Late results are refused, even after a restore.
- **Restore** makes the book active and re-projects it. It ends any job still open: it predates the trash, so its generation is stale, and a book trashed before schema 7 can have one. Then it **resumes exactly the jobs the trash ended**: a new job, with a new generation, for each kind with a job ended as `trashed` since the trash time (any such job for a book trashed before schema 7).
  - A job that failed or finished before the trash stays as it was, so a failed extraction is not retried on its own.
  - A queued rerun of a book that already has results is resumed.
  - A job still running from before the trash can neither block the new request nor publish. Collection memberships are kept throughout. Trash and restore are idempotent.
- `rebuildSearchIndex` recreates all projections from catalog tables in one transaction, without SDK work.

## Edited contents

`catalog/tocedits.h`. Edits never change an analysis run. **Effective contents** are the active edit revision if there is one, else the active run's entries. `bookDetails` returns them as `toc`, with `tocRevision` and, when a revision is shown, the run's own entries as `analyzedToc`. Search indexes the effective contents without removed entries, in the same transaction as each change.

- **`editToc(book, base, edits)`** applies the edits in order and saves the result as a new active revision. The edits are rename, set page or clear page, set parent or make top-level, remove (with sub-entries), restore (not under a removed parent), and add (a sibling after an entry and its sub-entries). It is refused:
  - with `StaleGeneration` when `base` (the active run and revision the caller saw) no longer matches;
  - with `InvalidArgument` for an invalid edit: empty title, page outside the document, unknown entry, or a cycle. Nothing is saved, even if earlier edits in the list were valid;
  - with `Trashed` for a trashed book.
- **A new run for a book with edited contents**, in `publishToc`, in the same transaction:
  - **Same entries in content** (titles, printed labels, pages, source pages and structure, parents compared by position in each list): the edits carry over as a new revision on the new run, tied to the new run's entries. Plan membership and evidence come from the new run.
  - **Anything else:** the edited contents stay in effect and searchable, and the book **needs reconciliation**. Edits are never moved by position or title alone, because SDK entry IDs are not stable across reruns (AGENTS.md §6).
- **Reconciliation, only by the user:**
  - `keepTocEdits` saves the edited entries as a new revision on the newer run, no longer tied to its entries.
  - `useAnalyzedToc` shows the run's entries again, and also serves as "discard my edits".
  - Both check `base` like `editToc`. The revisions stay in the catalog (`tocRevisions`).
- **Deleting a run** that a revision is based on fails (`ON DELETE NO ACTION`), so edits are never lost silently. Deleting the book removes its runs and revisions together.
- **An added entry** is hidden only when its parent is removed, never because the entry it was added after is removed.

## Collections

`catalog/collections.h`: `createCollection`, `renameCollection`, `deleteCollection`, `addToCollection`, `removeFromCollection`, `listCollections` (by name, with active book counts), `listCollectionBooks` (active books, in library order) and `collectionsOf(book)`.
- **Names:** trimmed, not empty (`InvalidArgument`), and unique regardless of ASCII case (`Duplicate`).
- **Adding books:** adding a book twice changes nothing. A trashed book cannot be added (`Trashed`). An unknown book or collection gives `NotFound`, and a call adds all its books or none.
- **Deleting a collection** removes only its memberships.
- **Trashed books** keep their memberships but are neither listed nor counted until restored.

## Effective metadata

`domain::effectiveMetadata(extracted, overrides)` computes each field:

| Override | Result |
| --- | --- |
| Auto | Extracted value if the field is `resolved`, else empty. Ambiguous values are never accepted automatically. |
| Value | The user's value. |
| Cleared | Empty, even when an extraction (current or later) has a value. |

The display title falls back to the original file name (`displayTitleFromFileName`). That fallback is display provenance, not metadata.

## Deferred to later milestones

Export plans will be added in M09 as a new migration. Permanent deletion of trashed books (removing unreferenced managed files, never external originals) is not implemented yet. Turning edited contents into an export plan is M09.
