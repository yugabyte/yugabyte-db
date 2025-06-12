//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::pg_sys;

#[inline]
pub fn xid_to_64bit(xid: pg_sys::TransactionId) -> u64 {
    let full_xid = unsafe { pg_sys::ReadNextFullTransactionId() };

    let last_xid = full_xid.value as u32;
    let epoch = (full_xid.value >> 32) as u32;

    convert_xid_common(xid, last_xid.into(), epoch)
}

#[inline]
fn convert_xid_common(
    xid: pg_sys::TransactionId,
    last_xid: pgrx_pg_sys::TransactionId,
    epoch: u32,
) -> u64 {
    /* return special xid's as-is */
    if !pg_sys::TransactionIdIsNormal(xid) {
        return xid.into_inner() as u64;
    }

    /* xid can be on either side when near wrap-around */
    let mut epoch = epoch as u64;
    if xid > last_xid && unsafe { pg_sys::TransactionIdPrecedes(xid, last_xid) } {
        epoch -= 1;
    } else if xid < last_xid && unsafe { pg_sys::TransactionIdFollows(xid, last_xid) } {
        epoch += 1;
    }

    (epoch << 32) | xid.into_inner() as u64
}
