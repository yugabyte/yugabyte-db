//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use pgrx::prelude::*;
use pgrx::{PgAtomic, PgLwLock, pg_shmem_init};
use std::sync::atomic::AtomicBool;

#[cfg(feature = "cshim")]
use pgrx::spinlock::PgSpinLock;

static ATOMIC: PgAtomic<AtomicBool> = unsafe { PgAtomic::new(c"pgrx_unit_tests_atomic") };
static LWLOCK: PgLwLock<bool> = unsafe { PgLwLock::new(c"pgrx_unit_tests_lwlock") };

#[cfg(feature = "cshim")]
static SPINLOCK: PgAtomic<PgSpinLock<usize>> =
    unsafe { PgAtomic::new(c"pgrx_unit_tests_spinlock") };

#[pg_guard]
pub extern "C-unwind" fn _PG_init() {
    // This ensures that this functionality works across PostgreSQL versions
    pg_shmem_init!(ATOMIC);
    pg_shmem_init!(LWLOCK);
    #[cfg(feature = "cshim")]
    pg_shmem_init!(SPINLOCK = PgSpinLock::new(0));
}
#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_unit_tests;

    use pgrx::prelude::*;

    #[pg_test]
    #[should_panic(expected = "cache lookup failed for type 0")]
    pub fn test_behaves_normally_when_elog_while_holding_lock() {
        use super::LWLOCK;
        // Hold lock
        let _lock = LWLOCK.exclusive();
        // Call into pg_guarded postgres function which internally reports an error
        unsafe { pg_sys::format_type_extended(pg_sys::InvalidOid, -1, 0) };
    }

    #[pg_test]
    pub fn test_lock_is_released_on_drop() {
        use super::LWLOCK;
        let lock = LWLOCK.exclusive();
        drop(lock);
        let _lock = LWLOCK.exclusive();
    }

    #[pg_test]
    pub fn test_lock_is_released_on_unwind() {
        use super::LWLOCK;
        let _res = std::panic::catch_unwind(|| {
            let _lock = LWLOCK.exclusive();
            panic!("get out")
        });
        let _lock = LWLOCK.exclusive();
    }

    #[cfg(feature = "cshim")]
    #[pg_test]
    pub fn test_spinlock() {
        use super::SPINLOCK;
        for i in 0..10 {
            let mut lock = SPINLOCK.get().lock();
            assert!(*lock == i);
            *lock = i + 1;
            drop(lock);
        }
    }
}
