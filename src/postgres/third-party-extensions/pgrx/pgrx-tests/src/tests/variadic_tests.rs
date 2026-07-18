//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
#[pgrx::pg_schema]
mod test {
    use pgrx::prelude::*;
    use pgrx::VariadicArray;

    #[pg_extern]
    fn func_with_variadic_array_args<'dat>(
        _field: &str,
        values: VariadicArray<'dat, &'dat str>,
    ) -> String {
        values.get(0).unwrap().unwrap().to_string()
    }
}

#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;

    use pgrx::prelude::*;

    #[pg_test]
    fn test_func_with_variadic_array_args() {
        let result = Spi::get_one::<String>(
            "SELECT test.func_with_variadic_array_args('test', 'a', 'b', 'c');",
        );
        assert_eq!(result, Ok(Some("a".into())));
    }
}
