//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use pgrx::nullable::Nullable;
use pgrx::prelude::*;
use pgrx::StringInfo;
use serde::{Deserialize, Serialize};

#[pg_extern]
fn add_two_numbers(a: i32, b: i32) -> i32 {
    a + b
}

#[pg_extern]
fn takes_i16(i: i16) -> i16 {
    i
}

#[pg_extern]
fn takes_i32(i: i32) -> i32 {
    i
}

#[pg_extern]
fn takes_i64(i: i32) -> i32 {
    i
}

#[pg_extern]
fn takes_bool(i: bool) -> bool {
    i
}

#[pg_extern]
fn takes_f32(i: f32) -> f32 {
    i
}

#[pg_extern]
fn takes_f64(i: f64) -> f64 {
    i
}

#[pg_extern]
fn takes_i8(i: i8) -> i8 {
    i
}

#[pg_extern]
fn takes_char(i: char) -> char {
    i
}

#[pg_extern]
fn takes_option(i: Option<i32>) -> i32 {
    i.unwrap_or(-1)
}

#[pg_extern]
fn takes_str(s: &str) -> &str {
    s
}

#[pg_extern]
fn takes_string(s: String) -> String {
    s
}

#[pg_extern]
fn returns_some() -> Option<i32> {
    Some(42)
}

#[pg_extern]
fn returns_none() -> Option<i32> {
    None
}

#[pg_extern]
fn returns_null() -> Nullable<i32> {
    Nullable::Null
}

#[pg_extern]
fn passes_null(null: Nullable<i32>) -> Nullable<i32> {
    null
}

#[pg_extern]
fn returns_void() {
    // noop
}

#[pg_extern]
fn returns_tuple() -> TableIterator<'static, (name!(id, i32), name!(title, String))> {
    TableIterator::once((42, "pgrx".into()))
}

#[pg_extern]
fn same_name(same_name: &str) -> &str {
    same_name
}

// Tests for regression of https://github.com/pgcentralfoundation/pgrx/issues/432
#[pg_extern]
fn fcinfo_renamed_one_arg(
    _x: PgBox<pg_sys::IndexAmRoutine>,
    _fcinfo: pg_sys::FunctionCallInfo,
) -> PgBox<pg_sys::IndexAmRoutine> {
    todo!()
}

#[pg_extern]
fn fcinfo_renamed_no_arg(_fcinfo: pg_sys::FunctionCallInfo) -> i32 {
    todo!()
}

#[pg_extern]
fn fcinfo_not_named_one_arg(
    _x: PgBox<pg_sys::IndexAmRoutine>,
    fcinfo: pg_sys::FunctionCallInfo,
) -> PgBox<pg_sys::IndexAmRoutine> {
    let _fcinfo = fcinfo;
    todo!()
}

#[pg_extern]
fn fcinfo_not_named_no_arg(fcinfo: pg_sys::FunctionCallInfo) -> i32 {
    let _fcinfo = fcinfo;
    todo!()
}

#[derive(PostgresType, Serialize, Deserialize, Debug, PartialEq)]
#[inoutfuncs]
pub struct NullStrict {}

impl InOutFuncs for NullStrict {
    fn input(_input: &core::ffi::CStr) -> Self
    where
        Self: Sized,
    {
        NullStrict {}
    }

    fn output(&self, _buffer: &mut StringInfo) {}
    // doesn't define a NULL_ERROR_MESSAGE
}

#[derive(PostgresType, Serialize, Deserialize, Debug, PartialEq)]
#[inoutfuncs]
pub struct NullError {}

impl InOutFuncs for NullError {
    fn input(_input: &core::ffi::CStr) -> Self
    where
        Self: Sized,
    {
        NullError {}
    }

    fn output(&self, _buffer: &mut StringInfo) {}

    const NULL_ERROR_MESSAGE: Option<&'static str> = Some("An error message");
}

#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;

    use super::{same_name, NullError, NullStrict};
    use pgrx::prelude::*;
    use pgrx::{direct_pg_extern_function_call, IntoDatum};

    #[test]
    fn make_idea_happy() {
        assert_eq!(5, 5);
    }

    #[pg_test]
    fn test_add_two_numbers() {
        assert_eq!(super::add_two_numbers(2, 3), 5);
    }

    #[pg_test]
    unsafe fn test_takes_i16() {
        let input = 42i16;
        let result =
            direct_pg_extern_function_call::<i16>(super::takes_i16_wrapper, &[input.into_datum()]);
        let result = result.expect("result is NULL");
        assert_eq!(result, input);
    }

    #[pg_test]
    unsafe fn test_takes_i32() {
        let input = 42i32;
        let result =
            direct_pg_extern_function_call::<i32>(super::takes_i32_wrapper, &[input.into_datum()]);
        let result = result.expect("result is NULL");
        assert_eq!(result, input);
    }

    #[pg_test]
    unsafe fn test_takes_i64() {
        let input = 42i64;
        let result =
            direct_pg_extern_function_call::<i64>(super::takes_i64_wrapper, &[input.into_datum()]);
        let result = result.expect("result is NULL");
        assert_eq!(result, input);
    }

    #[pg_test]
    unsafe fn test_takes_bool() {
        let input = true;
        let result = direct_pg_extern_function_call::<bool>(
            super::takes_bool_wrapper,
            &[input.into_datum()],
        );
        let result = result.expect("result is NULL");
        assert_eq!(result, input);
    }

    #[pg_test]
    unsafe fn test_takes_f32() {
        let input = 42.424_244;
        let result =
            direct_pg_extern_function_call::<f32>(super::takes_f32_wrapper, &[input.into_datum()]);
        let result = result.expect("result is NULL");
        assert!(result.eq(&input));
    }

    #[pg_test]
    unsafe fn test_takes_f64() {
        let input = 42.424_242_424_242f64;
        let result =
            direct_pg_extern_function_call::<f64>(super::takes_f64_wrapper, &[input.into_datum()]);
        let result = result.expect("result is NULL");
        assert!(result.eq(&input));
    }

    #[pg_test]
    unsafe fn test_takes_i8() {
        let result = Spi::get_one::<i8>("SELECT takes_i8('a');");
        assert_eq!(result, Ok(Some('a' as i8)));
    }

    #[pg_test]
    unsafe fn test_takes_char() {
        let result = Spi::get_one::<char>("SELECT takes_char('🚨');");
        assert_eq!(result, Ok(Some('🚨')));
    }

    #[pg_test]
    unsafe fn test_takes_option_with_null_arg() {
        let result = direct_pg_extern_function_call::<i32>(super::takes_option_wrapper, &[None]);
        assert_eq!(-1, result.expect("result is NULL"))
    }

    #[pg_test]
    unsafe fn test_takes_option_with_non_null_arg() {
        let input = 42i32;
        let result = direct_pg_extern_function_call::<i32>(
            super::takes_option_wrapper,
            &[input.into_datum()],
        );
        let result = result.expect("result is NULL");
        assert_eq!(result, input);
    }

    #[pg_test]
    unsafe fn test_takes_str() {
        let input = "this is a test";
        let result =
            direct_pg_extern_function_call::<&str>(super::takes_str_wrapper, &[input.into_datum()]);
        let result = result.expect("result is NULL");
        assert_eq!(result, input);
    }

    #[pg_test]
    unsafe fn test_takes_string() {
        let input = "this is a test".to_string();
        let result = direct_pg_extern_function_call::<String>(
            super::takes_str_wrapper,
            &[input.clone().into_datum()],
        );
        let result = result.expect("result is NULL");
        assert_eq!(result, input);
    }

    #[pg_test]
    unsafe fn test_returns_some() {
        let result = direct_pg_extern_function_call::<i32>(super::returns_some_wrapper, &[]);
        assert!(result.is_some());
    }

    #[pg_test]
    unsafe fn test_returns_none() {
        let result = direct_pg_extern_function_call::<i32>(super::returns_none_wrapper, &[]);
        assert!(result.is_none())
    }

    #[pg_test]
    fn test_returns_void() {
        let result = Spi::get_one::<()>("SELECT returns_void();");
        assert_eq!(result, Ok(Some(())));
    }

    #[pg_test]
    fn test_returns_tuple() {
        let result = Spi::get_two::<i32, String>("SELECT * FROM returns_tuple();");
        assert_eq!(Ok((Some(42), Some("pgrx".into()))), result);
    }

    /// ensures that we can have a `#[pg_extern]` function with an argument that
    /// shares its name
    #[pg_test]
    fn test_same_name() {
        assert_eq!("test", same_name("test"));
    }

    #[pg_test]
    fn test_null_strict_type() {
        assert_eq!(Ok(None), Spi::get_one::<NullStrict>("SELECT null::NullStrict"));
    }

    #[pg_test]
    #[should_panic(expected = "An error message")]
    fn test_null_error_type() {
        Spi::get_one::<NullError>("SELECT null::NullError").unwrap();
    }
}
