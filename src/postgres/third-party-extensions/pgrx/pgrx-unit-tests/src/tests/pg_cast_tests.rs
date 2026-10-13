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

#[pg_schema]
mod pg_catalog {
    use pgrx::prelude::*;
    use serde::{Deserialize, Serialize};
    use serde_json::Value::Number;

    #[pg_cast(implicit)]
    fn int4_from_json(value: pgrx::Json) -> i32 {
        if let Number(num) = &value.0 {
            if num.is_i64() {
                return num.as_i64().unwrap() as i32;
            } else if num.is_f64() {
                return num.as_f64().unwrap() as i32;
            } else if num.is_u64() {
                return num.as_u64().unwrap() as i32;
            }
        };
        panic!("Error casting json value {} to an integer", value.0)
    }

    #[derive(PostgresType, Serialize, Deserialize)]
    #[pg_binary_protocol]
    struct TestCastType;

    #[pg_cast(implicit, immutable)]
    fn testcasttype_to_bool(_i: TestCastType) -> bool {
        // look, it's just a test, okay?
        true
    }

    // there is a 3-arg version of CAST functions that pass through the user-provided "type modifier"
    // and if it's an explicit cast or not.  This function itself is enough to test that we can generate
    // the proper code for the function
    #[pg_cast]
    fn testcasttype_to_testcasttype(
        i: TestCastType,
        _typmod: i32,
        _is_explicit: bool,
    ) -> TestCastType {
        i
    }

    #[pg_cast]
    fn castdog_to_castcat(
        dog: pgrx::composite_type!("CastDog"),
    ) -> pgrx::composite_type!("CastCat") {
        let name: Option<String> = dog.get_by_name("name").ok().flatten();
        let mut cat = PgHeapTuple::new_composite_type("CastCat").unwrap();
        cat.set_by_name("name", name).unwrap();
        cat
    }
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_unit_tests;
    use pgrx::prelude::*;

    #[pg_test]
    fn test_pg_cast_explicit_type_cast() {
        assert_eq!(
            Spi::get_one::<i32>("SELECT CAST('{\"a\": 1}'::json->'a' AS int4);"),
            Ok(Some(1))
        );
        assert_eq!(Spi::get_one::<i32>("SELECT ('{\"a\": 1}'::json->'a')::int4;"), Ok(Some(1)));
    }

    #[pg_test]
    fn test_pg_cast_assignment_type_cast() {
        let _ = Spi::connect_mut(|client| {
            client.update("CREATE TABLE test_table(value int4);", None, &[])?;
            client.update("INSERT INTO test_table VALUES('{\"a\": 1}'::json->'a');", None, &[])?;

            Ok::<_, spi::Error>(())
        });
        assert_eq!(Spi::get_one::<i32>("SELECT value FROM test_table"), Ok(Some(1)));
    }

    #[pg_test]
    fn test_pg_cast_implicit_type_cast() {
        assert_eq!(Spi::get_one::<i32>("SELECT 1 + ('{\"a\": 1}'::json->'a')"), Ok(Some(2)));
    }

    #[pg_test]
    fn assert_cast_func_is_immutable() {
        let is_immutable = Spi::get_one::<bool>(
            "SELECT provolatile = 'i' FROM pg_proc WHERE proname = 'testcasttype_to_bool';",
        );
        assert_eq!(is_immutable, Ok(Some(true)));
    }

    #[pg_test]
    fn assert_cast_is_implicit() {
        let is_immutable = Spi::get_one::<bool>(
            "SELECT castcontext = 'i' FROM pg_cast WHERE castsource = 'TestCastType'::regtype AND casttarget = 'bool'::regtype;",
        );
        assert_eq!(is_immutable, Ok(Some(true)));
    }

    #[pg_test]
    fn assert_composite_cast_exists() {
        let cast_exists = Spi::get_one::<bool>(
            "SELECT EXISTS (\
                SELECT 1 \
                  FROM pg_cast \
                 WHERE castsource = 'CastDog'::regtype \
                   AND casttarget = 'CastCat'::regtype\
            );",
        );
        assert_eq!(cast_exists, Ok(Some(true)));
    }
}
