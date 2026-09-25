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

## Schema version 1

| Table | Holds |
| --- | --- |
| `assets` | Managed file identity: UUID, unique SHA-256, size, optional page count, unique relative managed path. |
| `books` | One per asset: lifecycle (`active`/`trashed`), `revision`, per-component request generations, active metadata and TOC run IDs, and original file name and path (provenance). |
| `metadata_runs`, `metadata_run_contributors` | Every metadata run, with run identity (source digest, SDK version, model identity, options, outcome, report path) and typed fields with status. Values are stored only for `resolved` fields. Contributors are ordered and have roles. |
| `metadata_overrides`, `metadata_override_contributors` | User overrides. No row means **Auto**. `value` holds the user's value; `cleared` means deliberately empty. |
| `toc_runs`, `toc_entries` | Every TOC run and **every parsed entry**: run-scoped SDK ID, order, title, hierarchy state and parent, printed label, destination state and page, source TOC page, and whether the entry is in the export plan. |
| `search_books`, `search_toc` | A3's FTS5 projections (derived; see SEARCH.md). |

Constraints enforce the invariants: one book per asset, one asset per SHA-256, a destination page present **iff** its state is `resolved` (page 0 is valid), a parent only for `known_parent`, and unique SDK entry IDs within a run.

## Migrations

Migrations live in `catalog/migrations.cpp`. `PRAGMA user_version` records the applied version. Each migration runs in its own transaction together with its `user_version` update:

- A failing migration is rolled back. The catalog stays at its previous version, and nothing is recreated or discarded.
- A catalog whose version is **newer** than the application supports is refused (`SchemaTooNew`) without modification.
- Released migrations are never edited. Changes are added as new versions.

## Publication rules

- `requestMetadataRun` / `requestTocRun` increment that component's generation and return a `PublishTicket` (book, generation, asset SHA-256).
- `publishToc` requires `TocAnalysis.outcome == RunIdentity.outcome` (otherwise `InvalidArgument`). The outcome is stored once and read back unchanged.
- `publishMetadata` / `publishToc` succeed only when all of these hold: the ticket's generation is current (`StaleGeneration` otherwise), the book is not trashed (`Trashed`), and the ticket's and run's digests equal the asset's (`SourceMismatch`). The new run, the active-run pointer, the revision bump and the search projection are written in **one transaction**.
- Metadata and TOC generations are independent, so neither publication disturbs the other component or the user's overrides.
- A TOC `known_parent` entry whose parent is missing, itself or part of a cycle is stored as `unknown`; no parent is invented. Parents may appear after their children.
- Trash increments both generations and removes the search rows. Late results are then refused, even after a restore. Restore re-projects the book.
- `rebuildSearchIndex` recreates all projections from catalog tables in one transaction, without SDK work.

## Effective metadata

`domain::effectiveMetadata(extracted, overrides)` computes each field:

| Override | Result |
| --- | --- |
| Auto | Extracted value if the field is `resolved`, else empty. Ambiguous values are never accepted automatically. |
| Value | The user's value. |
| Cleared | Empty, even when an extraction (current or later) has a value. |

The display title falls back to the original file name (`displayTitleFromFileName`). That fallback is display provenance, not metadata.

## Deferred to later milestones

Candidate and evidence rows for metadata (currently only in the raw report file) come in M04. Import provenance and recovery records come in M03. Durable jobs, TOC edit revisions, collections, reading position and export plans will be added in their milestones as new migrations.
