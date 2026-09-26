# Processing coordinator (A4)

`processing::ProcessingCoordinator` runs the durable processing jobs. M04 part 1 covers metadata jobs. TOC jobs (M05) use the same queue; until then a claimed TOC job fails with outcome `unsupported`.

## Threads

| Work | Thread |
| --- | --- |
| SDK call (`MetadataExtractor::extract`) | The coordinator's one-thread `QThreadPool` (`mbl-processing`): one SDK operation at a time |
| Claiming, publishing and finishing jobs | The library's database thread (`catalog::Library::run`) |
| Report file write | The processing worker, before publication |
| Signals (`jobChanged`, `metadataPublished`, `enqueueFailed`, `busyChanged`, `idle`) | The thread that owns the coordinator (the GUI thread in the application), through queued invocations |

The worker only waits on database futures; no SQL runs on it. `extract()` receives an atomic cancel flag owned by the coordinator, which outlives the call, as the SDK's `RunControl` requires.

## Job lifecycle

States: `queued`, `running`, `cancel_requested`, `succeeded`, `failed`, `cancelled`, `interrupted`. At most one **open** job (queued, running or cancel requested) exists per book and kind; a partial unique index enforces it.

1. `enqueueMetadata(book)` → `catalog::enqueueJob`. If an open job exists it is returned unchanged. Otherwise, in one transaction, the book's metadata **request generation** is incremented and a queued job records that generation and the asset's SHA-256. Trashed books are refused.
2. The worker claims the next job (metadata before TOC, then oldest). Queued jobs of trashed books are cancelled with outcome `trashed` instead of running.
3. The worker reads the managed path and calls the extractor with the job's cancel flag.
4. The result is handled as follows:

| Result | Job state / outcome |
| --- | --- |
| Extractor reports cancelled, or the flag was raised | `cancelled` / `cancelled` |
| Extractor failed (SDK error, unreadable PDF) | `failed` / `sdk_error`, with the SDK message |
| Reported digest ≠ the job's digest | `failed` / `source_mismatch` |
| Report file cannot be written | `failed` / `report_write_failed` |
| Published | `succeeded` / `published`, with the run ID |
| Refused: job no longer running (cancel requested meanwhile) | `cancelled` / `cancelled` |
| Refused: newer request generation | `cancelled` / `superseded` |
| Refused: book trashed | `cancelled` / `trashed` |
| An exception escaped the worker | `failed` / `exception` |

A refused publication removes the report it wrote. "Not found" on the first pages is a normal completed extraction whose fields have status `not_found_in_search`; it is not retried.

## Publication

`catalog::completeMetadataJob` does everything in **one transaction**, and changes nothing if any check fails:

- the job must still be `running`, so a cancel requested while the SDK ran wins;
- the usual publication checks (CATALOG.md): current generation, not trashed, digest equals the asset's;
- the run and its typed fields are stored, and so are the per-field evidence, candidates and reasons (`metadata_field_details`);
- the asset's page count is set if it was unknown;
- the active metadata pointer, revision and search projection are updated;
- the job becomes `succeeded`.

Overrides are read when effective metadata is computed, so corrections made while the SDK ran are kept, including **Cleared**. The TOC component is untouched.

## Restart and shutdown

`recover()` must run once before `start()`:

- `running` jobs become `interrupted` and a replacement job is queued with a new generation (skipped for trashed or missing books);
- `cancel_requested` jobs become `cancelled`;
- files in `reports/` named `<run-id>.json` that no run references are removed. They come from a process that stopped between writing a report and publishing its run. Other files are left alone.

Recovery is idempotent.

`cancelJob` raises the job's flag and records `cancel_requested` (a queued job is cancelled at once). `cancelAll` does this for every open job. Cancellation is cooperative: the SDK checks the flag at its own checkpoints, so an OCR step in progress finishes first. The application should call `cancelAll`, keep a responsive closing state and wait for `idle`/`!busy()` before destroying the coordinator. The destructor also raises every flag and waits, as a last resort for teardown.

## SDK boundary

`sdk::SdkMetadataExtractor` (in `mbl_sdk`) calls `pdfbookmark::extract_metadata` with:

- `models` set explicitly from `find_models()`, or none when OCR is disabled;
- the SDK's finite defaults (10 pages, then up to 30; 16 OCR attempts);
- a `RunControl` borrowing the job's flag.

It converts the SDK path from `QString::toStdWString()` on Windows, or explicit UTF-8 elsewhere. `sdk::normalizeMetadata`:

- copies values only for `resolved` fields;
- keeps contributor order and roles;
- keeps publication and copyright years apart;
- turns each field's evidence, candidates and reasons into `MetadataFieldDetail`.

The raw `metadata_report_json` is stored as the run's immutable report.

Missing models are not a failure. The extraction completes, and scanned title pages then give `not_found_in_search`. The run records its options (`models: false`) and an empty model identity.
