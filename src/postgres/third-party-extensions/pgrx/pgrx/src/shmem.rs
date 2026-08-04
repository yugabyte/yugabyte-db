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
use crate::lwlock::*;
use crate::{pg_sys, PgAtomic};
use std::cell::UnsafeCell;
use std::hash::Hash;

/// Custom types that want to participate in shared memory must implement this marker trait
pub unsafe trait PGRXSharedMemory {}

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
/// `shared_preload_libraries` configuration setting.  
///
/// # Example
///
/// ```rust,no_run
/// use pgrx::prelude::*;
/// use pgrx::{PgAtomic, PgLwLock, pg_shmem_init, PgSharedMemoryInitialization};
///
/// // primitive types must be protected behind a `PgLwLock`
/// static PRIMITIVE: PgLwLock<i32> = PgLwLock::new(c"primitive");
///
/// // Rust atomics can be used without locks, wrapped in a `PgAtomic`
/// static ATOMIC: PgAtomic<std::sync::atomic::AtomicBool> = PgAtomic::new(c"atomic");
///
/// #[pg_guard]
/// pub extern "C-unwind" fn _PG_init() {
///     pg_shmem_init!(PRIMITIVE);
///     pg_shmem_init!(ATOMIC);
/// }
/// ```
#[cfg(not(any(feature = "pg15", feature = "pg16", feature = "pg17")))]
#[macro_export]
macro_rules! pg_shmem_init {
    ($thing:expr) => {
        $thing.pg_init();

        unsafe {
            static mut PREV_SHMEM_STARTUP_HOOK: Option<unsafe extern "C-unwind" fn()> = None;
            PREV_SHMEM_STARTUP_HOOK = pg_sys::shmem_startup_hook;
            pg_sys::shmem_startup_hook = Some(__pgrx_private_shmem_hook);

            #[pg_guard]
            extern "C-unwind" fn __pgrx_private_shmem_hook() {
                unsafe {
                    if let Some(i) = PREV_SHMEM_STARTUP_HOOK {
                        i();
                    }
                    $thing.shmem_init();
                }
            }
        }
    };
}

#[cfg(any(feature = "pg15", feature = "pg16", feature = "pg17"))]
#[macro_export]
macro_rules! pg_shmem_init {
    ($thing:expr) => {
        unsafe {
            static mut PREV_SHMEM_REQUEST_HOOK: Option<unsafe extern "C-unwind" fn()> = None;
            PREV_SHMEM_REQUEST_HOOK = pg_sys::shmem_request_hook;
            pg_sys::shmem_request_hook = Some(__pgrx_private_shmem_request_hook);

            #[pg_guard]
            extern "C-unwind" fn __pgrx_private_shmem_request_hook() {
                unsafe {
                    if let Some(i) = PREV_SHMEM_REQUEST_HOOK {
                        i();
                    }
                    $thing.pg_init();
                }
            }
        }

        unsafe {
            static mut PREV_SHMEM_STARTUP_HOOK: Option<unsafe extern "C-unwind" fn()> = None;
            PREV_SHMEM_STARTUP_HOOK = pg_sys::shmem_startup_hook;
            pg_sys::shmem_startup_hook = Some(__pgrx_private_shmem_hook);

            #[pg_guard]
            extern "C-unwind" fn __pgrx_private_shmem_hook() {
                unsafe {
                    if let Some(i) = PREV_SHMEM_STARTUP_HOOK {
                        i();
                    }
                    $thing.shmem_init();
                }
            }
        }
    };
}

/// A trait that types can implement to provide their own Postgres Shared Memory initialization process
pub trait PgSharedMemoryInitialization {
    /// Automatically called when the an extension is loaded.  If using the `pg_shmem_init!()` macro
    /// in `_PG_init()`, this is called automatically
    fn pg_init(&'static self);

    /// Automatically called by the `pg_shmem_init!()` macro, when Postgres is initializing its
    /// shared memory system
    /// SAFETY: Must only be called from inside the Postgres shared memory init hook
    unsafe fn shmem_init(&'static self);
}

impl<T> PgSharedMemoryInitialization for PgLwLock<T>
where
    T: Default + PGRXSharedMemory + 'static,
{
    fn pg_init(&'static self) {
        PgSharedMem::pg_init_locked(self);
    }

    /// SAFETY: Must only be called from inside the Postgres shared memory init hook
    unsafe fn shmem_init(&'static self) {
        unsafe {
            PgSharedMem::shmem_init_locked(self);
        }
    }
}

impl<T> PgSharedMemoryInitialization for PgAtomic<T>
where
    T: atomic_traits::Atomic + Default,
{
    fn pg_init(&'static self) {
        PgSharedMem::pg_init_atomic(self);
    }

    /// SAFETY: Must only be called from inside the Postgres shared memory init hook
    unsafe fn shmem_init(&'static self) {
        unsafe {
            PgSharedMem::shmem_init_atomic(self);
        }
    }
}

/// This struct contains methods to drive creation of types in shared memory
pub struct PgSharedMem {}

impl PgSharedMem {
    /// Must be run from PG_init, use for types which are guarded by a LWLock
    pub fn pg_init_locked<T: Default + PGRXSharedMemory>(lock: &PgLwLock<T>) {
        unsafe {
            pg_sys::RequestAddinShmemSpace(std::mem::size_of::<PgLwLockShared<T>>());
            pg_sys::RequestNamedLWLockTranche(lock.name().as_ptr(), 1);
        }
    }

    /// Must be run from _PG_init for atomics
    pub fn pg_init_atomic<T: atomic_traits::Atomic + Default>(_atomic: &PgAtomic<T>) {
        unsafe {
            pg_sys::RequestAddinShmemSpace(std::mem::size_of::<T>());
        }
    }

    /// Must be run from the shared memory init hook, use for types which are guarded by a `LWLock`
    /// SAFETY: Must only be called from inside the Postgres shared memory init hook
    pub unsafe fn shmem_init_locked<T: Default + PGRXSharedMemory>(lock: &PgLwLock<T>) {
        unsafe {
            let shm_name = lock.name();
            let addin_shmem_init_lock = &raw mut (*pg_sys::MainLWLockArray.add(21)).lock;
            pg_sys::LWLockAcquire(addin_shmem_init_lock, pg_sys::LWLockMode::LW_EXCLUSIVE);

            let mut found = false;
            let fv_shmem = pg_sys::ShmemInitStruct(
                shm_name.as_ptr(),
                std::mem::size_of::<PgLwLockShared<T>>(),
                &mut found,
            )
            .cast::<PgLwLockShared<T>>();
            if !found {
                fv_shmem.write(PgLwLockShared {
                    data: UnsafeCell::new(T::default()),
                    lock_ptr: &raw mut (*pg_sys::GetNamedLWLockTranche(shm_name.as_ptr())).lock,
                });
            }

            lock.attach(fv_shmem);
            pg_sys::LWLockRelease(addin_shmem_init_lock);
        }
    }

    /// Must be run from the shared memory init hook, use for rust atomics behind `PgAtomic`
    /// SAFETY: Must only be called from inside the Postgres shared memory init hook
    pub unsafe fn shmem_init_atomic<T: atomic_traits::Atomic + Default>(atomic: &PgAtomic<T>) {
        unsafe {
            let shm_name = atomic.name();
            let addin_shmem_init_lock = &raw mut (*pg_sys::MainLWLockArray.add(21)).lock;
            pg_sys::LWLockAcquire(addin_shmem_init_lock, pg_sys::LWLockMode::LW_EXCLUSIVE);

            let mut found = false;
            let fv_shmem =
                pg_sys::ShmemInitStruct(shm_name.as_ptr(), std::mem::size_of::<T>(), &mut found)
                    .cast::<T>();
            if !found {
                fv_shmem.write(T::default());
            }

            atomic.attach(fv_shmem);
            pg_sys::LWLockRelease(addin_shmem_init_lock);
        }
    }
}

unsafe impl PGRXSharedMemory for bool {}
unsafe impl PGRXSharedMemory for char {}
unsafe impl PGRXSharedMemory for str {}
unsafe impl PGRXSharedMemory for () {}
unsafe impl PGRXSharedMemory for i8 {}
unsafe impl PGRXSharedMemory for i16 {}
unsafe impl PGRXSharedMemory for i32 {}
unsafe impl PGRXSharedMemory for i64 {}
unsafe impl PGRXSharedMemory for i128 {}
unsafe impl PGRXSharedMemory for u8 {}
unsafe impl PGRXSharedMemory for u16 {}
unsafe impl PGRXSharedMemory for u32 {}
unsafe impl PGRXSharedMemory for u64 {}
unsafe impl PGRXSharedMemory for u128 {}
unsafe impl PGRXSharedMemory for usize {}
unsafe impl PGRXSharedMemory for isize {}
unsafe impl PGRXSharedMemory for f32 {}
unsafe impl PGRXSharedMemory for f64 {}
unsafe impl<T> PGRXSharedMemory for [T] where T: PGRXSharedMemory + Default {}
unsafe impl<A, B> PGRXSharedMemory for (A, B)
where
    A: PGRXSharedMemory + Default,
    B: PGRXSharedMemory + Default,
{
}
unsafe impl<A, B, C> PGRXSharedMemory for (A, B, C)
where
    A: PGRXSharedMemory + Default,
    B: PGRXSharedMemory + Default,
    C: PGRXSharedMemory + Default,
{
}
unsafe impl<A, B, C, D> PGRXSharedMemory for (A, B, C, D)
where
    A: PGRXSharedMemory + Default,
    B: PGRXSharedMemory + Default,
    C: PGRXSharedMemory + Default,
    D: PGRXSharedMemory + Default,
{
}
unsafe impl<A, B, C, D, E> PGRXSharedMemory for (A, B, C, D, E)
where
    A: PGRXSharedMemory + Default,
    B: PGRXSharedMemory + Default,
    C: PGRXSharedMemory + Default,
    D: PGRXSharedMemory + Default,
    E: PGRXSharedMemory + Default,
{
}
unsafe impl<T, const N: usize> PGRXSharedMemory for heapless::Vec<T, N> {}
unsafe impl<T, const N: usize> PGRXSharedMemory for heapless::Deque<T, N> {}
unsafe impl<K: Eq + Hash, V: Default, S, const N: usize> PGRXSharedMemory
    for heapless::IndexMap<K, V, S, N>
{
}
