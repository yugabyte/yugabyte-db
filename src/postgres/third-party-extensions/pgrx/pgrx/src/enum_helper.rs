//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
//! Helper functions for working with Postgres `enum` types

use crate::pg_sys::GETSTRUCT;
use crate::{ereport, pg_sys, PgLogLevel, PgSqlErrorCode};

pub fn lookup_enum_by_oid(enumval: pg_sys::Oid) -> (String, pg_sys::Oid, f32) {
    let tup = unsafe {
        pg_sys::SearchSysCache(
            pg_sys::SysCacheIdentifier::ENUMOID as i32,
            pg_sys::Datum::from(enumval),
            pg_sys::Datum::from(0),
            pg_sys::Datum::from(0),
            pg_sys::Datum::from(0),
        )
    };
    if tup.is_null() {
        ereport!(
            PgLogLevel::ERROR,
            PgSqlErrorCode::ERRCODE_INVALID_BINARY_REPRESENTATION,
            format!("invalid internal value for enum: {enumval:?}")
        );
    }

    let en = unsafe { GETSTRUCT(tup) } as pg_sys::Form_pg_enum;
    let en = unsafe { en.as_ref() }.unwrap();
    let result = (
        unsafe {
            core::ffi::CStr::from_ptr(en.enumlabel.data.as_ptr() as *const std::os::raw::c_char)
        }
        .to_str()
        .unwrap()
        .to_string(),
        en.enumtypid,
        en.enumsortorder as f32,
    );

    unsafe {
        pg_sys::ReleaseSysCache(tup);
    }

    result
}

pub fn lookup_enum_by_label(typname: &str, label: &str) -> pg_sys::Datum {
    let enumtypoid = crate::regtypein(typname);

    if enumtypoid == pg_sys::InvalidOid {
        panic!("could not locate type oid for type: {typname}");
    }

    let tup = unsafe {
        let label =
            alloc::ffi::CString::new(label).expect("failed to convert enum typname to a CString");
        pg_sys::SearchSysCache(
            pg_sys::SysCacheIdentifier::ENUMTYPOIDNAME as i32,
            pg_sys::Datum::from(enumtypoid),
            pg_sys::Datum::from(label.as_ptr()),
            pg_sys::Datum::from(0usize),
            pg_sys::Datum::from(0usize),
        )
    };

    if tup.is_null() {
        panic!("could not find heap tuple for enum: {typname}.{label}, typoid={enumtypoid:?}");
    }

    // SAFETY:  we know that `tup` is valid because we just got it from Postgres above
    unsafe {
        let oid = extract_enum_oid(tup);
        pg_sys::ReleaseSysCache(tup);
        pg_sys::Datum::from(oid)
    }
}

unsafe fn extract_enum_oid(tup: *mut pg_sys::HeapTupleData) -> pg_sys::Oid {
    let en = {
        // SAFETY:  the caller has assured us that `tup` is a valid HeapTupleData pointer
        GETSTRUCT(tup) as pg_sys::Form_pg_enum
    };
    let en = en.as_ref().unwrap();
    en.oid
}
