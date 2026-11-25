# Review conventions for src/yb

These apply to C++ changes under `src/yb/` in addition to the repo-root [REVIEW.md](../../REVIEW.md). `lint.sh` enforces none of them. Report each at **MEDIUM** when a PR adds or edits the construct; do not flag pre-existing code the PR merely touches. Skip this file for backport PRs.

Flag new code that departs from the C++ rules in the [coding style guide](../../docs/content/stable/contribute/core-database/coding-style.md), including the legacy forms in its "Preferred utilities" table, and cite the section.

## Errors and logging

`CHECK` or `assert` on a fallible path is a crash: use `RETURN_NOT_OK`, `VERIFY_RESULT`, `RSTATUS_DCHECK`, or `SCHECK` (coding style: "Error checking macros"). An impossible state that is not worth crashing release builds for is `LOG(DFATAL)`. A `Status` that is computed and dropped, or an error path that logs and continues where it should return, is a finding.

## Tests

- No fixed sleeps as synchronization: `WaitFor` on the condition, or a sync point.
- `ASSERT_*` over `EXPECT_*` when the rest of the test depends on the check.
- `ASSERT_OK` and `EXPECT_OK` are do/while macros and accept no `<<` streaming; a suggestion to add one is itself a bug.
- Prefer `MiniCluster` to `ExternalMiniCluster` unless the test needs separate processes.
- Test-only flags and functions carry the `TEST_` prefix, and a delay flag treats 0 as "off".
- Do not ask a test to reset gflags at its end. `YBTest` holds a `google::FlagSaver` (`src/yb/util/test_util.h`) that restores all flags after each test, so a flag set in one test does not leak into the next. A reset in the middle of a test is needed only when a later step of the same test depends on the old value; existing mid-test resets are not a pattern for end-of-test cleanup.

## Concurrency

- Mutex-guarded members carry `GUARDED_BY`; a new `NO_THREAD_SAFETY_ANALYSIS` needs a stated reason.
- Callbacks, blocking work, and allocation should not run while a lock is held.
- Ask who guarantees an object outlives an async continuation that captures it.
