# Managed file store (A1) and import

## Library folder

| Path | Purpose |
| --- | --- |
| `library.sqlite`, `library.lock` | Catalog and writer lock (A2, see CATALOG.md) |
| `files/<asset-id>/source.pdf` | Immutable managed copy of an imported PDF |
| `reports/<run-id>.json` | Immutable SDK reports (M04) |
| `derivatives/<asset-id>/<export-id>.pdf` | Generated copies (M09) |
| `staging/<import-id>/` | An import in progress |
| `cache/` | Rebuildable artifacts only |

The catalog stores `files/...` paths relative to the root (`storage::LibraryLayout`). The original file name and location are kept as provenance (`books.original_file_name`, `books.original_path`), never as the managed file's identity. Reading works after the external original is moved or deleted.

## Import protocol (`storage::ImportService::importFile`)

This runs off the GUI thread. File work happens on the calling thread and catalog work on the database thread. The external original is only ever opened read-only.

1. **Guard:** the source must exist, and `%PDF-` must appear in its first 1024 bytes. Otherwise the import fails with no operation recorded.
2. **Record** an `import_operations` row in phase `copying` (source path, name, size and modification time).
3. **Copy and hash** into `staging/<import>/source.pdf` through a `QSaveFile`, in 1 MiB chunks with a streamed SHA-256, reporting progress and honouring cancellation.
   - The source's size and modification time must be unchanged after reading; otherwise the result is **Failed** ("changed while it was being imported").
   - The committed staged file is read again and must give the same digest.
4. **Deduplicate by exact SHA-256.** Title similarity is never used.
   - If a book has the same bytes: **Duplicate**, or **DuplicateInTrash** if that book is trashed. The staging folder is removed, and the existing book and its corrections are untouched.
   - Restoring a trashed book is left to the user.
5. **Record `verified`** with the SHA-256, size and a newly reserved asset ID.
6. **Install** by renaming the staged file to `files/<asset>/source.pdf`. It is the same filesystem, so this is atomic, and an existing target is never overwritten.
7. **Register** in one catalog transaction (`catalog::completeImport`): the asset, the book, its search projection, and the operation's `registered` phase.
   - If another import catalogued the same bytes first, the transaction reports Duplicate, and this import's installed copy is deleted **only if no asset references it**.

The page count is unknown at import, which is valid. Metadata, TOC and page count come from later processing (M04).

## Recovery (`ImportService::recover`)

Filesystem changes and SQLite commits are not one atomic transaction, so every phase is recorded before the next filesystem step. `recover()` runs before new imports and is idempotent.

| Open operation | Action |
| --- | --- |
| `copying` (nothing verified) | Remove its staging folder; close as **abandoned**. The original was never touched, so the user can import it again. |
| `verified`, and a book now has the same SHA-256 | Close as **duplicate**. Remove this operation's installed copy only if no asset references it. |
| `verified`, managed copy present with the right digest | Register it. |
| `verified`, staged copy present with the right digest | Install (rename), then register. |
| `verified`, neither copy intact | Close as **failed**; remove an unreferenced partial copy. |

Afterwards, staging folders that belong to no open operation are removed, since they never hold catalogued files. Managed files that no asset references and no open operation claims are **reported only** (`RecoveryReport::orphanedManagedFiles`); they are never deleted automatically.

## Tests (`tests/storage/tst_importservice.cpp`)

- a verified copy with unchanged original bytes, size and modification time, from a Greek/ü path;
- reading after the original is deleted;
- a 3.5 MiB multi-chunk copy with progress;
- a duplicate under another name keeps the existing book's correction;
- a duplicate of a trashed book is reported, not restored;
- a source modified mid-copy fails and leaves nothing behind;
- a missing source and a non-PDF source fail;
- cancellation;
- a **simulated crash after each phase** (begun, staged, verified, installed), followed by a restart and `recover()`: the expected outcome, clean staging, the original unchanged, recovery idempotent, and a later import behaving normally;
- a duplicate race resolved by recovery, where the referenced copy is kept;
- orphans reported but kept, and stray staging removed.
