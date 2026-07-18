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
use pgrx::{direct_function_call, AnyArray, Json};

#[pg_extern]
fn anyarray_arg(array: AnyArray) -> Json {
    unsafe { direct_function_call::<Json>(pg_sys::array_to_json, &[array.into_datum()]) }
        .expect("conversion to json returned null")
}

#[pg_extern]
fn anyarray_iter_arg(array: AnyArray) -> Json {
    let mut vec = Vec::<_>::new();
    for el in &array {
        vec.push(el.expect("element is null").datum().value() as i64)
    }
    unsafe { direct_function_call::<Json>(pg_sys::array_to_json, &[vec.into_datum()]) }
        .expect("conversion to json returned null")
}

#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;

    use pgrx::prelude::*;
    use pgrx::Json;
    use serde_json::*;

    #[pg_test]
    fn test_anyarray_arg() -> std::result::Result<(), pgrx::spi::Error> {
        let json = Spi::get_one::<Json>("SELECT anyarray_arg(ARRAY[1::integer,2,3]::integer[]);")?
            .expect("datum was null");
        assert_eq!(json.0, json! {[1,2,3]});
        Ok(())
    }

    #[pg_test]
    fn test_anyarray_iter_arg() -> std::result::Result<(), pgrx::spi::Error> {
        let json =
            Spi::get_one::<Json>("SELECT anyarray_iter_arg(ARRAY[1::integer,2,3]::integer[]);")?
                .expect("datum was null");
        assert_eq!(json.0, json! {[1,2,3]});
        Ok(())
    }
}
