//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::{direct_function_call_as_datum, pg_sys, AnyNumeric, FromDatum};

pub mod cmp;
pub mod convert;
pub(super) mod convert_anynumeric;
pub(super) mod convert_numeric;
pub(super) mod convert_primitive;
pub mod datum;
pub mod error;
pub mod hash;
pub mod ops;
pub mod serde;
pub mod sql;

#[inline]
pub(super) fn call_numeric_func(
    func: unsafe fn(pg_sys::FunctionCallInfo) -> pg_sys::Datum,
    args: &[Option<pg_sys::Datum>],
) -> AnyNumeric {
    unsafe {
        // SAFETY: this call to direct_function_call_as_datum will never return None
        let numeric_datum = direct_function_call_as_datum(func, args).unwrap_unchecked();

        // we asked Postgres to create this numeric so it'll need to be freed after we copy it
        let anynumeric = AnyNumeric::from_datum(numeric_datum, false);
        pg_sys::pfree(numeric_datum.cast_mut_ptr());

        anynumeric.unwrap()
    }
}
