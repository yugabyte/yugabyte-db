# YugabyteDB Code Review Style Guide

This document configures Gemini Code Assist's automated review for PRs in this repository. It scopes what to flag, what to skip, and how to handle a few PR types (especially backports) where the default review behavior produces noise.

## General review guidance

Focus on **substantive issues**: correctness, **memory safety**, security, concurrency, performance, and breaking-change risk. The repo runs `./build-support/lint.sh` on every PR — its checks are authoritative for style. Do not duplicate them.

Specifically, **do not** flag:

- Line length, indentation, trailing whitespace, missing trailing newlines, tab/space mixing.
- Import ordering, header-include ordering (the linter and clang-format own this).
- Naming-style nits where the existing module already follows a pattern (a new symbol that matches its surroundings is fine).
- Missing tests for hunks where an existing test already exercises the changed behavior — flag only when a code path has *no* coverage at all.

**Do** flag:

- Logic errors, off-by-ones, missing error handling on fallible operations, data races.
- **Memory-safety issues**: use-after-free, double-free, leaked allocations / file descriptors / locks, dangling references / iterators, returning pointers to local stack data, unchecked nullability on a pointer that could be null, buffer overruns, smart-pointer / `unique_ptr` ownership mistakes.
- Use of unsafe APIs, SQL/format-string injection vectors, missing input validation at trust boundaries.
- Public API or wire-format changes (`*.proto`, public headers under `src/yb/yql/`, gflags users will see, gRPC service definitions). These need extra scrutiny — call them out even if they look correct.
- Performance regressions on hot paths (DocDB write/read path, query layer plan execution, RPC dispatch).
- **Upgrade/rollback safety**: any change to `*.proto`, on-disk format, catalog schema, gflag default that changes observable behavior, or RPC versioning. The PR description should have a `## Upgrade/Rollback safety` section explaining forward/backward behavior on a mixed-version cluster and rollback. Flag the absence of that section, not just the change.
- Anything that contradicts a comment, docstring, or commit message in the same diff.

Default comment severity threshold: **MEDIUM**. Suppress LOW-severity nits unless they reveal an underlying correctness issue.

**When you have no actionable feedback, say so in one line.** Do not write a `## Code Review` summary paragraph that paraphrases the diff and then ends with "I have no feedback to provide." — that's review noise the author has to read and dismiss. Just leave a single comment of the form `LGTM` (optionally with a one-line caveat, e.g. `LGTM — please verify the test plan covers <X>`). Skip the diff recap entirely; the author already knows what they wrote.

**Do not repeatedly re-flag the same issue across review rounds.** A `/gemini review` retrigger after a follow-up commit must respect the prior round's resolution:

- If a thread on the same line / same issue was **resolved** (via the GitHub UI or `resolveReviewThread`), do not raise it again. The resolve carries the human/agent decision and re-flagging adds review noise.
- If a thread has a **reply** explaining why the suggestion was declined (e.g. "the script already has `set -euo pipefail` at line 17") or applied differently from the literal suggestion, accept the explanation and skip the issue on the next pass.
- If your prior comment matches a hunk that is **byte-identical** to the version you reviewed before, skip it. The fact that the diff is unchanged means the author is keeping that hunk; re-flagging won't change anything.
- New issues on lines you didn't review before are fine to raise.

## Backport PRs (different review rules)

A **backport** is a PR whose title starts with `[BACKPORT <release-branch>]`, e.g. `[BACKPORT 2024.2][#31358] YSQL: Add yb_enable_mage gFlag`. Its body always contains a footer line of the form:

```
Original commit: <SHA> / #<PR>
```

placed immediately above the `## Test plan` section (or appended to the end if there is no test plan). The `<SHA>` is the full originating commit on `master` (followed by ` / #<PR>` referencing the original PR number) that already passed full review when its original PR merged. The cherry-pick onto the release branch may have been clean or may have required conflict resolution.

When you review a backport PR, **switch to backport mode**:

1. **Locate the originating commit** at the SHA from the `Original commit:` line. Compare its diff (`git show <SHA>`) against the backport PR's diff.
2. **The expected outcome is byte-identical except for documented conflict resolutions.** Hunks differ only when the release branch's surrounding code differs from `master` and the cherry-pick had to adapt.
3. **Documented resolutions live under a `## Merge conflicts` section in the PR body**, formatted as `<path>:<line> — <one-line description>` entries. If the section is absent, the backport is a clean cherry-pick — every hunk should match the original.
4. **Do not flag any hunk that is byte-identical to the originating commit.** That code already passed review at merge time. Re-flagging it adds review noise, slows the backport, and risks blocking a release-branch fix on issues that were already accepted on `master`.
5. **Flag only**:
   - Hunks that diverge from the originating commit AND are *not* documented in the `## Merge conflicts` section. Either the backport author silently changed the behavior, or the conflicts section is incomplete.
   - Documented conflict resolutions that appear logically incorrect (e.g., the release branch lost a parameter and the backport elided a check that the original had).
   - Mismatched commit metadata (subject doesn't reference the right issue, or the `Original commit:` line is missing/wrong).
6. **If the backport diff is byte-identical to the originating commit and there is no `## Merge conflicts` section, leave a single approval-style comment confirming the clean cherry-pick.** No further review is necessary — the repo's CI will validate that the build still passes on the release branch.

This mode exists because backports re-traverse code that was already reviewed; treating them as fresh PRs creates duplicate review work for changes that were already accepted upstream.

## Areas that warrant deeper review

Regardless of PR type, give these areas extra attention:

- **`src/yb/master/`** (catalog manager, leader election) — concurrency and state-machine correctness.
- **`src/yb/tablet/`** and **`src/yb/docdb/`** — write/read path, MVCC, transaction handling.
- **`src/yb/cdc/`** and **`src/yb/cdc_consumer/`** — replication-stream correctness, idempotency.
- **`src/postgres/`** — PostgreSQL-fork merges; double-check that upstream Postgres semantics are preserved.
- **`managed/`** (YBA platform) — orchestration logic, especially around node lifecycle and cluster-wide ops.

## What to ignore entirely

- Markdown / config files under `.claude/`, `.gemini/`, plus `AGENTS.md`, `src/AGENTS.md`, and `CLAUDE.md` — these are agent / review-bot configuration. Don't review them as code; their effects are operational (they change how *agents* and *bots* behave, not what the database does). **Shell / Python scripts in those folders (e.g. `.claude/commands/*.sh`, anything else with executable bits) are real code and should be reviewed normally** — apply the regular focus areas above (correctness, memory safety, security, etc.).
- Narrative prose under `architecture/` and `docs/` — content review is welcome but skip code-style suggestions for the prose. Code samples embedded in docs should be reviewed.
- Generated files (anything under a `gen/` directory or matching `*.pb.{cc,h}`).
