//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
//! Helper functions for working with Postgres `ItemPointerData` (`tid`) type

use crate::datum::{FromDatum, IntoDatum};
use crate::PgMemoryContexts;
use crate::{pg_sys, AllocatedByRust, PgBox};

/// ## Safety
///
/// This function s unsafe because it does not check that the specified ItemPointerData pointer
/// might be null
#[inline]
pub unsafe fn item_pointer_get_block_number(
    ctid: *const pg_sys::ItemPointerData,
) -> pg_sys::BlockNumber {
    assert!(item_pointer_is_valid(ctid));
    item_pointer_get_block_number_no_check(*ctid)
}

/// ## Safety
///
/// This function s unsafe because it does not check that the specified ItemPointerData pointer
/// might be null
#[inline]
pub unsafe fn item_pointer_get_offset_number(
    ctid: *const pg_sys::ItemPointerData,
) -> pg_sys::OffsetNumber {
    assert!(item_pointer_is_valid(ctid));
    item_pointer_get_offset_number_no_check(*ctid)
}

/// ## Safety
///
/// This function is unsafe because it does not check that the specified ItemPointerData pointer
/// might be null
#[inline]
pub unsafe fn item_pointer_get_block_number_no_check(
    ctid: pg_sys::ItemPointerData,
) -> pg_sys::BlockNumber {
    let block_id = ctid.ip_blkid;
    (((block_id.bi_hi as u32) << 16) | (block_id.bi_lo as u32)) as pg_sys::BlockNumber
}

/// ## Safety
///
/// This function is unsafe because it does not check that the specified ItemPointerData pointer
/// might be null
#[inline]
pub unsafe fn item_pointer_get_offset_number_no_check(
    ctid: pg_sys::ItemPointerData,
) -> pg_sys::OffsetNumber {
    ctid.ip_posid
}

#[inline]
pub fn item_pointer_get_both(
    ctid: pg_sys::ItemPointerData,
) -> (pg_sys::BlockNumber, pg_sys::OffsetNumber) {
    unsafe {
        (
            item_pointer_get_block_number_no_check(ctid),
            item_pointer_get_offset_number_no_check(ctid),
        )
    }
}

#[inline]
pub fn item_pointer_set_all(
    tid: &mut pg_sys::ItemPointerData,
    blockno: pg_sys::BlockNumber,
    offno: pg_sys::OffsetNumber,
) {
    tid.ip_posid = offno;
    tid.ip_blkid.bi_hi = (blockno >> 16).try_into().unwrap();
    tid.ip_blkid.bi_lo = (blockno & 0xffff).try_into().unwrap();
}

/// Convert an `ItemPointerData` struct into a `u64`
#[inline]
pub fn item_pointer_to_u64(ctid: pg_sys::ItemPointerData) -> u64 {
    let (blockno, offno) = item_pointer_get_both(ctid);
    let blockno = blockno as u64;
    let offno = offno as u64;

    (blockno << 32) | offno
}

/// Deconstruct a `u64` into an otherwise uninitialized `ItemPointerData` struct
#[inline]
pub fn u64_to_item_pointer(value: u64, tid: &mut pg_sys::ItemPointerData) {
    let blockno = (value >> 32) as pg_sys::BlockNumber;
    let offno = value as pg_sys::OffsetNumber;
    item_pointer_set_all(tid, blockno, offno);
}

#[inline]
pub fn u64_to_item_pointer_parts(value: u64) -> (pg_sys::BlockNumber, pg_sys::OffsetNumber) {
    let blockno = (value >> 32) as pg_sys::BlockNumber;
    let offno = value as pg_sys::OffsetNumber;
    (blockno, offno)
}

#[allow(clippy::not_unsafe_ptr_arg_deref)] // this is okay b/c we guard against ctid being null
#[inline]
pub unsafe fn item_pointer_is_valid(ctid: *const pg_sys::ItemPointerData) -> bool {
    if ctid.is_null() {
        false
    } else {
        (*ctid).ip_posid != pg_sys::InvalidOffsetNumber
    }
}

#[inline]
pub fn new_item_pointer(
    blockno: pg_sys::BlockNumber,
    offno: pg_sys::OffsetNumber,
) -> PgBox<pg_sys::ItemPointerData, AllocatedByRust> {
    let mut tid = unsafe { PgBox::<pg_sys::ItemPointerData>::alloc() };
    tid.ip_blkid.bi_hi = (blockno >> 16) as u16;
    tid.ip_blkid.bi_lo = (blockno & 0xffff) as u16;
    tid.ip_posid = offno;
    tid
}

impl FromDatum for pg_sys::ItemPointerData {
    #[inline]
    unsafe fn from_polymorphic_datum(
        datum: pg_sys::Datum,
        is_null: bool,
        _typoid: pg_sys::Oid,
    ) -> Option<pg_sys::ItemPointerData> {
        if is_null {
            None
        } else {
            let tid = datum.cast_mut_ptr();
            let (blockno, offno) = item_pointer_get_both(*tid);
            let mut tid_copy = pg_sys::ItemPointerData::default();

            item_pointer_set_all(&mut tid_copy, blockno, offno);
            Some(tid_copy)
        }
    }
}

impl IntoDatum for pg_sys::ItemPointerData {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        let tid = self;
        let tid_ptr = unsafe {
            // SAFETY:  CurrentMemoryContext is always valid
            PgMemoryContexts::CurrentMemoryContext.palloc_struct::<pg_sys::ItemPointerData>()
        };
        let (blockno, offno) = item_pointer_get_both(tid);

        item_pointer_set_all(unsafe { &mut *tid_ptr }, blockno, offno);

        Some(tid_ptr.into())
    }

    fn type_oid() -> pg_sys::Oid {
        pg_sys::TIDOID
    }
}
