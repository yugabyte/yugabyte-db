//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;
    use std::error::Error;
    use std::ffi::CStr;

    use pgrx::prelude::*;

    #[pg_test]
    fn test_incompatible_datum_returns_error() {
        let result = unsafe {
            String::try_from_datum(
                pg_sys::Datum::from(false),
                true,
                pg_sys::BuiltinOid::BOOLOID.value(),
            )
        };
        assert!(result.is_err());
        assert_eq!("Postgres type boolean (Oid(16)) is not compatible with the Rust type alloc::string::String (Oid(25))", result.unwrap_err().to_string());
    }

    #[pg_extern]
    fn cstring_roundtrip(s: &CStr) -> &CStr {
        s
    }

    #[pg_test]
    fn test_cstring_roundtrip() -> Result<(), Box<dyn Error>> {
        let cstr = Spi::get_one::<&CStr>("SELECT tests.cstring_roundtrip('hello')")?
            .expect("SPI result was NULL");
        let expected = c"hello";
        assert_eq!(cstr, expected);
        Ok(())
    }

    #[pg_test]
    fn null_string_is_none() {
        let val: pg_sys::Datum = pg_sys::Datum::null();
        let is_null: bool = true;

        unsafe {
            if let Some(_) = String::from_datum(val, is_null) {
                // <- this seg fault
                panic!("converted a null Datum into a valid string")
            }
        }
    }
}
