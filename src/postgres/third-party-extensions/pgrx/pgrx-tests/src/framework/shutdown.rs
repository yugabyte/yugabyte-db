//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use std::panic::{self, AssertUnwindSafe, Location};
use std::sync::{Mutex, PoisonError};
use std::{any, mem, process};

/// Register a shutdown hook to be called when the process exits.
///
/// Note that shutdown hooks are only run on the client, so must be added from
/// your `setup` function, not the `#[pg_test]` itself.
#[track_caller]
pub fn add_shutdown_hook<F: FnOnce() + Send + 'static>(func: F) {
    SHUTDOWN_HOOKS
        .lock()
        .unwrap_or_else(PoisonError::into_inner)
        .push(ShutdownHook { source: Location::caller(), callback: Box::new(func) });
}

pub(super) fn register_shutdown_hook() {
    unsafe {
        libc::atexit(run_shutdown_hooks);
    }
}

/// The `atexit` callback.
///
/// If we panic from `atexit`, we end up causing `exit` to unwind. Unwinding
/// from a nounwind + noreturn function can cause some destructors to run twice,
/// causing (for example) libtest to SIGSEGV.
///
/// This ends up looking like a memory bug in either `pgrx` or the user code, and
/// is very hard to track down, so we go to some lengths to prevent it.
/// Essentially:
///
/// - Panics in each user hook are caught and reported.
/// - As a stop-gap an abort-on-drop panic guard is used to ensure there isn't a
///   place we missed.
///
/// We also write to stderr directly instead, since otherwise our output will
/// sometimes be redirected.
extern "C" fn run_shutdown_hooks() {
    let guard = PanicGuard;
    let mut any_panicked = false;
    let mut hooks = SHUTDOWN_HOOKS.lock().unwrap_or_else(PoisonError::into_inner);
    // Note: run hooks in the opposite order they were registered.
    for hook in mem::take(&mut *hooks).into_iter().rev() {
        any_panicked |= hook.run().is_err();
    }
    if any_panicked {
        write_stderr("error: one or more shutdown hooks panicked (see `stderr` for details).\n");
        process::abort()
    }
    mem::forget(guard);
}

/// Prevent panics in a block of code.
///
/// Prints a message and aborts in its drop. Intended usage is like:
/// ```ignore
/// let guard = PanicGuard;
/// // ...code that absolutely must never unwind goes here...
/// core::mem::forget(guard);
/// ```
struct PanicGuard;
impl Drop for PanicGuard {
    fn drop(&mut self) {
        write_stderr("Failed to catch panic in the `atexit` callback, aborting!\n");
        process::abort();
    }
}

static SHUTDOWN_HOOKS: Mutex<Vec<ShutdownHook>> = Mutex::new(Vec::new());

struct ShutdownHook {
    source: &'static Location<'static>,
    callback: Box<dyn FnOnce() + Send>,
}

impl ShutdownHook {
    fn run(self) -> Result<(), ()> {
        let Self { source, callback } = self;
        let result = panic::catch_unwind(AssertUnwindSafe(callback));
        if let Err(e) = result {
            let msg = failure_message(&e);
            write_stderr(&format!(
                "error: shutdown hook (registered at {source}) panicked: {msg}\n"
            ));
            Err(())
        } else {
            Ok(())
        }
    }
}

fn failure_message(e: &(dyn any::Any + Send)) -> &str {
    if let Some(&msg) = e.downcast_ref::<&'static str>() {
        msg
    } else if let Some(msg) = e.downcast_ref::<String>() {
        msg.as_str()
    } else {
        "<panic payload of unknown type>"
    }
}

/// Write to stderr, bypassing libtest's output redirection. Doesn't append `\n`.
fn write_stderr(s: &str) {
    use std::io::Write;
    let _ = std::io::stderr().lock().write_all(s.as_bytes());
}
