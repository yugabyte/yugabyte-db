//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
#![allow(clippy::needless_borrow)]
use crate::pg_sys;
use core::ops::{Deref, DerefMut};
use std::cell::UnsafeCell;
use std::ffi::CStr;

/// A Rust locking mechanism which uses a PostgreSQL LWLock to lock the data
///
/// This type of lock allows a number of readers or at most one writer at any
/// point in time. The write portion of this lock typically allows modification
/// of the underlying data (exclusive access) and the read portion of this lock
/// typically allows for read-only access (shared access).
///
/// The lock is valid across processes as the LWLock is managed by Postgres. Data
/// mutability once a lock is obtained is handled by Rust giving out `&` or `&mut`
/// pointers.
///
/// When a lock is given out it is wrapped in a PgLwLockShareGuard or
/// PgLwLockExclusiveGuard, which releases the lock on drop
///
/// # Poisoning
/// This lock can not be poisoned from Rust. Panic and Abort are handled by
/// PostgreSQL cleanly.
pub struct PgLwLock<T> {
    name: &'static CStr,
    inner: UnsafeCell<*mut PgLwLockShared<T>>,
}

unsafe impl<T: Send> Send for PgLwLock<T> {}
unsafe impl<T: Send + Sync> Sync for PgLwLock<T> {}

impl<T> PgLwLock<T> {
    /// Create an empty lock which can be created as a global with None as a
    /// sentinel value
    pub const fn new(name: &'static CStr) -> Self {
        PgLwLock { name, inner: UnsafeCell::new(std::ptr::null_mut()) }
    }

    /// Get the name of the PgLwLock
    pub fn name(&self) -> &'static CStr {
        self.name
    }

    /// Obtain a shared lock (which comes with `&T` access)
    pub fn share(&self) -> PgLwLockShareGuard<T> {
        unsafe {
            let shared = self.inner.get().read().as_ref().expect("PgLwLock was not initialized");
            pg_sys::LWLockAcquire((*shared).lock_ptr, pg_sys::LWLockMode::LW_SHARED);
            PgLwLockShareGuard { data: &*(*shared).data.get(), lock: (*shared).lock_ptr }
        }
    }

    /// Obtain an exclusive lock (which comes with `&mut T` access)
    pub fn exclusive(&self) -> PgLwLockExclusiveGuard<T> {
        unsafe {
            let shared = self.inner.get().read().as_ref().expect("PgLwLock was not initialized");
            pg_sys::LWLockAcquire((*shared).lock_ptr, pg_sys::LWLockMode::LW_EXCLUSIVE);
            PgLwLockExclusiveGuard { data: &mut *(*shared).data.get(), lock: (*shared).lock_ptr }
        }
    }

    /// Attach an empty PgLwLock lock to a LWLock, and wrap T
    /// SAFETY: Must only be called from inside the Postgres shared memory init hook
    pub unsafe fn attach(&self, value: *mut PgLwLockShared<T>) {
        *self.inner.get() = value;
    }
}

#[repr(C)]
pub struct PgLwLockShared<T> {
    pub data: UnsafeCell<T>,
    pub lock_ptr: *mut pg_sys::LWLock,
}

pub struct PgLwLockShareGuard<'a, T> {
    data: &'a T,
    lock: *mut pg_sys::LWLock,
}

impl<T> Drop for PgLwLockShareGuard<'_, T> {
    fn drop(&mut self) {
        // SAFETY: self.lock is always valid
        unsafe { release_unless_elog_unwinding(self.lock) }
    }
}

impl<T> Deref for PgLwLockShareGuard<'_, T> {
    type Target = T;

    fn deref(&self) -> &T {
        &self.data
    }
}

pub struct PgLwLockExclusiveGuard<'a, T> {
    data: &'a mut T,
    lock: *mut pg_sys::LWLock,
}

impl<T> Deref for PgLwLockExclusiveGuard<'_, T> {
    type Target = T;

    fn deref(&self) -> &T {
        &self.data
    }
}

impl<T> DerefMut for PgLwLockExclusiveGuard<'_, T> {
    fn deref_mut(&mut self) -> &mut T {
        &mut self.data
    }
}

impl<T> Drop for PgLwLockExclusiveGuard<'_, T> {
    fn drop(&mut self) {
        // SAFETY: self.lock is always valid
        unsafe { release_unless_elog_unwinding(self.lock) }
    }
}

/// Releases the given lock, unless we are unwinding due to an `error` in postgres code
///
/// `elog(ERROR)` from postgres code resets `pg_sys::InterruptHoldoffCount` to zero, and
/// `LWLockRelease` fails an assertion if called in this case.
/// If we detect this condition, we skip releasing the lock; all lwlocks will be released
/// on (sub)transaction abort anyway.
///
/// SAFETY: the given lock must be valid
unsafe fn release_unless_elog_unwinding(lock: *mut pg_sys::LWLock) {
    // SAFETY: mut static access is ok from a single (main) thread.
    if pg_sys::InterruptHoldoffCount > 0 {
        pg_sys::LWLockRelease(lock);
    }
}
