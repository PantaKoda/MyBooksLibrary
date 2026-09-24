# MyBooksLibrary — instructions for the desktop application agent

This is the root instruction file for `PantaKoda/MyBooksLibrary`. It provides the product context, subsystem boundaries and implementation sequence without requiring earlier conversations or human design reports.

You own the **Qt Quick desktop application**. Another agent develops the engine in `PantaKoda/PDFMegine`. Consume its installed **pdfbookmark SDK**; do not recreate its PDF algorithms or modify the installed SDK.

## 1. Product mission and fixed rules

Build a local PDF library manager in C++17 and Qt Quick. Users import poorly named PDFs, keep managed copies, extract bibliographic metadata and the printed table of contents, search across books and chapter titles, and open the relevant page. Their results and corrections survive restart.

The first useful workflow is: **import a PDF → see its extracted title and contents → search a chapter topic → open its mapped page → restart and repeat successfully**.

Target Windows, macOS and Linux. Preserve the working Windows integration first; a platform is supported only after its native SDK, application and packaged runtime have been tested.

The following rules apply across every subsystem:

1. **Never modify, move or delete the user's external original PDF.** Import creates a verified managed copy.
2. **The managed source is immutable too.** Adding bookmarks creates a separate derivative or export. Editing catalog metadata does not edit PDF bytes.
3. **Ignore all pre-existing PDF bookmarks.** Do not use them as TOC evidence, import them into the catalog, merge them into generated output, or display them as the application's contents. There is no trusted-author exception. The SDK derives a new TOC from document evidence and replaces the exported copy's outline with the selected plan.
4. Store PDFs as files and catalog data in SQLite. Do not put PDF binaries in database BLOBs.
5. Import success, metadata availability, TOC coverage, destination resolution and export readiness are separate states. A failed analysis does not erase an imported book.
6. Keep **all parsed TOC entries**, including unresolved entries and entries omitted from an export plan. They remain useful search results.
7. Preserve user corrections during reanalysis. A deliberately cleared field is different from a field with no override.
8. Keep zero-based physical page indices separate from printed labels such as `iv`, `12` or `A-12`. Display physical page numbers as index + 1.
9. Keep the GUI responsive during copying, hashing, SDK work and database operations.
10. Initial search covers metadata and TOC titles. Do not describe it as full-book text search.

Defer cloud sync, online metadata services, full-book OCR indexing, semantic search, chat, annotation editing and automatic merging of different editions.

## 2. Start here and preserve the existing integration

Before changing code:

1. Inspect the current worktree and applicable nested instructions. Preserve existing user changes.
2. Read `docs/IMPLEMENTATION_PROGRESS.md` and `docs/DECISIONS.md` if present; create them when implementation starts.
3. Resolve the `PDFBOOKMARK_SDK` CMake cache path. Read the installed `share/doc/pdfbookmark/AGENTS.md`, then the relevant parts of `API.md` and the public headers. Consult `JSON_FORMATS.md` for persisted SDK JSON.
4. Inspect `share/doc/pdfbookmark/examples/qt-quick/`. Reuse its worker/queued-delivery pattern, subject to the limitations below.
5. Establish the current build/runtime baseline and begin the first incomplete assigned milestone. Do not recreate completed work.
6. Before implementation edits, create or resume the step's feature branch using the GitHub workflow in section 13.

This instruction file supplies the application requirements. Installed headers determine actual SDK signatures. If an SDK version differs from the baseline, document the difference; do not invent an API or silently drop a product requirement.

Reviewed baseline, 24 September 2026:

| Item | Existing value |
| --- | --- |
| Desktop repository / reviewed commit | `PantaKoda/MyBooksLibrary` / `bd48f85cd85cf16e9590d7820e449a4a07bc666c` |
| Engine repository / reviewed commit | `PantaKoda/PDFMegine` / `4499b719d7adb71e5cc372bde88d47f66d8ffebf` |
| Desktop state at review | Initial QML screen and working SDK CMake integration; catalog, search and processing UI not implemented |
| Executable / QML URI | `appMyBooksLibrary` / `MyBooksLibrary` |
| Language / documented kit | C++17 / Qt 6.11.2, Desktop MSVC2022 64bit, MSVC toolchain |
| SDK package | `find_package(pdfbookmark 0.1 CONFIG REQUIRED)` |
| Imported link target | `pdfbookmark::pdfbookmark` |
| Public facade header | `<pdfbookmark/pdfbookmark.hpp>` |
| Runtime deployment helper | `pdfbookmark_deploy_runtime(appMyBooksLibrary)` |
| SDK location setting | `PDFBOOKMARK_SDK` CMake cache variable |

The SDK package already exists. Do not add a separate `pdfbookmarkEngine` package or manually link its private backend libraries. Preserve the target names and QML URI. Replace machine-specific SDK defaults with documented cache/preset configuration without breaking the existing user's setup; do not commit personal absolute paths.

The existing Windows SDK requires compatible MSVC x64 C++ ABI/runtime settings and matching Debug/Release artifacts. Do not switch to MinGW. Native macOS/Linux SDK packages are a separate prerequisite; the Windows DLL and its current deployment helper do not establish portability.

Build through the existing Qt Creator kit or an x64 Native Tools Visual Studio prompt. A command-line Debug build is shown below; set the `PDFBOOKMARK_SDK` environment variable to the actual installed SDK directory first. Adjust the Qt executable location only to match the existing installation.

```bat
C:\Qt\6.11.2\msvc2022_64\bin\qt-cmake -S . -B build\cli-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPDFBOOKMARK_SDK="%PDFBOOKMARK_SDK%"
cmake --build build\cli-debug
build\cli-debug\appMyBooksLibrary.exe
```

## 3. Architecture and ownership registry

A **subsystem** has a coherent responsibility, explicit contracts, implementation ownership and acceptance criteria. A view, helper or adapter is a component within that architecture, not automatically another subsystem.

### Existing engine subsystems — owned by PDFMegine

| ID | Subsystem | Owns |
| --- | --- | --- |
| **S1** | **Text Acquisition** | PDF sessions, embedded text, rendering for OCR, existing OCR integration, quality/coverage decisions |
| **S2** | **TOC Detection** | Finding candidate printed-TOC pages/groups from supplied evidence |
| **S3** | **TOC Parsing** | Extracting titles, printed references, hierarchy and source evidence |
| **S4** | **Page Mapping** | Resolving printed references to physical destinations and reporting ambiguity |
| **S5** | **Bookmark Writing** | Validating plans, writing a safe PDF copy and verifying the resulting outline |
| **S6** | **Document Metadata Extraction** | Inferring title, contributors, edition and year candidates from document evidence |

Engine orchestrates S1–S6 for a document. The SDK exposes their supported client operations. The existing PaddleOCR C++/ONNX Runtime work is already integrated engine capability; do not add another OCR stack or Python service to the desktop app.

### Application subsystems — owned by MyBooksLibrary

| ID | Subsystem / owned paths | Owns | Does not own |
| --- | --- | --- | --- |
| **A1** | **Managed File Store** — `src/storage/`, `tests/storage/` | Staging, hashing, verified copies, managed paths, report/derivative files, interrupted file-operation recovery | SQL domain records, extraction, metadata inference, search ranking |
| **A2** | **Catalog** — `src/catalog/`, `tests/catalog/` | SQLite schema/migrations, book records, analysis revisions, corrections, collections, reading state, transactional publication | PDF/OCR algorithms, rendering, search ranking |
| **A3** | **Search** — `src/search/`, `tests/search/` | Derived search indexes, query handling, ranking, grouped book/chapter hits, index rebuilding | Authoritative metadata, PDF acquisition, guessed destinations |
| **A4** | **Processing Coordinator** — `src/processing/`, `tests/processing/` | Durable jobs, SDK adapter, progress/cancellation/retry, stale-result checks, coordination of A1–A3 | S1–S6 algorithms, SQL in QML, view rendering |

Supporting components:

- `src/domain/`: application value contracts, stable IDs and states.
- `src/infrastructure/`: database executor, path conversion and small shared utilities.
- `src/processing/sdk/`: the production SDK boundary; SDK headers/types stay here where practical.
- `src/presentation/`: QObject controllers and GUI-owned Qt models.
- `src/reader/`, `qml/reader/`: viewer adapter and reading/navigation UI.
- `qml/`: handwritten views. Keep generated `importedcontent/` separate from logic.

QML invokes presentation commands. Presentation uses catalog/search/coordinator contracts and the reader adapter. A4 coordinates the other subsystems and invokes the SDK. A2 owns authoritative data; A3 owns derived index logic. Neither acquires PDF text.

A2 and A3 share the database executor. Index updates accompanying a catalog mutation run **inside the same transaction**; A3 must not open a second write connection or commit independently. A2 owns migration ordering and incorporates A3's index definitions.

Use ordinary modules or internal library targets in this repository. Do not introduce services, plugin frameworks or separate repositories for A1–A4. Keep a small composition root that wires dependencies; avoid one controller owning all file, SQL, SDK and UI logic.

Work serially with one coding agent and one active milestone. Do not spawn agents unless separately requested. If ownership is later split, assign work by A1–A4 and designate one editor for shared contracts, migrations and root CMake. Runtime worker threads are still required.

## 4. SDK integration contract

Use the installed facade. The following are existing operations, not functions to reimplement:

| Operation | Required handling |
| --- | --- |
| `version()` | Record the loaded SDK version alongside header/package identity |
| `find_models()` / `models_in(dir)` | Resolve model resources; explicitly set `models` in both metadata and analysis options |
| `extract_metadata(input, options, control)` | Returns `Result<MetadataReport>`; no progress callback |
| `analyze(input, options, control, progress)` | Returns `Result<AnalysisReport>`; progress provides a stage and acquired-page count, not a total |
| `metadata_report_json(report)` | Persist original metadata evidence |
| `analysis_report_json(report, options)` | Persist original analysis evidence; both arguments are required |
| `plan_to_json` / `plan_from_json` | Persist/restore actual export plans |
| `validate_plan(plan)` | Check structural validity; this does not prove TOC completeness or source-file identity |
| `apply(input, output, plan, options, control)` | Export to a separate file; inspect the returned `committed` result |

SDK analysis and metadata reports have serializers but no corresponding public report deserializers at the baseline. Normalize results into the catalog while they are available. Restart the UI from catalog records; retain raw JSON for evidence and diagnostics.

Important interpretation rules:

- Metadata fields have `Resolved`, `Ambiguous` or `NotFoundInSearch` states. Preserve candidates, evidence and reasons. Do not turn ambiguity into an accepted title automatically.
- Preserve ordered contributor names and roles, title/subtitle, edition statement, publication year and copyright year. Do not collapse different year meanings or encode authors only as one comma-separated string.
- `AnalysisReport.parsed` and `.mapping` are optional. Join parsed entries and mapping entries **by entry ID**, never vector position.
- Hierarchy distinguishes Root, KnownParent and Unknown. Destinations distinguish Resolved, Ambiguous and Unresolved. Preserve these states.
- Plan presence does not establish readiness. `PlanReady` can describe an explicitly allowed partial plan. Store completeness, omissions and export readiness separately.
- Source references are scoped to their run. Metadata evidence supports page navigation, but does not supply complete geometry for precise text-box overlays.
- Scores are candidate rankings, not calibrated probability percentages.

Start with automatic acquisition and finite SDK budgets. Use `AsPrinted` titles, `allow_partial = false` and `flat_outline_for_unknown_hierarchy = false` for automatic analysis. These settings must not discard partial parsed results from the catalog.

Missing models or analysis limits must produce an honest capability/job state. Do not delete a copied book because OCR is unavailable. The baseline S1 input snapshot limit is 512 MiB and is not exposed by the high-level options; do not bypass it through private headers.

The SDK example demonstrates integration, not the entire application architecture. Its ready-plan-only state loses catalog-worthy entries; its tree depth logic assumes parents appear before children; its GUI-thread shutdown wait is unsuitable for a responsive application. Do not copy those limitations.

## 5. A1 — Managed File Store implementation

Use a writable library root selected through application configuration. Store relative managed paths in the catalog:

| Path | Purpose |
| --- | --- |
| `library.sqlite` | Catalog |
| `files/<asset-id>/source.pdf` | Immutable managed PDF |
| `reports/<run-id>.json` | Immutable SDK report |
| `derivatives/<asset-id>/<export-id>.pdf` | Optional generated copy |
| `staging/<operation-id>/` | Incomplete import/report operation |
| `cache/` | Rebuildable artifacts only |

Use stable UUIDs through existing Qt facilities. Preserve the original filename/location as provenance, not as the managed file's identity. Reading must work after the external original is moved or removed by the user.

Import protocol:

1. Record an import operation and allocate staging space inside the library filesystem.
2. Stream-copy and hash off the GUI thread. Check for observed source changes; close and verify staged bytes before installation.
3. Deduplicate by exact SHA-256. Reuse the existing book/asset without overwriting corrections; offer restoration for a duplicate already in Trash. Do not merge by title.
4. Install the verified source at its stable path using a same-filesystem commit operation.
5. In a short catalog transaction, register the book/asset, initial search projection and pending jobs.
6. Reconcile interrupted operations on restart. Recovery must be idempotent and must not delete referenced source files.

Filesystem changes and SQLite commits are not one atomic transaction. Persist sufficient operation phases to recover installed-but-unregistered files and staged reports. Unknown page count is valid at import; extraction failure is separate from copy failure.

A1 also validates export destinations. Protect every managed source and internal catalog/report path, including aliases, rather than relying solely on the SDK's protection of its own input. Keep generated export names sanitized and collision-safe.

## 6. A2 — Catalog implementation

Use Qt Sql and QSQLITE. One database thread owns its connection, queries and transactions. Pass copied values across threads, never `QSqlQuery`, database handles or mutable models. Keep copy/hash/OCR work outside transactions.

Enable foreign keys, use versioned migrations and guard each library with a single-process writer lock. Failed migration must not recreate or discard the catalog. Refuse an unsupported newer schema with a useful explanation.

Define application-owned contracts before wiring views: asset identity, book summary/details, metadata snapshot, TOC snapshot, job snapshot, search request/results and export request. Include stable IDs, revisions and explicit optional values. A model row number is never a persistent identifier; page zero is a valid destination.

Persist at least:

- Assets/books and import provenance/recovery records.
- Separate active metadata and TOC run IDs, with a request generation for each component.
- Analysis run identity: source digest, options, SDK/model identity, outcome/coverage and raw report reference.
- Typed metadata, ordered contributors, evidence/candidates and user overrides.
- Every parsed TOC entry: run-scoped SDK ID, order, title, hierarchy state/parent, printed label, mapping state/destination and source evidence.
- Export plan revisions, durable jobs, collections/membership and reading position as their milestones arrive.

Metadata overrides have three modes: **Auto**, **Value**, **Cleared**. Compute effective values from the active extraction and the current override. A cleared value must not fall back to an old extracted value. Filename fallback is display provenance, not extracted metadata.

TOC edits belong to a particular asset/run/entry revision. SDK entry IDs are not stable across reruns. Preserve earlier edited results and require explicit reconciliation when a new run changes entries; never transfer edits by vector position or title similarity alone.

Publishing a new component must verify asset digest, request generation and current lifecycle state, read the latest overrides, and activate catalog data plus search projection in one transaction. Keep old usable results during pending, failed or cancelled reruns. Metadata can remain available when TOC analysis fails.

Trash is reversible: hide records from search, invalidate pending generations and prevent late completion from restoring them. Restoration rebuilds visibility. Permanent deletion, when explicitly requested, affects only unreferenced managed files, never external originals.

## 7. A3 — Search implementation

Use SQLite FTS5 over effective bibliographic fields and individual TOC titles. Probe FTS5 by creating and querying a temporary FTS table through the **actual QSQLITE driver**; the SQLite version string alone is insufficient.

Keep authoritative catalog tables separate from rebuildable FTS projections. Index changes for edits, publication, trash and restore share the catalog transaction. Provide an index rebuild from catalog data without rerunning OCR.

Search behavior:

- Scopes: all indexed fields, titles, contributors/authors and contents.
- Group hits by book; show a bounded number of matching chapter titles and their context.
- Include unresolved and export-omitted entries. A resolved chapter hit opens its physical destination. Otherwise offer the source TOC page if known; do not guess a page.
- Use predictable ranking tiers and the best chapter match. Do not let a long TOC dominate simply by having more entries, or combine incomparable raw ranks from different indexes blindly.
- Bind SQL values and separately escape/compile FTS query syntax. Ordinary punctuation must not become an unexpected search operator or crash the query.
- Debounce, paginate and tag requests with a generation so stale results cannot replace a newer query.
- Support UTF-8 titles and practical terms such as `C++`, `C#`, `TCP/IP` and `HTTP/2`; document phrase/prefix behavior.

Use honest empty-state wording such as “No matches in indexed titles and contents.” Show whether a book's metadata/contents are pending, partial or unavailable.

## 8. A4 — Processing Coordinator implementation

Persist jobs before running them. Keep job state separate from available book content: Queued, Running, CancelRequested, Succeeded, Failed, Cancelled and Interrupted. Recover interrupted jobs explicitly on restart. A completed search that finds no TOC is a domain outcome, not a failure to retry forever.

Run **one SDK operation at a time** on a dedicated worker or one-thread pool. File copying can use separately bounded work. Do not put substantial database work in the SDK worker. Run metadata first and publish it independently, then analyze the TOC; batch scheduling may prioritize pending metadata jobs.

Each SDK task captures immutable inputs, book/asset/run IDs, component generation and an owned atomic cancellation flag. `RunControl` borrows its pointer, so the flag must outlive the call. Handle failed `Result` values, successful reports marked cancelled and unexpected exceptions at the worker boundary.

Deliver throttled progress and copied results through queued invocations. Update all Qt models on their owning GUI thread. Show stage/count information when available and indeterminate progress otherwise; do not fabricate percentages for metadata or export.

Before publication, reject stale generations, wrong source identities and results for trashed/deleted books. Independent metadata and TOC jobs must not erase each other's successful results. Do not overwrite corrections made while a worker was running.

On close, request cancellation and keep a responsive closing state until active calls finish. Cancellation is cooperative and may wait for in-flight OCR. Do not destroy worker targets early or use a blocking GUI-thread `waitForDone()` as normal shutdown.

Use UTF-8 for SDK text/JSON. Convert file-dialog URLs with `toLocalFile()`. On Windows construct SDK filesystem paths from `QString::toStdWString()`; elsewhere use an explicit UTF-8 filesystem conversion. Exercise non-ASCII paths.

## 9. Presentation and reader

Build a library application with a list-first workflow:

- Library/collection navigation and a persistent search field.
- Book list showing effective title, contributors and processing/coverage state.
- Inspector for metadata, corrections, evidence and the application's TOC.
- Reader workspace with page navigation and reading-position persistence.
- Nonblocking job queue/progress details with cancel/retry actions.

Use Qt models suitable for lists and TOC trees. Build tree lookup in two passes so a parent may appear after its child. Detect invalid references/cycles; preserve Unknown hierarchy rather than inventing a parent. Keep selected book, query and scroll/reading state stable through asynchronous refreshes.

Qt PDF is the intended embedded viewer behind an adapter. Verify module availability and actual coexistence with SDK analysis early: both involve PDFium, and the SDK's internal serialization does not automatically govern Qt PDF. Test concurrent viewing/analysis, document load/unload, cancellation and shutdown with the packaged binaries. Do not declare this integration complete from header compatibility alone.

The baseline SDK has no public standalone page-rendering API. Do not use private S1 headers to build the reader. If embedded viewing is blocked, continue independent catalog/storage work; an external viewer can assist development but does not satisfy in-app page navigation acceptance.

Display the **catalog TOC**, not Qt's default existing-bookmark model. Distinguish “Open chapter” from “Show source TOC page.” Show reasons for uncertainty in understandable language, with technical diagnostics available in details.

Keep QML declarative: no SQL, SDK calls or PDF inference. Use `qsTr()` / `tr()`, keyboard navigation, focus indicators and plain-text rendering of extracted content. Escape text if adding markup for highlighted search matches. Register handwritten files correctly in the CMake/QML module; generated design files remain separate.

## 10. Corrections, export and backup

Saving metadata/TOC corrections updates the catalog and search. **Export PDF** is a separate explicit operation, not an automatic consequence of saving a field.

Retain actual SDK plans alongside full catalog TOCs. For export, select the relevant plan/edit revision, validate it and make any partial/flattening choices explicit. Structural validity does not establish extraction completeness. If the desired conversion from an edited TOC to a plan lacks a supported SDK operation, report that specific gap rather than inventing hidden API behavior.

Never change a plan's source digest or page count to force acceptance. Validate page destinations and hierarchy. Plans may list parents after children. Preserve omissions/promotions and their explanations. Call `apply()` in the worker with a separate destination and output replacement disabled by default; never merge old outlines.

Record committed output accurately. Cancellation received after a successful commit must not cause the application to claim that no file was written. Keep export state separate from the immutable source and the book's active metadata/TOC.

Implement a recoverable backup/restore workflow before release. Quiesce library mutations/jobs, checkpoint and close database connections, then copy a consistent catalog and its referenced source/report files. Copying a live `.sqlite` file alone can miss WAL state. Verify restoration, including corrections and chapter search.

## 11. Serial implementation sequence

Inspect progress first. Implement the earliest unfinished step in the current assignment, verify its gate, and complete the PR/review cycle in section 13 before beginning a dependent step. A large milestone may contain several separately reviewable steps. The initial useful slice is M00–M06; the Windows v1 release also requires M07–M10.

| Step | Main ownership | Completion gate |
| --- | --- | --- |
| **M00** Baseline | Integration | Preserve existing linking; record installed SDK/header/runtime identity; build and run an actual SDK call from the app or a small integration harness |
| **M01** Feasibility | Infrastructure + reader | Actual QSQLITE FTS5 probe; Qt PDF availability/coexistence check; record concrete blockers |
| **M02** Contracts/persistence | A2 + A3 foundation | Shared values, schema/migrations, connection ownership, library lock and transactional search maintenance; synthetic records survive restart |
| **M03** Import/library shell | A1 + A2 | Managed copies, duplicate handling, interrupted-import recovery, unchanged originals and a usable book list |
| **M04** Metadata jobs | A4 + A2 | Durable queue; metadata extraction with models; independent publication, cancellation and restart persistence |
| **M05** Contents | A4 + A2 + presentation | Full parsed TOC and mapping/evidence persist; unresolved/omitted entries remain visible after restart |
| **M06** Search/read | A3 + reader | Chapter-only search finds a book; resolved hit opens the correct page; unresolved hit offers evidence; behavior survives restart |
| **M07** Corrections/reruns | A2–A4 | Auto/Value/Cleared and TOC edits persist; edits during analysis survive; stale results cannot replace current data |
| **M08** Organization | A2 + presentation | Collections without duplicate files; reversible Trash; queue/cancellation/restore races handled |
| **M09** Export | A1 + A4 + presentation | Explicit validated plan produces a new PDF; all managed sources remain protected; partial coverage is visible |
| **M10** Windows release | All | Backup/restore verified; clean packaged runtime includes models, SQL and reader; responsive shutdown/recovery; dependency notices included |
| **M11** Native platforms | Desktop + separately assigned SDK work | Native macOS/Linux packages pass relevant gates; otherwise mark them Unverified |

A blocked external prerequisite does not authorize private SDK work or require abandoning independent app work. Record the blocker, continue what is possible, and do not mark its acceptance gate passed.

## 12. Verification and working discipline

Use small generated PDF fixtures and application-value fixtures. Do not commit personal books or duplicate the existing OCR parity corpus for UI tests. Add focused behavior tests for meaningful risks:

- Original/source hashes stay unchanged; export aliases and another book's managed source cannot be overwritten.
- Crashes around copy installation/catalog publication recover without lost files or duplicate books.
- Metadata remains searchable after TOC failure; unresolved/omitted chapter titles remain searchable.
- Printed labels do not cause incorrect physical navigation; page zero works; parents after children work.
- Overrides and manual clears survive restart/reanalysis; stale jobs and trash races cannot restore old state.
- Search rebuild matches the effective catalog; punctuation/multilingual input works; rollback cannot leave stale index rows.
- Models update on the GUI thread; SQL stays on its owner thread; closing during analysis remains responsive.
- Clean-package startup outside the build tree exercises models, FTS5, embedded viewing and safe export.

Use existing CMake/Qt Test infrastructure where appropriate. Build and run the actual app; distinguish code inspection, earlier CI evidence, locally executed checks and unverified behavior. Do not claim Windows GUI verification from an environment that cannot execute it.

Preserve these repository working rules:

- Put growing C++ code in `src/`, handwritten QML in `qml/`, and documentation in `docs/`.
- Record significant decisions in `docs/DECISIONS.md`: **Change / Why / Assumptions**, plus **Removed / Verified** when relevant.
- Track milestones in `docs/IMPLEMENTATION_PROGRESS.md` with status, branch/PR, touched paths, exact verification commands/results, limitations and next action. Distinguish locally verified, awaiting review and merged work. Keep one active milestone.
- Ask before downloading software, adding an unapproved dependency or changing the Qt kit/version. Existing explicit user authorization remains valid; do not request it repeatedly. SQLite through Qt Sql and Qt PDF are the planned choices, but this file alone does not authorize installations.
- Never edit the installed SDK, vendor its private sources or bypass its safeguards. For an SDK gap, report the missing public capability, installed version and a minimal reproduction to the owner of `PantaKoda/PDFMegine`; pause the dependent part and continue independent authorized work.
- Do not commit build output, installed SDK files, deployed models, personal PDFs or machine-specific configuration.
- Include the actual shipped components' license notices and runtime resources in release packaging. Qt deployment tools alone do not collect every SDK resource.

At each handoff, state what changed and which subsystem owns it, what actually passed, what remains blocked or unverified, and the next concrete milestone. Include the branch, PR URL and tested head commit. Update this file only when an architectural rule, workflow or verified baseline changes; keep routine progress in the progress document.

## 13. GitHub workflow — one implementation step, one feature branch, one PR

The owner requires reviewable changes through GitHub. The normal sequence is **branch → implementation → tests and self-review → commit/push → PR and CI → independent review → fixes → owner-authorized merge**. Do not commit or push implementation changes directly to `main`, and do not silently combine several milestones into one PR.

For assigned implementation work, creating its branch, committing the scoped changes, pushing that branch and opening/updating its PR are part of the requested workflow; do not ask for permission again for each routine action. This does not authorize automatic merging, releases, unrelated changes or repository-settings changes.

### Before implementation

1. Inspect the worktree, remotes and existing PRs. Preserve unrelated edits; do not reset, discard or stash another person's changes without authorization. Use an isolated worktree if needed.
2. Fetch the target repository and create the branch from current `origin/main`. If resuming the same assigned step, continue its existing branch/PR instead of duplicating it. Honor a different base only when explicitly assigned.
3. Name branches by change type, milestone and subsystem, for example `feat/m03-a1-managed-import`, `fix/m06-a3-query-escaping` or `docs/m00-agent-workflow`. Honor any branch prefix required by the execution environment.
4. Define the step's scope and acceptance checks. Split a large milestone into coherent parts before coding; each part must leave the repository buildable. Keep required shared-contract changes in scope and explain their consumers.

### Implement, verify, then publish

5. Implement only the assigned step. Add or update focused behavior tests for its risks, using section 12. For application-code changes, build the affected application configuration; exercise changed UI behavior in a smoke test. Documentation-only changes need relevant document checks, not unrelated OCR runs.
6. Inspect the full diff, including untracked files. Run formatting/whitespace checks, review error and cancellation paths where affected, and verify that no SDK binaries/models, personal PDFs, credentials or machine-specific paths enter the commit. Stage only intended files; do not sweep unrelated work into a commit.
7. Record exact commands and observed results, then commit coherent changes and push the feature branch to the intended repository. Open one PR targeting `main` after local checks pass. If necessary validation can run only in CI, or a known blocker remains, open a **draft PR** with that limitation; do not label it ready or tested prematurely.
8. Run the relevant CI checks and inspect failures. Tie evidence to the current PR head commit, or its corresponding merge-test commit; an earlier green run does not validate later changes. Missing, skipped or cancelled checks are not passing checks. If CI is not configured or lacks an authorized SDK/Qt artifact, report the gap and provide actual local evidence without claiming CI passed. Add/reuse a reproducible build/test workflow when its dependencies are available and authorized.
9. Update progress and the PR description, then hand the owner the PR URL and head SHA for review. An open PR is **AwaitingReview**, not a completed/merged milestone.

Every PR description must contain:

| Field | Required content |
| --- | --- |
| Purpose and scope | Concrete problem, milestone/step, owning subsystem(s), resulting behavior |
| Contracts and data | Changed public application contracts, migrations, compatibility and recovery implications; say when none apply |
| Verification | Exact build/test/smoke commands, results, tested platform/kit/SDK, and CI run links where available |
| UI evidence | Screenshots or a short recording for visible behavior changes, when executable locally |
| Remaining limits | Known blockers, unverified platforms/behavior and follow-up work |

Use a structured PR-body argument or a UTF-8 temporary body file with `gh pr create --body-file` / `gh pr edit --body-file`. Preserve real newlines. Do not put multiline bodies into fragile shell interpolation.

### Review, corrections and merge

The owner intends to have a separate ChatGPT session review these PRs. Include enough context in the repository and PR for that reviewer to work without the implementation agent's private conversation. A PR does not automatically notify or wake that chat session; the owner supplies the PR URL there.

Review covers the diff and relevant surrounding code, subsystem boundaries, PDF/source protection, persistence/recovery, threading/lifetimes, search/navigation correctness and test evidence. Findings should identify concrete behavior, severity, file/line, a reproduction or rationale, and the missing test when relevant. Record the reviewed commit and disclose checks that could not be executed.

Address review findings with additional commits on the **same branch and PR**, rerun affected checks, and request another review by handing back the updated URL/SHA. Explain disagreements with evidence. Do not mark a finding resolved merely to make the PR look complete. Do not force-push shared history or rewrite reviewed commits by default; preserve the review trail.

Do not merge, enable auto-merge, or treat the implementation agent's self-review as independent approval. The owner merges, or explicitly authorizes an agent to merge a named PR after the current revision has passed review and required checks. Any later change requires appropriate revalidation/review. Never bypass repository protections, manufacture approvals or weaken checks to achieve a merge.

GitHub reviews run under the connected account's identity. If that account also authored the PR, it cannot formally approve its own PR; written review findings can still be delivered as comments or in chat. Do not confuse a technical review with satisfying a repository's independent-review rule. See [GitHub's review rules](https://docs.github.com/en/pull-requests/how-tos/review-pull-requests/approving-a-pull-request-with-required-reviews).

Once merged, fetch and fast-forward the local base, record the merged PR in progress, and start the next assigned step from the updated base. Do not build dependent work on an unreviewed branch unless the owner explicitly chooses a stacked-PR workflow. Keep independent preparatory work separate while review is pending; do not use it to evade the review gate.

If branch protection or required-check settings would improve enforcement, propose the concrete settings to the owner; do not change access, protection rules, secrets or automation credentials as an incidental implementation step.
