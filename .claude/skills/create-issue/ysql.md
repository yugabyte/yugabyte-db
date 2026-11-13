# YSQL issues

Read this after Step 2 of `SKILL.md` puts the issue in YSQL. It shapes the
`Description` of `ysql.yml`. The routing, confidentiality, labels and
confirmation steps in `SKILL.md` still apply.

The goal is a **minimal reproducer** that a YSQL developer can run and
understand quickly. This is a framework, not a form: include what this issue
needs. Write for developer readability: open with the headline, what's wrong
and where (the statement, feature or component, and the build), so a reader
knows the problem before reaching the details.

## Reduce first

Before drafting, try to reduce the issue to its minimal form: the fewest
objects, rows, statements, sessions and non-default settings that still show
the problem. Check that it still shows the problem after each cut.

If you can't reduce it within reason, file it anyway and say so ("not yet
minimized"). A bloated issue is better than an unfiled one.

## Check for duplicates

Before drafting, search existing issues (open and closed) for the same
symptom.

- **Same problem, same repro:** don't file. Report the existing issue to the
  user.
- **Same problem, materially different repro** (a different query shape,
  setting, environment or trigger): don't file a new issue. Instead, draft a
  comment for the existing one that covers everything below: where it was
  found, the environment needed, the repro, actual vs expected output, how
  often, and logs. Confirm the comment with the user before posting it.
- **Not a duplicate:** file a new issue. Link any related issues the search
  turned up.

## Always cover

- **Where it was found.** Give the branch plus a commit, version or build
  number. Commits must already be on a remote, written bare so GitHub
  auto-links them. If the build type matters, name it: release, debug, ASAN,
  TSAN, and so on. List every branch or version you actually tested, with its
  result; if you tested only one, say other versions are untested.
- **What kind of issue it is.** An issue gets exactly one `kind/*` label;
  pick the one that fits best (correctness bugs and crashes are `kind/bug`),
  defaulting to `kind/bug` if nothing else applies. Use only existing labels.
  The categories below can still stack in the body, for example a correctness
  bug that only appears under concurrency.
- **Environment, only what's needed to reproduce it.** List the non-default
  GUCs, gflags, cluster shape (nodes, RF, colocation) and connection manager
  settings that the repro depends on. Omit everything that doesn't change the
  outcome.
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

**Crash.** Include the symbolized stack trace and the log lines leading up to
it. Name the crashing process (postgres backend, tserver or master).

**Concurrency.** If the interleaving is known, show it as a step table: one
column per session, one numbered row per step, with the isolation level in
each column header. Mark blocking with ⋯ and resumption with ↳ (plain
Unicode, not emoji), and put the expected result next to the actual one.

| # | S1 (READ COMMITTED) | S2 (READ COMMITTED) |
|---|---|---|
| 1 | `BEGIN;` | |
| 2 | `UPDATE t1 SET v = 1 WHERE id = 1;` | |
| 3 | | `BEGIN;` |
| 4 | | `SELECT v FROM t1 WHERE id = 1;` ⋯ blocks |
| 5 | `COMMIT;` | |
| 6 | | ↳ returns `0` (expected `1`) |

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
- a link to the full logs and to the test's stability history, if public (see
  Links)

**Bug (other).** Explain briefly why the behavior is wrong: which docs,
upstream PostgreSQL behavior or invariant it contradicts.

**Feature request / enhancement.** State the motivation and the desired
behavior. The repro sections don't apply.

## Validating a SQL repro

Run it before filing, in whatever environment makes sense for the issue.
Paste the real output. If it doesn't reproduce,
stop and ask the user.

## Links

Internal links (Jenkins, test-stability dashboards, internal JIRA or Slack,
support tickets) don't belong in a public GitHub issue. Link only to public
sources.
