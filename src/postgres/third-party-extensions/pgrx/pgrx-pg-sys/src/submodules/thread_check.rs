//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
//! Enforces thread-safety in `pgrx`.
//!
//! It's UB to call into Postgres functions from multiple threads. We handle
//! this by remembering the first thread to call into postgres functions (the
//! "active thread" in some comments), and panicking if we're called from a
//! different thread. On some OSes, we (attempt to) verify that the active
//! thread is actually the main thread too.
//!
//! This is called from the current crate from inside the setjmp shim, as that
//! code is *definitely* unsound to call in the presence of multiple threads.
//!
//! This is somewhat heavyhanded, and should be called from fewer places in the
//! future...

use std::num::NonZeroUsize;
use std::sync::atomic::{AtomicUsize, Ordering};

static ACTIVE_THREAD: AtomicUsize = AtomicUsize::new(0);
#[track_caller]
pub(crate) fn check_active_thread() {
    let current_thread = nonzero_thread_id();
    // Relaxed is sufficient as we're only interested in the effects on a single
    // atomic variable, and don't need synchronization beyond that.
    match ACTIVE_THREAD.load(Ordering::Relaxed) {
        0 => init_active_thread(current_thread),
        thread_id => {
            if current_thread.get() != thread_id {
                thread_id_check_failed();
            }
        }
    }
}

/// Use OS-specific mechanisms to detect if we're the process main thread, if
/// supported on the OS. Should return `None` when unsupported, or if there's an
/// error.
///
/// Concretely, it is very important that this not return `Some(false)`
/// incorrectly, but the other values are less important. Callers generally
/// should compare the result against `Some(false)`.
pub(super) fn is_os_main_thread() -> Option<bool> {
    #[cfg(any(target_os = "macos", target_os = "openbsd", target_os = "freebsd"))]
    return unsafe {
        match libc::pthread_main_np() {
            1 => Some(true),
            0 => Some(false),
            // Note that this returns `-1` in some error conditions.
            //
            // In these cases we are almost certainly not the main thread, but
            // we don't know -- it's better for this function to return `None`
            // in cases of uncertainty.
            _ => None,
        }
    };
    #[cfg(target_os = "linux")]
    return unsafe {
        // Note: `gettid()` is in glibc 2.30+ (from 2019) and musl 1.2.2+ (from late 2020).
        // It's not clear if we are able to raise pgrx's support requirements
        // beyond "libc on a non-EOL OS that is also supported by Rust".
        //
        // So for now, we just use the raw syscall instead, which is available
        // in all versions of linux that Rust supports (exposing `gettid()` from
        // glibc was extremely contentious for various reasons).
        let tid = libc::syscall(libc::SYS_gettid) as core::ffi::c_long;
        let pid = libc::getpid() as core::ffi::c_long;
        Some(tid == pid)
    };
    // hacky cfg-if
    #[allow(unreachable_code)]
    {
        None
    }
}

#[track_caller]
fn init_active_thread(tid: NonZeroUsize) {
    // Check if we're the actual honest-to-god main thread (if we know). Or at
    // least make sure we detect cases where we're definitely *not* the OS main
    // thread.
    //
    // This avoids a case where Rust and Postgres disagree about the main
    // thread. Such cases are almost certainly going to fail the thread check
    // *eventually*.
    if is_os_main_thread() == Some(false) {
        panic!("Attempt to initialize `pgrx` active thread from a thread other than the main");
    }
    match ACTIVE_THREAD.compare_exchange(0, tid.get(), Ordering::Relaxed, Ordering::Relaxed) {
        #[cfg(all(target_family = "unix", not(target_os = "emscripten")))]
        Ok(_) => unsafe {
            // We won the race. Register an atfork handler to clear the atomic
            // in any child processes we spawn.
            extern "C" fn clear_in_child() {
                ACTIVE_THREAD.store(0, Ordering::Relaxed);
            }
            libc::pthread_atfork(None, None, Some(clear_in_child));
        },
        #[allow(unreachable_patterns)]
        Ok(_) => (),
        Err(_) => {
            thread_id_check_failed();
        }
    }
}

#[cold]
#[inline(never)]
#[track_caller]
fn thread_id_check_failed() -> ! {
    // I don't think this can ever happen, but it would be a bug if it could.
    assert_ne!(is_os_main_thread(), Some(true), "`pgrx` active thread is not the main thread!?");
    panic!(
        "{}:  postgres FFI may not be called from multiple threads.",
        std::panic::Location::caller()
    );
}

fn nonzero_thread_id() -> NonZeroUsize {
    // Returns the `addr()` of a thread local variable.
    //
    // For now this is reasonably efficient, but could be (substantially, for
    // our use) improved by using a pointer to the thread control block, which
    // can avoid going through `__tls_get_addr`.
    //
    // Sadly, doing that would require some fiddly platform-specific inline
    // assembly, which is enough of a pain that it's not worth bothering with
    // for now. That said, in the future if this becomes a performance issue,
    // that'd be a good fix.
    //
    // For examples of how to do this, see the how checks for cross-thread frees
    // on are implemented in thread-pooling allocators, ex:
    // - https://github.com/microsoft/mimalloc/blob/f2712f4a8f038a7fb4df2790f4c3b7e3ed9e219b/include/mimalloc-internal.h#L871
    // - https://github.com/mjansson/rpmalloc/blob/f56e2f6794eab5c280b089c90750c681679fde92/rpmalloc/rpmalloc.c#L774
    // and so on.
    std::thread_local! {
        static BYTE: u8 = const { 0 };
    }
    BYTE.with(|p: &u8| {
        // Note: Avoid triggering the `unstable_name_collisions` lint.
        let addr = sptr::Strict::addr(p as *const u8);
        // SAFETY: `&u8` is always nonnull, so its address is always nonzero.
        unsafe { NonZeroUsize::new_unchecked(addr) }
    })
}
