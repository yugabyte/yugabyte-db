use pgrx::pg_sys::panic::CaughtError;
use pgrx::prelude::*;
use proptest::strategy::Strategy;
use proptest::test_runner::{TestCaseError, TestCaseResult, TestError, TestRunner};
use std::panic::AssertUnwindSafe;

/** proptest's [`TestRunner`] adapted for `#[pg_test]`

# Experimental
This is an exploratory implementation in which many subtle details may be quickly changed.
Prefer to rely only on the core API documented here while using this, instead of e.g. `PROPTEST_` environment variables.

# When To Use
In order to manage executing code inside `#[pg_test]` that may throw an exception, a runner
that knows how to try-catch is required. The way pgrx interacts with proptest's TestRunner
means proptest accepts exceptions as test failures, but prevents them from resolving.
This exposes Postgres to a risk of stack overflow. `PgTestRunner::run` solves that.

For exposed functions, `PgTestRunner` transparently replaces `TestRunner`:
- Inside closures executed by this runner, macros like [`prop_assert!`] work normally.

This does not provide compatibility with the entire proptest API surface:
- `proptest!` can't use `PgTestRunner` as it builds its own runners
- Using this with proptest's forking support is likely profoundly broken

[`prop_assert!`]: proptest::prop_assert
*/
#[derive(Default)]
pub struct PgTestRunner(TestRunner);

impl PgTestRunner {
    pub fn run<S: Strategy>(
        &mut self,
        strategy: &S,
        test: impl Fn(S::Value) -> TestCaseResult,
    ) -> Result<(), TestError<<S as Strategy>::Value>> {
        self.0.run(strategy, |value| {
            PgTryBuilder::new(AssertUnwindSafe(|| test(value)))
                .catch_others(|err| match err {
                    CaughtError::PostgresError(err)
                    | CaughtError::ErrorReport(err)
                    | CaughtError::RustPanic { ereport: err, .. } => {
                        Err(TestCaseError::Fail(err.message().to_owned().into()))
                    }
                })
                .execute()
        })
    }
}
