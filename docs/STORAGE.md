# Managed file store (A1) and import

## Library folder

| Path | Purpose |
| --- | --- |
| `library.sqlite`, `library.lock` | Catalog and writer lock (A2, see CATALOG.md) |
| `files/<asset-id>/source.pdf` | Immutable managed copy of an imported PDF |
| `reports/<run-id>.json` | Immutable SDK reports, written once through `QSaveFile` before their run is published (`storage/reportstore`); unreferenced ones are removed by job recovery (PROCESSING.md) |
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
   - If a book has the same bytes: **Duplicate**, or **DuplicateInTrash** if that book is trashed. The operation is stored as `duplicate` with the digest, size and existing book (`closeImportAsDuplicate`), the staging folder is removed, and the existing book and its corrections are untouched.
   - Restoring a trashed book is left to the user.
5. **Record `verified`** with the SHA-256, size and a newly reserved asset ID.
6. **Install** with `commitMove`, a strict same-volume move of the staged file to `files/<asset>/source.pdf`.
   - On Windows this is `MoveFileExW` with `MOVEFILE_WRITE_THROUGH`, without `MOVEFILE_COPY_ALLOWED` or `MOVEFILE_REPLACE_EXISTING`; elsewhere `rename(2)` after an existence check.
   - It never copies (unlike `QFile::rename`, whose Qt 6.11 fallback copies and deletes) and never overwrites.
   - A failure here closes the operation as Failed. The external original still exists, so the user imports again.
7. **Register** in one catalog transaction (`catalog::completeImport`): the asset, the book, its search projection, and the operation's `registered` phase.
   - If another import catalogued the same bytes first, the transaction reports Duplicate, and this import's installed copy is deleted **only if no asset references it**.

Every phase transition's result is checked. If recording an outcome fails, the import reports **Failed** with that error, never a success.

The page count is unknown at import, which is valid. Metadata, TOC and page count come from later processing (M04).

## Recovery (`ImportService::recover`)

Filesystem changes and SQLite commits are not one atomic transaction, so every phase is recorded before the next filesystem step. `recover()` runs before new imports and is idempotent.

| Open operation | Action |
| --- | --- |
| `copying` (nothing verified) | Remove its staging folder; close as **abandoned**. The original was never touched, so the user can import it again. |
| `verified`, and a book now has the same SHA-256 | Close as **duplicate** first; then remove this operation's installed copy (only if no asset references it) and its staging. |
| `verified`, managed copy present with the right digest | Register it; then remove staging. |
| `verified`, staged copy present with the right digest | Remove a wrong-digest file at the destination if no asset references it. Install with `commitMove`, then register, then remove staging. If installation or registration fails, the operation **stays open** (`deferred`) with the verified stage kept, and the next recovery retries. |
| `verified`, no copy confirmed good, and a copy **could not be read** (sharing violation, permission, I/O error) | Keep every file and the `verified` phase (`deferred`). An unreadable file is not evidence of damage. |
| `verified`, both copies confirmed **missing or mismatched** | The verified bytes are gone: close as **failed** and remove the confirmed-bad leftovers. |

`storage::checkDigest` classifies each copy as missing, match, mismatch or unreadable. Only a confirmed mismatch or absence counts against a copy. A destination that cannot be read is never replaced. The verified staged copy is deleted only after a good installed copy exists, or after the bytes turn out to be catalogued already.

Afterwards, staging folders that belong to no open operation are removed, since they never hold catalogued files. Managed files that no asset references and no open operation claims are **reported only** (`RecoveryReport::orphanedManagedFiles`); they are never deleted automatically.

## Backup and restore (`storage/backup.*`, M10)

A backup is a folder "MyBooksLibrary backup *yyyy-MM-dd HHmmss*" in a folder the user chooses, outside the library:

| Entry | Holds |
| --- | --- |
| `library.sqlite` | The catalog, written by `catalog::snapshotCatalog` with `VACUUM INTO`: the committed state, WAL content included, never a copy of the live file. Rollback journal mode, so it reads without side files. |
| `files/<asset-id>/source.pdf` | Every managed source, trashed books included, copied with SHA-256 and checked against the digest recorded at import. |
| `reports/<run-id>.json` | Every report a metadata or contents run references. A missing one is listed in the manifest, not fatal. |
| `backup.json` | Format 1, creation time, schema version, book count, and the catalog and each file with its size and SHA-256. |

`derivatives/`, `cache/` and `staging/` are not included.

**Creating** (`createBackup`):
- The catalog is listed and copied in **one database task**. Every catalog write goes through that thread, and sources and reports are immutable and never removed while referenced, so the backup is consistent while imports and jobs run.
- The files are then copied on the caller's worker thread. A source whose bytes changed stops the backup.
- Everything is written into a hidden `.<name>-<id>.partial` folder, **verified** (every digest, and the catalog's `integrity_check` and schema version), and renamed into place. A cancelled or failed backup removes its partial folder, so an incomplete backup never looks like one.

**Verifying** (`verifyBackup`): the manifest's format, and paths that stay under `files/` or `reports/`, so a crafted manifest cannot name a file elsewhere. Then every file's size and digest, and the catalog.

**Restoring** (`restoreBackup`):
- Only into a **new or empty** folder, never the library in use and never inside the backup.
- The backup is verified first, and nothing is written if it fails.
- The files are copied into a hidden `.<name>-<id>.restoring` folder, each digest checked again, and the folder is moved into place.
- It is then opened as a library, which takes the lock and migrates an older catalog, and checked with `integrity_check`.
- A cancelled or failed restore leaves the target as it was.

## Tests (`tests/storage/tst_importservice.cpp`)

- a verified copy with unchanged original bytes, size and modification time, from a Greek/ü path;
- reading after the original is deleted;
- a 3.5 MiB multi-chunk copy with progress;
- a duplicate under another name keeps the existing book's correction;
- a duplicate of a trashed book is reported, not restored;
- both duplicate kinds are **stored** as `duplicate` with the digest, size and existing book; they are not open, and a restart and recovery leave them unchanged;
- a source modified mid-copy fails and leaves nothing behind;
- a missing source and a non-PDF source fail;
- cancellation;
- a **simulated crash after each phase** (begun, staged, verified, installed), followed by a restart and `recover()`: the expected outcome, clean staging, the original unchanged, recovery idempotent, and a later import behaving normally;
- a duplicate race resolved by recovery, where the referenced copy is kept;
- orphans reported but kept, and stray staging removed;
- a failed installation during recovery (a file blocking the asset folder, then a failing move) keeps the verified stage and the open operation across repeated recoveries, then completes once the obstacle is gone;
- a wrong-digest file at the destination is replaced by the verified stage;
- `commitMove` refuses an existing target;
- `checkDigest` distinguishes missing, match, mismatch and unreadable (a folder, and on Windows a file held open with `FILE_SHARE_DELETE` only);
- recovery with the only copy (staged, or installed) held unreadable but deletable: deferred twice with the `verified` phase kept and the file surviving the lock's release, then registered with the original bytes once readable (Windows).
