//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
#![deny(unsafe_op_in_unsafe_fn)]

#[cfg(not(all(
    any(target_os = "linux", target_os = "macos"),
    any(target_arch = "x86_64", target_arch = "aarch64")
)))]
mod cee_scape {
    #[cfg(not(feature = "cshim"))]
    compile_error!("target platform cannot work without feature cshim");

    use libc::{c_int, c_void};
    use std::marker::PhantomData;

    #[repr(C)]
    pub struct SigJmpBufFields {
        _internal: [u8; 0],
        _neither_send_nor_sync: PhantomData<*const u8>,
    }

    pub fn call_with_sigsetjmp<F>(savemask: bool, mut callback: F) -> c_int
    where
        F: for<'a> FnOnce(&'a SigJmpBufFields) -> c_int,
    {
        extern "C-unwind" {
            fn call_closure_with_sigsetjmp(
                savemask: c_int,
                closure_env_ptr: *mut c_void,
                closure_code: extern "C-unwind" fn(
                    jbuf: *const SigJmpBufFields,
                    env_ptr: *mut c_void,
                ) -> c_int,
            ) -> c_int;
        }

        extern "C-unwind" fn call_from_c_to_rust<F>(
            jbuf: *const SigJmpBufFields,
            closure_env_ptr: *mut c_void,
        ) -> c_int
        where
            F: for<'a> FnOnce(&'a SigJmpBufFields) -> c_int,
        {
            let closure_env_ptr: *mut F = closure_env_ptr as *mut F;
            unsafe { (closure_env_ptr.read())(&*jbuf) }
        }

        let savemask: libc::c_int = if savemask { 1 } else { 0 };

        unsafe {
            let closure_env_ptr = core::ptr::addr_of_mut!(callback);
            core::mem::forget(callback);
            call_closure_with_sigsetjmp(
                savemask,
                closure_env_ptr as *mut libc::c_void,
                call_from_c_to_rust::<F>,
            )
        }
    }
}

use cee_scape::{call_with_sigsetjmp, SigJmpBufFields};

/**
Given a closure that is assumed to be a wrapped Postgres `extern "C-unwind"` function, [pg_guard_ffi_boundary]
works with the Postgres and C runtimes to create a "barrier" that allows Rust to catch Postgres errors
(`elog(ERROR)`) while running the supplied closure. This is done for the sake of allowing Rust to run
destructors before Postgres destroys the memory contexts that Rust-in-Postgres code may be enmeshed in.

Wrapping the FFI into Postgres enables
- memory safety
- improving error logging
- minimizing resource leaks

But only the first of these is considered paramount.

At all times PGRX reserves the right to choose an implementation that achieves memory safety.
Currently, this function is used to protect **every** bindgen-generated Postgres `extern "C-unwind"` function.

Generally, the only time *you'll* need to use this function is when calling a Postgres-provided
function pointer.

# Safety

It is undefined behavior if the function passed to `pg_guard_ffi_boundary` have objects with
destructors on the stack when postgres raises an `ERROR`. For example, the following is
both a resource leak, and undefined behavior (as it needs to be a [trivially-deallocated
stack frame]):

```rust,ignore
// This is UB!
pgrx::pg_sys::ffi::pg_guard_ffi_boundary(|| {
    let data = vec![1, 2, 3, 4, 5];
    // call FFI function that raises an ERROR
});
```
Instead, you should write it like
```rust,ignore
let data = vec![1, 2, 3, 4, 5];
pgrx::pg_sys::ffi::pg_guard_ffi_boundary(|| {
    // call FFI function that raises an ERROR
});
```

Further, it is undefined behavior if the function passed into `pg_guard_ffi_boundary` panics. It
is recommended that you keep the body of the `pg_guard_ffi_boundary` closure very small -- ideally
*only* containing a call to some C function, rather than containing any logic or variables of its
own.

Furthermore, Postgres is a single-threaded runtime.  As such, [`pg_guard_ffi_boundary`] should
**only** be called from the main thread.  In fact, [`pg_guard_ffi_boundary`] will detect this
and immediately panic.

More generally, Rust cannot guarantee destructors are always run, PGRX is written in Rust code, and
the implementation of `pg_guard_ffi_boundary` relies on help from Postgres, the OS, and the C runtime;
thus, relying on the FFI boundary catching an error and propagating it back into Rust to guarantee
Rust's language-level memory safety when calling Postgres is unsound (i.e. there are no promises).
Postgres can and does opt to erase exception and error context stacks in some situations.
The C runtime is beholden to the operating system, which may do as it likes with a thread.
PGRX has many magical powers, some of considerable size, but they are not infinite cosmic power.

Thus, if Postgres gives you a pointer into the database's memory, and you corrupt that memory
in a way technically permitted by Rust, intending to fix it before Postgres or Rust notices,
then you may not call Postgres and expect Postgres to not notice the code crimes in progress.
Postgres and Rust will see you. Whether they choose to ignore such misbehavior is up to them, not PGRX.
If you are manipulating transient "pure Rust" data, however, it is unlikely this is of consequence.

# Implementation Note

The main implementation uses `sigsetjmp`, [`pg_sys::error_context_stack`], and [`pg_sys::PG_exception_stack`].
which, when Postgres enters its exception handling in `elog.c`, will prompt a `siglongjmp` back to it.

This caught error is then converted into a Rust `panic!()` and propagated up the stack, ultimately
being converted into a transaction-aborting Postgres `ERROR` by PGRX.

[trivially-deallocated stack frame]: https://github.com/rust-lang/rfcs/blob/master/text/2945-c-unwind-abi.md#plain-old-frames
**/
use crate as pg_sys;
use crate::panic::{CaughtError, ErrorReport, ErrorReportLocation, ErrorReportWithLevel};
use core::ffi::CStr;
use std::mem::MaybeUninit;

#[inline(always)]
#[track_caller]
pub unsafe fn pg_guard_ffi_boundary<T, F: FnOnce() -> T>(f: F) -> T {
    // SAFETY: Caller promises not to call us from anything but the main thread.
    unsafe { pg_guard_ffi_boundary_impl(f) }
}

#[inline(always)]
#[track_caller]
unsafe fn pg_guard_ffi_boundary_impl<T, F: FnOnce() -> T>(f: F) -> T {
    //! This is the version that uses sigsetjmp and all that, for "normal" Rust/PGRX interfaces.

    // The next code is definitely thread-unsafe (it manipulates statics in an
    // unsynchronized manner), so we may as well check here.
    super::thread_check::check_active_thread();

    // SAFETY: This should really, really not be done in a multithreaded context as it
    // accesses multiple `static mut`. The ultimate caller asserts this is the main thread.
    unsafe {
        let caller_memxct = pg_sys::CurrentMemoryContext;
        let prev_exception_stack = pg_sys::PG_exception_stack;
        let prev_error_context_stack = pg_sys::error_context_stack;
        let mut result: std::mem::MaybeUninit<T> = MaybeUninit::uninit();
        let jump_value = call_with_sigsetjmp(false, |jump_buffer| {
            // Make Postgres' error-handling system aware of our new
            // setjmp/longjmp restore point.
            pg_sys::PG_exception_stack = std::mem::transmute(jump_buffer as *const SigJmpBufFields);

            // execute the closure, which will be a wrapped internal Postgres function
            result.write(f());
            0
        });

        if jump_value == 0 {
            // Flag is 0, so we've taken the successful return path. We're not
            // here as the result of a longjmp.
            // Restore Postgres' understanding of where its next longjmp should go
            pg_sys::PG_exception_stack = prev_exception_stack;
            pg_sys::error_context_stack = prev_error_context_stack;

            result.assume_init()
        } else {
            // We've landed here b/c of a longjmp originating in Postgres

            // the overhead to get the current [ErrorData] from Postgres and convert
            // it into our [ErrorReportWithLevel] seems worth the user benefit
            //
            // Note that this only happens in the case of us trapping an error

            // At this point, we're running within `pg_sys::ErrorContext`, but should be in the
            // memory context the caller was in before we call [CopyErrorData()] and start using it
            pg_sys::CurrentMemoryContext = caller_memxct;

            // SAFETY: `pg_sys::CopyErrorData()` will always give us a valid pointer, so just assume so
            let errdata_ptr = pg_sys::CopyErrorData();
            let errdata = errdata_ptr.as_ref().unwrap_unchecked();

            // copy out the fields we need to support pgrx' error handling
            let level = errdata.elevel.into();
            let sqlerrcode = errdata.sqlerrcode.into();
            let message = errdata
                .message
                .is_null()
                .then(|| String::from("<null error message>"))
                .unwrap_or_else(|| CStr::from_ptr(errdata.message).to_string_lossy().to_string());
            let detail = errdata.detail.is_null().then_some(None).unwrap_or_else(|| {
                Some(CStr::from_ptr(errdata.detail).to_string_lossy().to_string())
            });
            let hint = errdata.hint.is_null().then_some(None).unwrap_or_else(|| {
                Some(CStr::from_ptr(errdata.hint).to_string_lossy().to_string())
            });
            let funcname = errdata.funcname.is_null().then_some(None).unwrap_or_else(|| {
                Some(CStr::from_ptr(errdata.funcname).to_string_lossy().to_string())
            });
            let file =
                errdata.filename.is_null().then(|| String::from("<null filename>")).unwrap_or_else(
                    || CStr::from_ptr(errdata.filename).to_string_lossy().to_string(),
                );
            let line = errdata.lineno as _;

            // clean up after ourselves by freeing the result of [CopyErrorData] and restoring
            // Postgres' understanding of where its next longjmp should go
            pg_sys::FreeErrorData(errdata_ptr);
            pg_sys::PG_exception_stack = prev_exception_stack;
            pg_sys::error_context_stack = prev_error_context_stack;

            // finally, turn this Postgres error into a Rust panic so that we can ensure proper
            // Rust stack unwinding and also defer handling until later
            std::panic::panic_any(CaughtError::PostgresError(ErrorReportWithLevel {
                level,
                inner: ErrorReport {
                    sqlerrcode,
                    message,
                    detail,
                    hint,
                    location: ErrorReportLocation { file, funcname, line, col: 0, backtrace: None },
                },
            }))
        }
    }
}
