//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.

use crate::{PGRXSharedMemory, pg_sys};
use core::mem::MaybeUninit;
use core::ops::{Deref, DerefMut};
use std::cell::UnsafeCell;

/// A Rust locking mechanism which uses a PostgreSQL `slock_t` to lock the data.
///
/// Note that this lock does not handle poisoning, unlike [`std::sync::Mutex`],
/// but is similar in most other respects (aside from supporting cross-process
/// use).
///
/// In most cases, [PgLwLock](crate::PgLwLock) should be preferred. Be aware of
/// the following documentation from [`storage/spin.h`]:
///
/// > Keep in mind the coding rule that spinlocks must not be held for more than
/// > a few instructions.  In particular, we assume it is not possible for a
/// > CHECK_FOR_INTERRUPTS() to occur while holding a spinlock, and so it is not
/// > necessary to do HOLD/RESUME_INTERRUPTS() in these macros.
///
/// [`storage/spin.h`]:
///     https://github.com/postgres/postgres/blob/1f0c4fa255253d223447c2383ad2b384a6f05854/src/include/storage/spin.h
#[doc(alias = "slock_t")]
pub struct PgSpinLock<T> {
    data: UnsafeCell<T>,
    lock: UnsafeCell<pg_sys::slock_t>,
}

unsafe impl<T: PGRXSharedMemory> Sync for PgSpinLock<T> {}
unsafe impl<T: PGRXSharedMemory> PGRXSharedMemory for PgSpinLock<T> {}

impl<T> PgSpinLock<T> {
    /// Create a new [`PgSpinLock`]. See the type documentation for more info.
    #[inline]
    #[doc(alias = "SpinLockInit")]
    pub fn new(value: T) -> Self {
        let mut slock = MaybeUninit::zeroed();
        // Safety: We initialize the `slock_t` before use (and it was likely
        // already properly initialized by `zeroed()` in the first place, since
        // it's probably a primitive integer).
        unsafe {
            pg_sys::SpinLockInit(slock.as_mut_ptr());
            Self { data: UnsafeCell::new(value), lock: UnsafeCell::new(slock.assume_init()) }
        }
    }
}

impl<T: PGRXSharedMemory> PgSpinLock<T> {
    /// Returns true if the spinlock is locked, and false otherwise.
    #[inline]
    #[doc(alias = "SpinLockFree")]
    pub fn is_locked(&self) -> bool {
        // SAFETY: Doesn't actually modify state, despite appearances.
        unsafe { !pg_sys::SpinLockFree(self.lock.get()) }
    }

    /// Returns a lock guard for the spinlock. See the [`PgSpinLockGuard`]
    /// documentation for more info.
    #[inline]
    #[doc(alias = "SpinLockAcquire")]
    pub fn lock(&self) -> PgSpinLockGuard<'_, T> {
        unsafe {
            pg_sys::SpinLockAcquire(self.lock.get());
            PgSpinLockGuard { data: &mut *self.data.get(), lock: self.lock.get() }
        }
    }
}

/// An implementation of a "scoped lock" for a [`PgSpinLock`]. When this
/// structure falls out of scope (is dropped), the lock will be unlocked.
///
/// See the documentation of [`PgSpinLock`] for more information.
pub struct PgSpinLockGuard<'a, T: 'a> {
    data: &'a mut T,
    lock: *mut pg_sys::slock_t,
}

unsafe impl<T: PGRXSharedMemory> Sync for PgSpinLockGuard<'_, T> {}

impl<T> Drop for PgSpinLockGuard<'_, T> {
    #[inline]
    #[doc(alias = "SpinLockRelease")]
    fn drop(&mut self) {
        unsafe { pg_sys::SpinLockRelease(self.lock) };
    }
}

impl<T> Deref for PgSpinLockGuard<'_, T> {
    type Target = T;

    #[inline]
    fn deref(&self) -> &T {
        self.data
    }
}

impl<T> DerefMut for PgSpinLockGuard<'_, T> {
    #[inline]
    fn deref_mut(&mut self) -> &mut T {
        self.data
    }
}
