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
use std::cell::UnsafeCell;
use std::ffi::CStr;

pub struct PgAtomic<T> {
    name: &'static CStr,
    inner: UnsafeCell<*mut Shared<T>>,
}

unsafe impl<T: PGRXSharedMemory> Sync for PgAtomic<T> {}

impl<T> PgAtomic<T> {
    /// Create a pointer of an atomic that points to nothing.
    ///
    /// # Safety
    ///
    /// * Caller must be confident that there are no name conflicts.
    pub const unsafe fn new(name: &'static CStr) -> Self {
        Self { name, inner: UnsafeCell::new(std::ptr::null_mut()) }
    }

    /// Get the name of the shared memory.
    pub const fn name(&self) -> &'static CStr {
        self.name
    }
}

impl<T: PGRXSharedMemory> PgAtomic<T> {
    /// Obtain the reference (which comes with `&T` access).
    pub fn get(&self) -> &T {
        unsafe {
            let shared = self.inner.get().read().as_ref().expect("PgAtomic was not initialized");
            &shared.data
        }
    }
}

impl<T: PGRXSharedMemory> PgSharedMemoryInitialization for PgAtomic<T> {
    type Value = T;

    unsafe fn on_shmem_request(&'static self) {
        unsafe {
            crate::pg_sys::RequestAddinShmemSpace(size_of::<Shared<T>>());
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
                fv_shmem.write(Shared { data: value });
            }

            *self.inner.get() = fv_shmem;

            pg_sys::LWLockRelease(addin_shmem_init_lock);
        }
    }
}

#[repr(transparent)]
struct Shared<T> {
    data: T,
}
