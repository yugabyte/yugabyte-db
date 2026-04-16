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
use std::cell::UnsafeCell;
use std::ffi::CStr;

pub struct PgAtomic<T> {
    name: &'static CStr,
    inner: UnsafeCell<*mut T>,
}

impl<T> PgAtomic<T> {
    pub const fn new(name: &'static CStr) -> Self {
        Self { name, inner: UnsafeCell::new(std::ptr::null_mut()) }
    }

    pub fn name(&self) -> &'static CStr {
        self.name
    }
}

impl<T> PgAtomic<T>
where
    T: atomic_traits::Atomic + Default,
{
    /// SAFETY: Must only be called from inside the Postgres shared memory init hook
    pub unsafe fn attach(&self, value: *mut T) {
        unsafe {
            *self.inner.get() = value;
        }
    }

    pub fn get(&self) -> &T {
        unsafe {
            let shared = self.inner.get().read().as_ref().expect("PgAtomic was not initialized");
            shared
        }
    }
}

unsafe impl<T> Send for PgAtomic<T> where T: atomic_traits::Atomic + Default {}
unsafe impl<T> Sync for PgAtomic<T> where T: atomic_traits::Atomic + Default {}
