//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::errcodes::PgSqlErrorCode;
use crate::panic::{downcast_panic_payload, CaughtError};
use std::collections::BTreeMap;
use std::panic::{catch_unwind, resume_unwind, AssertUnwindSafe, RefUnwindSafe, UnwindSafe};

/// [`PgTryBuilder`] is a mechanism to mimic Postgres C macros `PG_TRY` and `PG_CATCH`.
///
/// A primary difference is that the [`PgTryBuilder::finally()`] block runs even if a catch handler
/// rethrows (or throws a new) error.
pub struct PgTryBuilder<'a, R, F: FnOnce() -> R + UnwindSafe> {
    func: F,
    when: BTreeMap<
        PgSqlErrorCode,
        Box<dyn FnMut(CaughtError) -> R + 'a + UnwindSafe + RefUnwindSafe>,
    >,
    others: Option<Box<dyn FnMut(CaughtError) -> R + 'a + UnwindSafe + RefUnwindSafe>>,
    rust: Option<Box<dyn FnMut(CaughtError) -> R + 'a + UnwindSafe + RefUnwindSafe>>,
    finally: Option<Box<dyn FnMut() + 'a>>,
}

impl<'a, R, F: FnOnce() -> R + UnwindSafe> PgTryBuilder<'a, R, F> {
    /// Create a new `[PgTryBuilder]`.  The `func` argument specifies the closure that is to run.
    ///
    /// If it fails with either a Rust panic or a Postgres error, a registered catch handler
    /// for that specific error is run.  Whether one exists or not, the finally block also runs
    /// at the end, for any necessary cleanup.
    ///
    /// ## Example
    ///
    /// ```rust,no_run
    /// # use pgrx_pg_sys::{ereport, PgTryBuilder};
    /// # use pgrx_pg_sys::errcodes::PgSqlErrorCode;
    ///
    /// let i = 41;
    /// let mut finished = false;
    /// let result = PgTryBuilder::new(|| {
    ///     if i < 42 {
    ///         ereport!(ERROR, PgSqlErrorCode::ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE, "number too small");
    ///     }
    ///     i
    /// })
    ///     .catch_when(PgSqlErrorCode::ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE, |cause| cause.rethrow())
    ///     .finally(|| finished = true)
    ///     .execute();
    ///
    /// assert_eq!(finished, true);
    /// assert_eq!(result, 42);
    /// ```
    #[must_use = "must call `PgTryBuilder::execute(self)` in order for it to run"]
    pub fn new(func: F) -> Self {
        Self { func, when: Default::default(), others: None, rust: None, finally: None }
    }

    /// Add a catch handler to run should a specific error occur during execution.
    ///
    /// The argument to the catch handler closure is a [`CaughtError`] which can be
    /// rethrown via [`CaughtError::rethrow()`]
    ///
    /// The return value must be of the same type as the main execution block, or the supplied
    /// error cause must be rethrown.
    ///
    /// ## Safety
    ///
    /// While this function isn't itself unsafe, catching and then ignoring an important internal
    /// Postgres error may very well leave your database in an undesirable state.  This is your
    /// responsibility.
    #[must_use = "must call `PgTryBuilder::execute(self)` in order for it to run"]
    pub fn catch_when(
        mut self,
        error: PgSqlErrorCode,
        f: impl FnMut(CaughtError) -> R + 'a + UnwindSafe + RefUnwindSafe,
    ) -> Self {
        self.when.insert(error, Box::new(f));
        self
    }

    /// Add a catch-all handler to catch a raised error that wasn't explicitly caught via
    /// [PgTryBuilder::catch_when].
    ///
    /// The argument to the catch handler closure is a [`CaughtError`] which can be
    /// rethrown via [`CaughtError::rethrow()`].
    ///
    /// The return value must be of the same type as the main execution block, or the supplied
    /// error cause must be rethrown.
    ///
    /// ## Safety
    ///
    /// While this function isn't itself unsafe, catching and then ignoring an important internal
    /// Postgres error may very well leave your database in an undesirable state.  This is your
    /// responsibility.
    #[must_use = "must call `PgTryBuilder::execute(self)` in order for it to run"]
    pub fn catch_others(
        mut self,
        f: impl FnMut(CaughtError) -> R + 'a + UnwindSafe + RefUnwindSafe,
    ) -> Self {
        self.others = Some(Box::new(f));
        self
    }

    /// Add a handler to specifically respond to a Rust panic.
    ///
    /// The catch handler's closure argument is a [`CaughtError::PostgresError {ereport, payload}`]
    /// that you can inspect in whatever way makes sense.
    ///
    /// The return value must be of the same type as the main execution block, or the supplied
    /// error cause must be rethrown.
    ///
    #[must_use = "must call `PgTryBuilder::execute(self)` in order for it to run"]
    pub fn catch_rust_panic(
        mut self,
        f: impl FnMut(CaughtError) -> R + 'a + UnwindSafe + RefUnwindSafe,
    ) -> Self {
        self.rust = Some(Box::new(f));
        self
    }

    /// The finally block, of which there can be only one.  Successive calls to this function
    /// will replace the prior finally block.
    ///
    /// The finally block closure is called after successful return from the main execution handler
    /// or a catch handler.  The finally block does not return a value.
    #[must_use = "must call `PgTryBuilder::execute(self)` in order for it to run"]
    pub fn finally(mut self, f: impl FnMut() + 'a) -> Self {
        self.finally = Some(Box::new(f));
        self
    }

    /// Run the main execution block closure.  Any error raised will be passed to a registered
    /// catch handler, and when finished, the finally block will be run.
    pub fn execute(mut self) -> R {
        let result = catch_unwind(self.func);

        fn finally<F: FnMut()>(f: &mut Option<F>) {
            if let Some(f) = f {
                f()
            }
        }

        let result = match result {
            Ok(result) => result,
            Err(error) => {
                let (sqlerrcode, root_cause) = match downcast_panic_payload(error) {
                    CaughtError::RustPanic { ereport, payload } => {
                        let sqlerrcode = ereport.inner.sqlerrcode;
                        let panic = CaughtError::RustPanic { ereport, payload };
                        (sqlerrcode, panic)
                    }
                    CaughtError::ErrorReport(ereport) => {
                        let sqlerrcode = ereport.inner.sqlerrcode;
                        let panic = CaughtError::ErrorReport(ereport);
                        (sqlerrcode, panic)
                    }
                    CaughtError::PostgresError(ereport) => {
                        let sqlerrcode = ereport.inner.sqlerrcode;
                        let panic = CaughtError::PostgresError(ereport);
                        (sqlerrcode, panic)
                    }
                };

                // Postgres source docs says that a PG_TRY/PG_CATCH/PG_FINALLY block can't have
                // both a CATCH and a FINALLY.
                //
                // We allow it by wrapping handler execution in its own `catch_unwind()` block
                // and deferring the finally block execution until after it is complete
                let handler_result = catch_unwind(AssertUnwindSafe(|| {
                    if let Some(mut handler) = self.when.remove(&sqlerrcode) {
                        // we have a registered catch handler for the error code we caught
                        return handler(root_cause);
                    } else if let Some(mut handler) = self.others {
                        // we have a registered "catch others" handler
                        return handler(root_cause);
                    } else if let Some(mut handler) = self.rust {
                        // we have a registered catch handler for a rust panic
                        if let cause @ CaughtError::RustPanic { .. } = root_cause {
                            // and we have a rust panic
                            return handler(cause);
                        }
                    }

                    // we have no handler capable of handling whatever error we have, so rethrow the root cause
                    root_cause.rethrow();
                }));

                let handler_result = match handler_result {
                    Ok(result) => result,
                    Err(caught) => {
                        let catch_handler_error = downcast_panic_payload(caught);

                        // make sure to run the finally block and then resume unwinding
                        // with this new panic
                        finally(&mut self.finally);
                        resume_unwind(Box::new(catch_handler_error))
                    }
                };

                // Being here means the catch handler didn't raise an error.
                //
                // Postgres says:
                unsafe {
                    /*
                     * FlushErrorState --- flush the error state after error recovery
                     *
                     * This should be called by an error handler after it's done processing
                     * the error; or as soon as it's done CopyErrorData, if it intends to
                     * do stuff that is likely to provoke another error.  You are not "out" of
                     * the error subsystem until you have done this.
                     */
                    crate::FlushErrorState();
                }
                handler_result
            }
        };

        // `result` could be from a successful execution or returned from a catch handler
        //
        // Either way, the finally block needs to be run before we return it
        finally(&mut self.finally);
        result
    }
}
