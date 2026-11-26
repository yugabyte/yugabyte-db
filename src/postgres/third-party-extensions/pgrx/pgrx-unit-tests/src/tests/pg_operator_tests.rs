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

const OPERATOR_DOG_COMPOSITE_TYPE: &str = "OperatorDog";

#[pg_schema]
mod pg_catalog {
    use pgrx::{opname, pg_operator};

    use super::OPERATOR_DOG_COMPOSITE_TYPE;

    #[pg_operator]
    #[opname(==>)]
    fn concat_strings(left: String, right: String) -> String {
        left + &right
    }

    #[pg_operator]
    #[opname(@=)]
    fn operator_dog_name_eq(
        left: pgrx::composite_type!(OPERATOR_DOG_COMPOSITE_TYPE),
        right: pgrx::composite_type!(OPERATOR_DOG_COMPOSITE_TYPE),
    ) -> bool {
        let left_name: Option<String> = left.get_by_name("name").ok().flatten();
        let right_name: Option<String> = right.get_by_name("name").ok().flatten();
        left_name == right_name
    }
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_unit_tests;
    use pgrx::prelude::*;

    #[pg_test]
    fn test_correct_schema() {
        let result = Spi::get_one::<String>("SELECT 'hello, ' OPERATOR(pg_catalog.==>) 'world';");
        assert_eq!(result, Ok(Some(String::from("hello, world"))));
    }

    #[pg_test]
    fn test_composite_operator() {
        let result = Spi::get_one::<bool>(
            "SELECT ROW('Nami')::OperatorDog OPERATOR(pg_catalog.@=) ROW('Nami')::OperatorDog;",
        );
        assert_eq!(result, Ok(Some(true)));
    }
}
