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

#[pg_extern(immutable)]
fn returns_tuple_with_attributes(
) -> TableIterator<'static, (name!(arg, String), name!(arg2, String))> {
    TableIterator::once(("hi".to_string(), "bye".to_string()))
}

// Check we can map a `fdw_handler`
#[pg_extern]
fn fdw_handler_return() -> PgBox<pg_sys::FdwRoutine> {
    unimplemented!("Not a functional test, just a signature test for SQL generation. Feel free to make a functional test!")
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;

    use pgrx::prelude::*;

    // exists just to make sure the code compiles -- tickles the bug behind PR #807
    #[pg_extern]
    fn returns_named_tuple_with_rust_reserved_keyword(/*
                                 `type` is a reserved Rust keyword, but we still need to be able to parse it for SQL generation 
                                                        */
    ) -> TableIterator<'static, (name!(type, String), name!(i, i32))> {
        unimplemented!()
    }

    #[pg_extern(immutable)]
    fn is_immutable() {}

    #[pg_test]
    fn test_immutable() {
        let result = Spi::get_one::<bool>(
            "SELECT provolatile = 'i' FROM pg_proc WHERE proname = 'is_immutable'",
        );
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_extern(security_invoker)]
    fn is_invoker() {}

    #[pg_test]
    fn test_security_invoker() {
        let result = Spi::get_one::<bool>(
            "SELECT prosecdef = 'f' FROM pg_proc WHERE proname = 'is_invoker'",
        );
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_extern(security_definer)]
    fn is_definer() {}

    #[pg_test]
    fn test_security_definer() {
        let result = Spi::get_one::<bool>(
            "SELECT prosecdef = 't' FROM pg_proc WHERE proname = 'is_definer'",
        );
        assert_eq!(result, Ok(Some(true)));
    }

    // Ensures `@MODULE_PATHNAME@` and `@FUNCTION_NAME@` are handled.
    #[pg_extern(sql = r#"
        CREATE FUNCTION tests."overridden_sql_with_fn_name"() RETURNS boolean
        STRICT
        LANGUAGE c /* Rust */
        AS '@MODULE_PATHNAME@', '@FUNCTION_NAME@';
    "#)]
    fn overridden_sql_with_fn_name() -> bool {
        true
    }

    #[pg_test]
    fn test_overridden_sql_with_fn_name() {
        let result = Spi::get_one::<bool>(r#"SELECT tests."overridden_sql_with_fn_name"()"#);
        assert_eq!(result, Ok(Some(true)));
    }

    // Manually define the function first here. Note that it returns false
    #[pg_extern(sql = r#"
        CREATE FUNCTION tests."create_or_replace_method"() RETURNS bool
        STRICT
        LANGUAGE c /* Rust */
        AS '@MODULE_PATHNAME@', '@FUNCTION_NAME@';
    "#)]
    fn create_or_replace_method_first() -> bool {
        false
    }

    // Replace the "create_or_replace_method" function using pg_extern[create_or_replace]
    // Note that it returns true, and that we use a "requires = []" here to ensure
    // that there is an order of creation.
    #[pg_extern(create_or_replace, requires = [create_or_replace_method_first])]
    fn create_or_replace_method() -> bool {
        true
    }

    // This will test to make sure that a function is created if it doesn't exist
    // while using pg_extern[create_or_replace]
    #[pg_extern(create_or_replace)]
    fn create_or_replace_method_other() -> i32 {
        42
    }

    #[pg_test]
    fn test_create_or_replace() {
        let replace_result = Spi::get_one::<bool>(r#"SELECT tests."create_or_replace_method"()"#);
        assert_eq!(replace_result, Ok(Some(true)));

        let create_result =
            Spi::get_one::<i32>(r#"SELECT tests."create_or_replace_method_other"()"#);
        assert_eq!(create_result, Ok(Some(42)));
    }

    #[pg_extern]
    fn anyele_type(x: pgrx::datum::AnyElement) -> pg_sys::Oid {
        x.oid()
    }

    #[pg_test]
    fn test_anyele_type() {
        let interval_type =
            Spi::get_one::<pg_sys::Oid>(r#"SELECT tests."anyele_type"('5 hours'::interval)"#);
        assert_eq!(interval_type, Ok(Some(pg_sys::INTERVALOID)));
    }

    #[pg_extern(name = "custom_name")]
    fn fn_custom() -> bool {
        true
    }

    #[pg_test]
    fn test_name() {
        let result = Spi::get_one::<bool>(r#"SELECT tests."custom_name"()"#);
        assert_eq!(result, Ok(Some(true)));
    }
}
