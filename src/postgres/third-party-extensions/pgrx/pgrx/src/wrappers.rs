//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
//! Provides safe wrapper functions around some of Postgres' useful functions.
use crate::{direct_function_call, pg_sys, IntoDatum};

/// A helper function for Postgres' `regtypein` function to lookup a type by a specific name
///
/// Returns the `oid` of the specified type name.  Will panic if Postgres can't find the type
pub fn regtypein(type_name: &str) -> pg_sys::Oid {
    let cstr =
        alloc::ffi::CString::new(type_name).expect("specified type_name has embedded NULL byte");
    unsafe {
        direct_function_call::<pg_sys::Oid>(pg_sys::regtypein, &[cstr.as_c_str().into_datum()])
            .expect("type lookup returned NULL")
    }
}

/// A helper function for Postgres' `regtypein` function to lookup a type using the name of a Rust type
///
/// We truncate the type name to its last value, unless its a primitive type.
///
/// Returns the `oid` of the specified type name.  Will panic if Postgres can't find the type
pub fn rust_regtypein<T>() -> pg_sys::Oid {
    let type_name = std::any::type_name::<T>();

    // pluck out the last part of the type name
    let idx = match type_name.rfind("::") {
        Some(idx) => idx + 2,
        None => 0,
    };

    let type_name = &type_name[idx..];
    regtypein(type_name)
}
