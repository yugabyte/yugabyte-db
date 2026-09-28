//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.

/// In order to store a type in Postgres Shared Memory, it must be passed to
/// `pg_shmem_init!()` during `_PG_init()`.
///
/// Additionally, the type must be a `static` global and also be `#[derive(Copy, Clone)]`.
///
/// > Types that allocate on the heap, such as `String` and `Vec` are not supported.
///
/// For complex data structures like vecs and maps, `pgrx` prefers the use of types from
/// [`heapless`](https://crates.io/crates/heapless).
///
/// Custom types need to also implement the `PGRXSharedMemory` trait.
///
/// > Extensions that use shared memory **must** be loaded via `postgresql.conf`'s
/// > `shared_preload_libraries` configuration setting.
///
/// # Example
///
/// ```rust,no_run
/// use pgrx::prelude::*;
/// use pgrx::{PgAtomic, PgLwLock, pg_shmem_init, PgSharedMemoryInitialization};
/// use std::sync::atomic::AtomicBool;
///
/// // Primitive types must be protected behind a `PgLwLock`.
/// static PRIMITIVE: PgLwLock<i32> = unsafe { PgLwLock::new(c"primitive") };
///
/// // Rust atomics can be used without locks, wrapped in a `PgAtomic`.
/// static ATOMIC: PgAtomic<AtomicBool> = unsafe { PgAtomic::new(c"atomic") };
///
/// #[pg_guard]
/// pub extern "C-unwind" fn _PG_init() {
///     pg_shmem_init!(PRIMITIVE);
///     pg_shmem_init!(ATOMIC);
/// }
/// ```
#[macro_export]
macro_rules! pg_shmem_init {
    ($var:ident) => {
        $crate::pg_shmem_init!($var = Default::default())
    };
    ($var:ident = $e:expr) => {
        $crate::pg_sys::submodules::thread_check::check_active_thread();

        #[cfg(any(feature = "pg13", feature = "pg14"))]
        unsafe {
            $crate::shmem::PgSharedMemoryInitialization::on_shmem_request(&$var);
        }

        #[cfg(any(feature = "pg15", feature = "pg16", feature = "pg17", feature = "pg18"))]
        unsafe {
            static mut PREV_SHMEM_REQUEST_HOOK: Option<unsafe extern "C-unwind" fn()> = None;
            PREV_SHMEM_REQUEST_HOOK = pg_sys::shmem_request_hook;
            pg_sys::shmem_request_hook = Some(on_shmem_request);

            #[pg_guard]
            unsafe extern "C-unwind" fn on_shmem_request() {
                unsafe {
                    if let Some(i) = PREV_SHMEM_REQUEST_HOOK {
                        $crate::pg_sys::submodules::ffi::pg_guard_ffi_boundary(|| i());
                    }
                }
                unsafe {
                    $crate::shmem::PgSharedMemoryInitialization::on_shmem_request(&$var);
                }
            }
        }

        unsafe {
            static mut PREV_SHMEM_STARTUP_HOOK: Option<unsafe extern "C-unwind" fn()> = None;
            PREV_SHMEM_STARTUP_HOOK = pg_sys::shmem_startup_hook;
            pg_sys::shmem_startup_hook = Some(on_shmem_startup);

            #[pg_guard]
            #[forbid(unsafe_op_in_unsafe_fn)]
            unsafe extern "C-unwind" fn on_shmem_startup() {
                unsafe {
                    if let Some(i) = PREV_SHMEM_STARTUP_HOOK {
                        $crate::pg_sys::submodules::ffi::pg_guard_ffi_boundary(|| i());
                    }
                }
                let value = $e;
                unsafe {
                    $crate::shmem::PgSharedMemoryInitialization::on_shmem_startup(&$var, value);
                }
            }
        }
    };
}

/// Types for which it is safe to transfer and share references between PostgreSQL processes.
pub unsafe trait PGRXSharedMemory {}

unsafe impl PGRXSharedMemory for bool {}

unsafe impl PGRXSharedMemory for i8 {}

unsafe impl PGRXSharedMemory for u8 {}

unsafe impl PGRXSharedMemory for i16 {}

unsafe impl PGRXSharedMemory for u16 {}

unsafe impl PGRXSharedMemory for i32 {}

unsafe impl PGRXSharedMemory for u32 {}

unsafe impl PGRXSharedMemory for i64 {}

unsafe impl PGRXSharedMemory for u64 {}

unsafe impl PGRXSharedMemory for i128 {}

unsafe impl PGRXSharedMemory for u128 {}

unsafe impl PGRXSharedMemory for isize {}

unsafe impl PGRXSharedMemory for usize {}

#[cfg(target_has_atomic = "8")]
unsafe impl PGRXSharedMemory for std::sync::atomic::AtomicBool {}

#[cfg(target_has_atomic = "8")]
unsafe impl PGRXSharedMemory for std::sync::atomic::AtomicI8 {}

#[cfg(target_has_atomic = "8")]
unsafe impl PGRXSharedMemory for std::sync::atomic::AtomicU8 {}

#[cfg(target_has_atomic = "16")]
unsafe impl PGRXSharedMemory for std::sync::atomic::AtomicI16 {}

#[cfg(target_has_atomic = "16")]
unsafe impl PGRXSharedMemory for std::sync::atomic::AtomicU16 {}

#[cfg(target_has_atomic = "32")]
unsafe impl PGRXSharedMemory for std::sync::atomic::AtomicI32 {}

#[cfg(target_has_atomic = "32")]
unsafe impl PGRXSharedMemory for std::sync::atomic::AtomicU32 {}

#[cfg(target_has_atomic = "64")]
unsafe impl PGRXSharedMemory for std::sync::atomic::AtomicI64 {}

#[cfg(target_has_atomic = "64")]
unsafe impl PGRXSharedMemory for std::sync::atomic::AtomicU64 {}

#[cfg(target_has_atomic = "ptr")]
unsafe impl PGRXSharedMemory for std::sync::atomic::AtomicIsize {}

#[cfg(target_has_atomic = "ptr")]
unsafe impl PGRXSharedMemory for std::sync::atomic::AtomicUsize {}

unsafe impl PGRXSharedMemory for char {}

unsafe impl PGRXSharedMemory for f32 {}

unsafe impl PGRXSharedMemory for f64 {}

unsafe impl PGRXSharedMemory for str {}

unsafe impl<T: PGRXSharedMemory> PGRXSharedMemory for [T] {}

unsafe impl<const N: usize, T: PGRXSharedMemory> PGRXSharedMemory for [T; N] {}

macro_rules! impl_pg_sync_for_tuple {
    ($($t:ident),*) => {
        unsafe impl<$($t,)*> PGRXSharedMemory for ($($t,)*) where $($t: PGRXSharedMemory,)* {}
    };
}

impl_pg_sync_for_tuple!();
impl_pg_sync_for_tuple!(A);
impl_pg_sync_for_tuple!(A, B);
impl_pg_sync_for_tuple!(A, B, C);
impl_pg_sync_for_tuple!(A, B, C, D);
impl_pg_sync_for_tuple!(A, B, C, D, E);
impl_pg_sync_for_tuple!(A, B, C, D, E, F);

/// A trait that types can implement to provide their own Postgres Shared Memory initialization process.
pub trait PgSharedMemoryInitialization {
    type Value: PGRXSharedMemory;

    /// # Safety
    ///
    /// * Be called from inside PostgreSQL `shmem_request_hook`.
    /// * For PostgreSQL 13, 14, it could be called at any time.
    unsafe fn on_shmem_request(&'static self);

    /// # Safety
    ///
    /// * Be called from inside PostgreSQL `shmem_startup_hook`.
    unsafe fn on_shmem_startup(&'static self, value: Self::Value);
}

#[repr(transparent)]
pub struct AssertPGRXSharedMemory<T>(T);

impl<T> AssertPGRXSharedMemory<T> {
    pub const unsafe fn new(value: T) -> Self {
        Self(value)
    }
    pub fn into_inner(self) -> T {
        self.0
    }
}

impl<T> std::ops::Deref for AssertPGRXSharedMemory<T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl<T> std::ops::DerefMut for AssertPGRXSharedMemory<T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

unsafe impl<T> PGRXSharedMemory for AssertPGRXSharedMemory<T> {}
