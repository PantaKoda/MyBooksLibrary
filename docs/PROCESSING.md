# Processing coordinator (A4)

In the application, `presentation::LibraryController` owns the coordinator; see UI.md. Each import queues its book's first **metadata** and **contents** (TOC) jobs **in the import transaction** (`catalog::completeImport`, through `detail::queueJob`), so a crash right after an import cannot lose the requests. The controller creates the coordinator when the library opens, runs `recover()`, then `start()`, and calls `start()` again after each import.

`processing::ProcessingCoordinator` runs the durable processing jobs: metadata jobs (`MetadataExtractor`) and contents jobs (`ContentsAnalyzer`). Without an analyzer, a claimed contents job fails with outcome `unsupported`.

## One SDK call for both jobs of a book

When the worker claims a metadata job and the same book has a contents job queued, it claims that one too (`catalog::claimQueuedJob`) and serves both with **one** `ContentsAnalyzer::analyzeBook` call (SDK `analyze_book`). The pages are read, and OCR'd, once; on the 4-page scanned fixture this halves the time (about 58 s → 29 s). A book whose metadata job runs alone uses `MetadataExtractor::extract`; a contents job whose metadata job is not waiting uses `ContentsAnalyzer::analyze`.

Each job still keeps its own generation, publication, cancel and outcome:

- **Metadata first.** `analyzeBook` calls back as soon as the metadata stage is done, before the contents analysis starts. The coordinator publishes the metadata right then, so the title shows while the contents are still being analyzed. If the metadata stage fails, both jobs fail; if only the contents analysis fails, the metadata stays published.
- **Shared cancel.** The call's cancel flag is a **group** flag (`CancelFlags::group`). It is raised only when every job that still needs the call is cancelled, or at `stop()`. A job leaves the group once it has its result (`settle`). Cancelling only the contents job during the metadata stage lets the metadata finish and publish, then stops the call. Cancelling only the metadata job lets the call continue for the contents and discards the metadata result.
- **Progress** of the SDK's `"metadata"` stage goes to the metadata job, the rest to the contents job (`jobProgress`: stage and pages acquired in it; no total exists). It is throttled to about four updates a second plus every change of stage.
- **Scheduling.** Metadata jobs are claimed before contents jobs, oldest first, so an import of several books is processed book by book, each with one call.

## Threads

| Work | Thread |
| --- | --- |
| SDK call (`MetadataExtractor::extract`) | The coordinator's one-thread `QThreadPool` (`mbl-processing`): one SDK operation at a time |
| Claiming, publishing and finishing jobs | The library's database thread (`catalog::Library::run`) |
| Report file write | The processing worker, before publication |
| Signals (`jobChanged`, `metadataPublished`, `enqueueFailed`, `busyChanged`, `idle`) | The thread that owns the coordinator (the GUI thread in the application), through queued invocations |

The worker only waits on database futures; no SQL runs on it. `extract()` receives an atomic cancel flag owned by the coordinator, which outlives the call, as the SDK's `RunControl` requires.

## Job lifecycle

States: `queued`, `running`, `cancel_requested`, `succeeded`, `failed`, `cancelled`, `interrupted`. At most one **pending** job (queued or running) exists per book and kind; a partial unique index enforces it. A `cancel_requested` job is ending and can never publish, so it does not block a new request.

1. `enqueueMetadata(book)` → `catalog::enqueueJob`. If a pending job exists it is returned unchanged. Otherwise (including while an earlier job is still `cancel_requested`), in one transaction, the book's metadata **request generation** is incremented and a queued job records that generation and the asset's SHA-256. Trashed books are refused.
2. The worker claims the next job (metadata before TOC, then oldest). Trash already ends a book's jobs (CATALOG.md, publication rules); as a safeguard, a queued job of a trashed book is still cancelled with outcome `trashed` instead of running.
3. The worker reads the managed path and calls the SDK step with the job's (or the pair's group) cancel flag.
4. The result is handled as follows:

| Result | Job state / outcome |
| --- | --- |
| Stopped by `stop()` (application shutdown) without a completed result | `interrupted` / `interrupted`, and a replacement job is queued |
| Extractor reports cancelled, or the flag was raised by a user cancel | `cancelled` / `cancelled` |
| Extractor failed (SDK error, unreadable PDF) | `failed` / `sdk_error`, with the SDK message |
| Reported digest ≠ the job's digest | `failed` / `source_mismatch` |
| Report file cannot be written | `failed` / `report_write_failed` |
| Published | `succeeded` / `published`, with the run ID |
| Refused: job no longer running (cancel requested meanwhile) | `cancelled` / `cancelled` |
| Refused: the result itself is invalid (e.g. a contents entry pointing past the last page) | `failed` / `publish_failed`, with the reason |
| Refused: newer request generation | `cancelled` / `superseded` |
| Refused: book trashed | `cancelled` / `trashed` |
| The SDK step threw | `failed` / `exception` |

A refused publication removes the report it wrote. "Not found" on the first pages is a normal completed extraction whose fields have status `not_found_in_search`, and a book without printed contents is a normal completed analysis with outcome `no_toc_found_in_search`; neither is retried.

## Publication

`catalog::completeMetadataJob` does everything in **one transaction**, and changes nothing if any check fails:

- the job must still be `running`, so a cancel requested while the SDK ran wins;
- the usual publication checks (CATALOG.md): current generation, not trashed, digest equals the asset's;
- the run and its typed fields are stored, and so are the per-field evidence, candidates and reasons (`metadata_field_details`);
- the asset's page count is set if it was unknown;
- the active metadata pointer, revision and search projection are updated;
- the job becomes `succeeded`.

Overrides are read when effective metadata is computed, so corrections made while the SDK ran are kept, including **Cleared**. The TOC component is untouched.

`catalog::completeTocJob` is the same for contents: in one transaction it records the page count if it was unknown (first, so that destinations are checked against it), publishes **every parsed entry** with its evidence (`toc_entries.evidence_json`) and the run's parse completeness, search coverage, plan blockers, stop reasons and SDK plan (`toc_runs`), updates the search projection, and marks the job succeeded. The metadata component is untouched.

## Restart and shutdown

`recover()` must run once before `start()`:

- `running` jobs become `interrupted` and a replacement job is queued with a new generation (skipped for trashed or missing books);
- `cancel_requested` jobs become `cancelled`;
- files in `reports/` named `<run-id>.json` that no run references are removed. They come from a process that stopped between writing a report and publishing its run. Other files are left alone.

Recovery is idempotent.

### Cancelling

`cancelJob` (user "Cancel") cancels a queued job at once and moves a running job to `cancel_requested`. `cancelAll` (user "Cancel all") does this for every open job. Cancellation is cooperative: the SDK checks the flag at its own checkpoints, so an OCR step in progress finishes first.

The flags live in a registry shared with database tasks. The task that records `cancel_requested` also raises (or creates, already raised) that job's flag. The worker may claim a job just before its cancel is recorded, and that SDK call still sees the cancel. The worker drops a job's flag only after its final database call, which is always ordered after any cancel task that raised it.

### Closing the application

Closing is **not** a user cancel. The application calls `stop()`, keeps a responsive closing state, and waits for `idle`/`!busy()` before destroying the coordinator.

`stop()` raises every flag and claims nothing more. A job whose SDK call returns without a completed result is closed as `interrupted` and requeued in one transaction (`catalog::interruptJob`). A result that completes anyway is still published. Queued jobs stay queued, so the next session continues where this one stopped. A job the user had already cancelled still ends `cancelled`. After `stop()`, `start()` does nothing.

The destructor calls `stop()` and waits, as a last resort for teardown.

### Worker boundary

The extractor call is wrapped: an exception fails that job with outcome `exception`, and the worker continues. Any other exception, for example one rethrown by a database future, ends the loop with a warning instead of escaping into the thread pool. A job left `running` that way is closed by `recover()` in the next session.

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

`sdk::SdkContentsAnalyzer` calls `pdfbookmark::analyze`, or `pdfbookmark::analyze_book` for a pair, with the settings of AGENTS.md section 4 for automatic analysis: `models` set explicitly, titles as printed, `allow_partial = false`, `flat_outline_for_unknown_hierarchy = false`, and the SDK's finite limits (40 pages, then 20 more, up to 200; 64 OCR attempts for the whole run). These settings only shape the plan; every parsed entry is still returned and stored. `sdk::normalizeContents`:

- keeps **every parsed entry**, joined to its mapping **by entry ID**, never by position;
- keeps hierarchy (root, known parent, unknown) and destination (resolved, ambiguous, unresolved) states, and gives a page only to resolved entries; printed labels stay labels;
- records source pages (the first one is the source TOC page), reasons, uncertain printed labels, the resolution method, alternative pages of ambiguous entries, and why the plan omitted an entry;
- marks an entry "in the export plan" when the plan (draft or ready) has a node for it, and stores the plan as `plan_to_json`.

The raw `analysis_report_json` is stored as the run's immutable report.
