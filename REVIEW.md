# YugabyteDB Code Review Style Guide

This document is the review guide for anyone — human or AI agent — reviewing PRs in this repository. It scopes what to flag, what to skip, and how to handle a few PR types (especially backports) where the default review behavior produces noise.

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

## General review guidance

Focus on **substantive issues**: correctness, **memory safety**, security, concurrency, performance, and breaking-change risk. The repo runs `./build-support/lint.sh` on every PR — its checks are authoritative for style. Do not duplicate them.

Specifically, **do not** flag:

- Line length, indentation, trailing whitespace, missing trailing newlines, tab/space mixing.
- Import ordering, header-include ordering (the linter and clang-format own this).
- Naming-style nits where the existing module already follows a pattern (a new symbol that matches its surroundings is fine). This is about spelling and casing; a new *term* for a concept the codebase already names is in scope — see [Reuse and terminology](#reuse-and-terminology).
- Missing tests for hunks where an existing test already exercises the changed behavior — flag only when a code path has *no* coverage at all.

**Do** flag:

- Logic errors, off-by-ones, missing error handling on fallible operations, data races.
- **Memory-safety issues**: use-after-free, double-free, leaked allocations / file descriptors / locks, dangling references / iterators, returning pointers to local stack data, unchecked nullability on a pointer that could be null, buffer overruns, smart-pointer / `unique_ptr` ownership mistakes.
- Use of unsafe APIs, SQL/format-string injection vectors, missing input validation at trust boundaries.
- Public API or wire-format changes (`*.proto`, public headers under `src/yb/yql/`, gflags users will see, gRPC service definitions). These need extra scrutiny — call them out even if they look correct.
- Performance regressions on hot paths (DocDB write/read path, query layer plan execution, RPC dispatch).
- **Upgrade/rollback safety**: YugabyteDB supports **online upgrade and rollback across major, minor, and major-postgres versions** — every change must preserve that. Any on-disk or wire-format change (proto fields, catalog schema, RPC versioning, persistent file layout) **must land behind an `AutoFlag` or a `gFlag` that's turned off during development** so the new behavior can't reach a mixed-version cluster until the flag is promoted. Flag any commit that changes one of these surfaces without gating it: `*.proto` field add/remove/renumber, on-disk format bumps, catalog migrations, gflag default flips that alter observable behavior, RPC service signatures, persisted enum reordering. **When and only when the PR diff actually contains one of those upgrade-relevant changes,** the PR description must include a `## Upgrade/Rollback safety` section explaining forward/backward behavior on a mixed-version cluster and what rollback looks like — flag the absence of that section in that case. [Wire and storage compatibility](#wire-and-storage-compatibility) below says what to check in the change itself. **Do not flag the missing section on PRs that don't touch any of those surfaces** (e.g. docs-only changes, comment edits, test-only refactors, agent-instruction updates, README tweaks, build-script changes that don't alter runtime behavior). An "N/A" placeholder is not required for those PRs.
- Anything that contradicts a comment, docstring, or commit message in the same diff.
- Non-ASCII characters in new source code or comments, unless the file is test data that needs them.
- Under `src/yb/`, departures from the conventions in [`src/yb/REVIEW.md`](src/yb/REVIEW.md).
- Under `src/postgres/`, what [`src/postgres/REVIEW.md`](src/postgres/REVIEW.md) asks to flag.

Default comment severity threshold: **MEDIUM**. Suppress LOW-severity nits unless they reveal an underlying correctness issue.

**When you have no actionable feedback, say so in one line.** Do not write a `## Code Review` summary paragraph that paraphrases the diff and then ends with "I have no feedback to provide." — that's review noise the author has to read and dismiss. Just leave a single comment of the form `LGTM` (optionally with a one-line caveat, e.g. `LGTM — please verify the test plan covers <X>`). Skip the diff recap entirely; the author already knows what they wrote.

**Do not repeatedly re-flag the same issue across review rounds.** A re-review after a follow-up commit must respect the prior round's resolution:

- If a thread on the same line / same issue was **resolved** (via the GitHub UI or `resolveReviewThread`), do not raise it again. The resolve carries the human/agent decision and re-flagging adds review noise.
- If a thread has a **reply** explaining why the suggestion was declined (e.g. "the script already has `set -euo pipefail` at line 17") or applied differently from the literal suggestion, accept the explanation and skip the issue on the next pass.
- If your prior comment matches a hunk that is **byte-identical** to the version you reviewed before, skip it. The fact that the diff is unchanged means the author is keeping that hunk; re-flagging won't change anything.
- New issues on lines you didn't review before are fine to raise.

## Wire and storage compatibility

A mixed-version cluster is the normal state during an upgrade, and rollback must stay possible until the upgrade is finalized. Review every change to what one process sends another, or writes to disk, against four questions: can the old binary read what the new one writes, can the new binary read what the old one wrote, can an old sender talk to a new receiver and the reverse, and what happens on rollback to data the new binary has already written. Report a change that fails any of them at **HIGH**; it blocks a release. The `## Upgrade/Rollback safety` section of the PR must answer all four; an answer that covers the upgrade but not the rollback is incomplete.

### Protobuf messages

Protobuf is the wire format between every yb-master, yb-tserver, and client, and most persisted metadata is a serialized message too, so one set of rules covers both.

- A field's number and type never change, and the number of a removed field is never reused: the removed field's number and name go under `reserved` (see `src/yb/tablet/operations.proto`). A rename keeps the wire format but breaks anything that uses the field by name: text-format dumps, JSON output, tools. Check for those consumers.
- No new `required` field, and no `optional` field made `required`.
- Adding a field is safe only when an old receiver may ignore it. When the new sender relies on the receiver honoring the field, the sender must not set it until an AutoFlag is promoted.
- Adding an enum value is a format change: an old binary reads an unknown value as unset, and a `switch` ending in `FATAL_INVALID_ENUM_VALUE` or `FATAL_INVALID_PB_ENUM_VALUE` crashes on it. A new value is sent or persisted only behind a flag. Existing values are never renumbered or reordered.
- Moving fields into or out of a `oneof`, changing a field's message type, or changing the meaning of a field's default are breaking changes even though the file still compiles.
- A new RPC method or service is safe only when the caller handles `ERROR_NO_SUCH_METHOD` from an old server or is gated. Changing a method's request or response type, or its semantics, is a format change.

### Persisted data and metadata

- The surfaces: DocDB key and value encodings (`src/yb/dockv/value_type.h`), packed rows (`src/yb/dockv/packed_row.h`), the WAL (`src/yb/consensus/log.proto`), consensus and tablet metadata (`src/yb/consensus/metadata.proto`, `src/yb/tablet/metadata.proto`), the master's sys catalog entries (`src/yb/master/catalog_entity_info.proto`), snapshots and backups, xCluster and CDC streams, and the YSQL catalog.
- Reads accept every format ever written and are never gated. Writes of a new format are gated by an AutoFlag whose class matches how far the data travels, per `architecture/design/auto_flags.md`: `kLocalVolatile` for data that only crosses the wire inside the universe, `kLocalPersisted` for anything written to disk, `kExternal` for anything that leaves the universe (xCluster, backups, CDC). Flag a persisted or exported format behind `kLocalVolatile`.
- A `kLocalPersisted` or `kExternal` flag cannot be demoted, so promotion is the point of no return for rollback. Until then the old binary must read everything the new one wrote, and the new binary must not rewrite or delete anything the old one needs. The PR must say where that point is.
- A new key or value entry type takes an unused byte and the meaning of an existing byte never changes. Key entry types sort, so a new one must sort where the encoding needs it.
- A format version that is bumped is still read at every older version and written at the new version only behind the flag.
- YSQL catalog changes (system tables, views, functions) go through a migration in `src/yb/yql/pgwrapper/ysql_migrations/` following its README: idempotent, modifying objects in place with fixed OIDs, named `V<n>__<issue>__<description>.sql`, with `pg_yb_migration.dat` updated and coverage in `TestYsqlUpgrade`. A change only in initdb leaves existing clusters without it. The migrated catalog must stay usable by the old binary until finalization.
- A format change needs a test that reads the old format, either a checked-in fixture or an upgrade test under `src/yb/integration-tests/upgrade-tests/`. A round trip through the new code alone proves nothing about compatibility.

When reporting, cite the surface, the flag that gates the write, its class, and which of the four questions the change fails.

## Reuse and terminology

These two checks need searching the repo, not just reading the diff. Do the searches; a finding here without a citation of the existing code or term (`path:line`, or a commit SHA for a term found only in history) is not a finding. Report each at **MEDIUM**, so the LOW threshold above does not suppress them. Raise to HIGH when the duplicate already behaves differently from the code it copies, since that is a correctness bug. Skip both checks for backport PRs and for test-only changes.

### Reuse

Does the added code duplicate an existing function, helper, constant, or pattern?

1. **What to check:** every new helper, non-trivial expression, magic number, and hand-built object (iterators, `ReadOptions`, key encoding).
2. **Where to search:** the same file first, then its directory, then the rest of `src/yb/`.
3. **How to search:** by what the code does, not by how the diff spells it, because an equivalent rarely matches the text. Search for likely function and constant names, the types and APIs the code calls, and the key bytes or values it uses. For example, `a > b ? a - b : b - a` and `std::max(a, b) - std::min(a, b)` compute the same thing, and `kMaxPartitionKey` equals `std::numeric_limits<uint16_t>::max()`.
4. **Before reporting:** confirm the existing code has the same semantics (edge cases, error handling, ownership, locking). If the new code is evidently meant to do the same thing but differs on an edge case, that is a drifted copy: report it at HIGH rather than dropping it. Model it on production code, not test code (`TEST_*`, `*-test.cc`). If reuse would cost more than it saves, for example it pulls in a heavy header or crosses a layering boundary, suggest the refactor that would make it cheap (such as moving the existing code to a lower-level header or library), and say whether it belongs in this PR or in a follow-up change. Report a follow-up refactor at LOW; the LOW threshold above does not apply to it.
5. **What to report:** cite the existing code as `path:line` and show the replacement. When the same logic appears twice or more, including twice within the diff, suggest extracting a shared helper and name the file where it should live.
6. **What not to report:** a choice between facilities that coexist because they differ on purpose, such as `scoped_refptr` vs `std::shared_ptr` (intrusive vs separate reference count), `MonoTime` vs `CoarseMonoClock` (precise vs cheap clock), or `std::mutex` vs `simple_spinlock` (blocking vs spinning). Do not flag one because the other is more common. Flag the choice only when that difference matters for this code, for example a spinlock held across blocking I/O, and say why. The legacy forms listed under "Preferred utilities" in `docs/content/stable/contribute/core-database/coding-style.md` are different: each has a replacement that does the same job, so do flag them in new code.

### Terminology

Do new names and terms match the vocabulary the codebase already uses for the same concept?

1. **What to check:** new identifiers, gflag names, log and error messages, and comments that name a concept.
2. **Where to search:** beyond the changed file, in the components it interacts with: master and tserver code for the same feature, the RocksDB layer under DocDB, the YSQL/YCQL layer above it, and the `*.proto` definitions both sides share. Also search commit history (`git log -S<term>`, `git log --grep=<term>`).
3. **What to report:** a new term for something that already has a name. Cite where the existing term is used (`path:line` or commit SHA) and give the rename. When the codebase already uses more than one term for the concept, do not flag a choice between them unless one clearly dominates and the other is legacy; flag only a term that is new to all of them.

## Areas that warrant deeper review

Regardless of PR type, give these areas extra attention:

- **`src/yb/master/`** (catalog manager, leader election) — concurrency and state-machine correctness. See [Catalog manager changes](#catalog-manager-changes).
- **`src/yb/tablet/`** and **`src/yb/docdb/`** — write/read path, MVCC, transaction handling.
- **`src/yb/cdc/`** and **`src/yb/cdc_consumer/`** — replication-stream correctness, idempotency.
- **`src/postgres/`** — PostgreSQL-fork merges; double-check that upstream Postgres semantics are preserved. Also check the diff against the fork conventions in `.claude/skills/pg-code-style-guide/SKILL.md`: YB additions yb-prefixed and clustered behind `/* YB ... */` marker comments, upstream-owned lines left byte-identical (no style fixes or rewrapping), and regress test changes respecting the upstream-named / `yb.port.*` / `yb.orig.*` taxonomy. Flag violations — they surface as merge conflicts on the next PG major-version merge.
- **`managed/`** (YBA platform) — orchestration logic, especially around node lifecycle and cluster-wide ops.

## Catalog manager changes

For changes to catalog objects (namespaces, tables, tablets, and other COW objects) in `src/yb/master/`, check:

1. **Lock order.** Every acquisition follows the [lock order](#catalog-manager-lock-order) below. Flag an inversion at HIGH.
2. **Failover between persisted steps.** An operation that persists several states to the sys catalog can stop after any of them when the master leader fails. For each persisted intermediate state, check that the new leader resumes the operation to completion or rolls it back, and that it does not leave an object stuck in that state. Flag a state that no code path on the new leader picks up at HIGH.
3. **Deadlocks with other operations.** List the other operations that can run on the same objects at the same time, such as tserver heartbeats, the load balancer, tablet splitting, background tasks, DDL rollback, backfill, xCluster/CDC, and snapshot restore. Check each one for a lock cycle with the change, including cycles through waits that are not locks, such as a lock held while waiting for an RPC callback that needs the same lock. Flag a possible deadlock at HIGH.
4. **Hold time of `CatalogManager::mutex_`.** `CatalogManager::mutex_` must be held as briefly as possible and never across an RPC, a sys catalog write, or other IO. It is a spinlock and most master leader RPCs take it, so a thread that blocks while holding it stalls those RPCs and burns CPU in the waiting threads (#23040). Long holds of COW locks can also stall other operations, but `mutex_` is far more sensitive. The leader shared lock is held for the whole duration of many RPCs and does not have this restriction. Flag at HIGH, unless it is an already existing issue, then it is MEDIUM.

### Catalog manager lock order

Code in `src/yb/master/` must acquire locks in this order. A thread that holds a lock may only acquire locks that come later in the list.

1. `ScopedLeaderSharedLock::leader_shared_lock_`, usually taken with the `SCOPED_LEADER_SHARED_LOCK` macro.
2. `CatalogManager::mutex_`.
3. Namespace COW locks (`NamespaceInfo`), sorted by `namespace_id`.
4. Table COW locks (`TableInfo`), sorted by `table_id`.
5. Tablet COW locks (`TabletInfo`), sorted by `tablet_id`.
6. `CatalogManager::state_lock_` (this position is less certain).

COW lock means the read/write/commit lock of a copy-on-write catalog object (`src/yb/util/rwc_lock.h`). Several objects of the same kind must be locked in id order; an unsorted loop over tablets or tables that takes write locks is a deadlock risk.

Commit COW write locks in the reverse order of acquisition: a lock acquired later commits earlier. This applies to all COW objects: tablets commit before their table, and of two tables the one with the larger `table_id` commits first. A commit on object A waits until every reader of A releases its read lock. If the committing thread still holds a write lock on an object B that comes after A, a reader that holds A and then tries to lock B (a correct acquisition order) waits for the writer, while the writer waits for that reader: deadlock. Heartbeat processing is one such reader: it holds a table read lock while it takes tablet write locks (#10304, #32090). Debug builds assert the table/tablet case through the held-tablet-write-lock count in `src/yb/master/catalog_entity_info.h`. Code paths exempt from that assert are marked with `TableCommitLockAssertSuppression` (`src/yb/util/cow_object.h`).

Inversions to flag at HIGH:

- A COW lock taken before `CatalogManager::mutex_` on the same thread, or a `SCOPED_LEADER_SHARED_LOCK` taken while `mutex_` or a COW lock is held.
- Several objects of one kind locked without sorting by id.
- A COW write lock committed while the same thread still holds a write lock acquired after it, for example a table committed before its tablets.

## What to ignore entirely

- Narrative prose under `architecture/` and `docs/` — content review is welcome but skip code-style suggestions for the prose. Code samples embedded in docs should be reviewed.
- Generated files (anything under a `gen/` directory or matching `*.pb.{cc,h}`).
