//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.

use crate::{PGRXSharedMemory, PgSharedMemoryInitialization};
use core::ops::{Deref, DerefMut};
use std::cell::UnsafeCell;
use std::ffi::CStr;

/// A Rust locking mechanism which uses a PostgreSQL LWLock to lock the data.
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
    inner: UnsafeCell<*mut Shared<T>>,
}

unsafe impl<T: PGRXSharedMemory> Sync for PgLwLock<T> {}

impl<T> PgLwLock<T> {
    /// Create a pointer of a lock that points to nothing.
    ///
    /// # Safety
    ///
    /// * Caller must be confident that there are no name conflicts.
    pub const unsafe fn new(name: &'static CStr) -> Self {
        Self { name, inner: UnsafeCell::new(std::ptr::null_mut()) }
    }

    /// Get the name of the atomic.
    pub const fn name(&self) -> &'static CStr {
        self.name
    }
}

impl<T: PGRXSharedMemory> PgLwLock<T> {
    /// Obtain a shared lock (which comes with `&T` access).
    pub fn share(&self) -> PgLwLockShareGuard<'_, T> {
        unsafe {
            let shared = self.inner.get().read().as_ref().expect("PgLwLock was not initialized");
            crate::pg_sys::LWLockAcquire(shared.lock, crate::pg_sys::LWLockMode::LW_SHARED);
            PgLwLockShareGuard { data: &*shared.data.get(), lock: shared.lock }
        }
    }

    /// Obtain an exclusive lock (which comes with `&mut T` access).
    pub fn exclusive(&self) -> PgLwLockExclusiveGuard<'_, T> {
        unsafe {
            let shared = self.inner.get().read().as_ref().expect("PgLwLock was not initialized");
            crate::pg_sys::LWLockAcquire(shared.lock, crate::pg_sys::LWLockMode::LW_EXCLUSIVE);
            PgLwLockExclusiveGuard { data: &mut *shared.data.get(), lock: shared.lock }
        }
    }
}

impl<T: PGRXSharedMemory> PgSharedMemoryInitialization for PgLwLock<T> {
    type Value = T;

    unsafe fn on_shmem_request(&'static self) {
        unsafe {
            crate::pg_sys::RequestAddinShmemSpace(size_of::<Shared<T>>());
            crate::pg_sys::RequestNamedLWLockTranche(self.name.as_ptr(), 1);
        }
    }

    unsafe fn on_shmem_startup(&'static self, value: T) {
        unsafe {
            use crate::pg_sys;

            let shm_name = self.name;
            let addin_shmem_init_lock = &raw mut (*pg_sys::MainLWLockArray.add(21)).lock;
            pg_sys::LWLockAcquire(addin_shmem_init_lock, pg_sys::LWLockMode::LW_EXCLUSIVE);

            let mut found = false;
            let fv_shmem =
                pg_sys::ShmemInitStruct(shm_name.as_ptr(), size_of::<Shared<T>>(), &mut found)
                    .cast::<Shared<T>>();
            assert!(fv_shmem.is_aligned(), "shared memory is not aligned");
            if !found {
                fv_shmem.write(Shared {
                    data: UnsafeCell::new(value),
                    lock: &raw mut (*pg_sys::GetNamedLWLockTranche(shm_name.as_ptr())).lock,
                });
            }

            *self.inner.get() = fv_shmem;

            pg_sys::LWLockRelease(addin_shmem_init_lock);
        }
    }
}

#[repr(C)]
struct Shared<T> {
    data: UnsafeCell<T>,
    lock: *mut crate::pg_sys::LWLock,
}

pub struct PgLwLockShareGuard<'a, T> {
    data: &'a T,
    lock: *mut crate::pg_sys::LWLock,
}

unsafe impl<T: PGRXSharedMemory> Sync for PgLwLockShareGuard<'_, T> {}

impl<T> Drop for PgLwLockShareGuard<'_, T> {
    fn drop(&mut self) {
        // SAFETY: self.lock is always valid
        unsafe { release_unless_elog_unwinding(self.lock) }
    }
}

impl<T> Deref for PgLwLockShareGuard<'_, T> {
    type Target = T;

    #[inline]
    fn deref(&self) -> &T {
        self.data
    }
}

pub struct PgLwLockExclusiveGuard<'a, T> {
    data: &'a mut T,
    lock: *mut crate::pg_sys::LWLock,
}

unsafe impl<T: PGRXSharedMemory> Sync for PgLwLockExclusiveGuard<'_, T> {}

impl<T> Deref for PgLwLockExclusiveGuard<'_, T> {
    type Target = T;

    #[inline]
    fn deref(&self) -> &T {
        self.data
    }
}

impl<T> DerefMut for PgLwLockExclusiveGuard<'_, T> {
    #[inline]
    fn deref_mut(&mut self) -> &mut T {
        self.data
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
unsafe fn release_unless_elog_unwinding(lock: *mut crate::pg_sys::LWLock) {
    // SAFETY: mut static access is ok from a single (main) thread.
    if crate::pg_sys::InterruptHoldoffCount > 0 {
        crate::pg_sys::LWLockRelease(lock);
    }
}
