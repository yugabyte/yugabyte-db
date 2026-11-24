# Filing YSQL issues

The goal is a **minimal reproducer** that a YSQL developer can run and
understand quickly. This is a framework, not a form: include what this issue
needs, and write for developer readability.

Lead with the problem and its impact, so a reader knows how bad it is before
reaching the details. For a bug, the impact is often one of these, from most
to least severe:

- a security hole: never filed as a GitHub issue; see "Where to file" in
  [filing-issues.md](filing-issues.md)
- data corruption
- wrong results
- a crash: which process (the postmaster, a postgres backend, a tserver or a
  master), and whether it is a crash loop
- a hang, a deadlock, or the cluster becoming unavailable
- memory or other resource growth
- an error
- a performance regression or a bad plan
- a test failure

Then say where it happens: the statement, feature or component, and the
build. Other issues, such as an enhancement, a refactor, new tests or
documentation, lead with what they ask for and why.

Where to file, how to handle duplicates and what to do with internal links
are the same for every core DB component; see
[filing-issues.md](filing-issues.md). When a YSQL issue turns out to
be a duplicate with a materially different repro, the comment on the
existing issue covers everything under "Always cover" below.

## Reduce first

Before drafting, try to reduce the issue to its minimal form: the fewest
objects, rows, statements, sessions and non-default settings that still show
the problem. Check that it still shows the problem after each cut.

If you can't reduce it within reason, file it anyway and say so ("not yet
minimized"). A bloated issue is better than an unfiled one.

## Always cover

- **Where it was found.** Give the branch plus a commit, version or build
  number. Commits must already be on a remote, written bare so GitHub
  auto-links them. If the build type matters, name it: release, debug, ASAN,
  TSAN, and so on. List every branch or version you actually tested, with its
  result; if you tested only one, say other versions are untested. If you
  know it used to work, give the last version or commit where it did.
- **What kind of issue it is.** An issue gets exactly one `kind/*` label;
  pick the one that fits best (correctness bugs and crashes are `kind/bug`),
  defaulting to `kind/bug` if nothing else applies. Use only existing labels.
  The categories below can still stack in the body, for example a correctness
  bug that only appears under concurrency.
- **Environment, only what's needed to reproduce it.** List the non-default
  GUCs, gflags, cluster shape (nodes, RF, colocation) and connection manager
  settings that the repro depends on, and the client when it matters: the
  driver, ORM or tool and its version (for example JDBC, psycopg or
  `ysqlsh`). Omit everything that doesn't change the outcome.
- **Repro.** SQL or steps that run as written against a fresh cluster, using
  generic names (`t1`, `id`, `v`). Put the schema setup (DDL and seed data) in
  its own code block, separate from the statements that trigger the issue.
  Optionally show `\d` (or equivalent) output for the objects involved.
- **Actual vs expected output.** Show both. For `EXPLAIN` output, leave out
  values that change from run to run unless they are the point, for example
  with `COSTS OFF, TIMING OFF, SUMMARY OFF`.
- **How often.** If it doesn't happen every time, give the rate and the
  sample: "failed 2 of 20 runs", "passed all 194 fastdebug runs on master
  from 2026-09-02 to 2026-10-28".
- **How each claim is known.** Mark anything not shown by the repro output:
  observed, measured, inferred from logs, or from reading the code ("inferred
  from the logs, not reproduced").
- **Related issues.** Link any you know of or found in the duplicate check,
  with one line on how each relates. Don't search beyond that.
- **Logs.** Paste short, relevant excerpts inline in a code block. Put long
  ones in a collapsed `<details>` block, or link to their source. Only logs
  from your own reproduction belong on GitHub; customer logs are sensitive.

## Per category

**Correctness.** Ground the expected result in a reference, never in
intuition. Name the reference you compared against:
- vanilla PostgreSQL, with its version
- colocated vs non-colocated
- the flag or GUC on vs off
- hash vs range sharding
- another YB version

**Crash.** Include the symbolized stack trace, and the log lines leading up to
it when they're relevant. Name the crashing process: the postmaster, a
postgres backend, a tserver or a master. Say whether it is an assertion in a
debug build or a crash in a release build.

**Concurrency.** If the interleaving is known, show it as a step table: one
column per session, one numbered row per step, with the isolation level in
each column header. Mark blocking with ⋯ and resumption with ↳ (plain
Unicode, not emoji), and put the expected result next to the actual one.

| # | S1 (READ COMMITTED) | S2 (READ COMMITTED) |
|---|---|---|
| 1 | `BEGIN;` | |
| 2 | `UPDATE t1 SET v = 1 WHERE id = 1;` | |
| 3 | | `BEGIN;` |
| 4 | | `UPDATE t1 SET v = v + 1 WHERE id = 1;` ⋯ blocks |
| 5 | `COMMIT;` | |
| 6 | | ↳ `UPDATE 0` (expected `UPDATE 1`) |

If the interleaving can be scripted, also attach an isolation-tester spec
(the format of `src/postgres/src/test/isolation/specs/*.spec`) in a collapsed
`<details>` block. It reproduces deterministically and can become the
regression test. If a step only reproduces with a forced delay (a gflag,
sleep or debugger breakpoint), say which step and how.

State any locking-related flags.

**Failing test.** Include:
- the command that runs the test (for example a `./yb_build.sh` invocation with
  the build type)
- a snippet of the error
- the full logs and the test's stability history, as public links or
  summarized (see "Links" in [filing-issues.md](filing-issues.md))

**Bug (other).** Explain briefly why the behavior is wrong: which docs,
upstream PostgreSQL behavior or invariant it contradicts.

**Feature request, enhancement, refactor, new tests or documentation.** State
the motivation and the desired outcome. The repro sections don't apply.

## Validating a SQL repro

Run it before filing, in whatever environment makes sense for the issue.
Paste the real output. If it doesn't reproduce, stop (agents: ask the user).
