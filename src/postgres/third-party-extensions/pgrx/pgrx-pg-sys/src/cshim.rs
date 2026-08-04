#![cfg(feature = "cshim")]
#![allow(deprecated)]

use crate as pg_sys;

#[pgrx_macros::pg_guard]
extern "C-unwind" {
    #[link_name = "SpinLockInit__pgrx_cshim"]
    pub fn SpinLockInit(lock: *mut pg_sys::slock_t);
    #[link_name = "SpinLockAcquire__pgrx_cshim"]
    pub fn SpinLockAcquire(lock: *mut pg_sys::slock_t);
    #[link_name = "SpinLockRelease__pgrx_cshim"]
    pub fn SpinLockRelease(lock: *mut pg_sys::slock_t);
    #[link_name = "SpinLockFree__pgrx_cshim"]
    pub fn SpinLockFree(lock: *mut pg_sys::slock_t) -> bool;
}
