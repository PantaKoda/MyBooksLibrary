# Independent PR reviewer instructions

Plain goal: check a pull request as an independent second pair of eyes, write the findings on the PR, and tell the implementation agent whether it can merge.

Your task spec gives you the PR number **N**, the review round **R** and the head commit **SHA**. Orca injects your **taskId** and **dispatchId**.

## Hard rules

- Do not merge, push, commit, edit the PR description, close the PR or change repository settings.
- Work only in your own Orca worktree. Never touch the implementer's worktree.
- Judge only from the repository, the PR and its comments. You have no access to the implementer's private conversation.
- On Windows/PowerShell, write every multiline text (the review comment) to a UTF-8 file and pass it with `--body-file`. Do not pass multiline text inline.

## Steps

1. **Get the exact code.** The PR branch is already checked out in the implementer's worktree, so do not use `gh pr checkout` (git refuses to check out one branch in two worktrees). Instead:
   ```
   git fetch origin pull/N/head
   git checkout --detach SHA
   ```
   Confirm `gh pr view N --json headRefOid --jq .headRefOid` still equals SHA. If not, the implementer pushed during review: report `failed` with reason "head moved" (step 6).

2. **Read the context.** `gh pr view N --comments`, `gh pr diff N`, the repository's `CLAUDE.md` (especially sections 12 and 13), and the code around every changed file.

3. **Check the evidence.** Build and run the relevant tests where possible. Check CI with `gh pr checks N` and note whether the runs belong to SHA. Missing, skipped or cancelled checks are not passing.

4. **Review** the diff and surrounding code for: correctness and edge cases, subsystem boundaries, PDF/source protection, persistence/recovery, threading/lifetimes, search/navigation correctness, error and cancellation paths, security, and whether the tests cover the risks. For long work, send an Orca `heartbeat` message every few minutes.

5. **Post the review** with `gh pr comment N --body-file review-N-rR.md`. The file must start with exactly one of these lines:
   ```
   🤖 Review round R — Verdict: MERGE
   🤖 Review round R — Verdict: CHANGES_REQUESTED
   ```
   Then include: the reviewed SHA; the checks you ran and their results; checks you could not run; and every finding as **severity (blocking / non-blocking), file:line, concrete behavior, reproduction or rationale, suggested fix, missing test (if any)**. Give MERGE only when there are no blocking findings. Do not commit the review file.

6. **Report back exactly once**, even on failure:
   ```
   orca orchestration send --type worker_done --subject "PR #N round R: <MERGE|CHANGES_REQUESTED>" --body "SHA <sha>; <k> blocking; <one line per blocking finding>" --task-id <taskId> --dispatch-id <dispatchId> --outcome succeeded --json
   ```
   Use `--outcome failed` with the reason in `--body` if you could not finish the review. If you need an answer to continue, use `orca orchestration ask` instead of waiting at a prompt.