//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use core::ffi::CStr;
use pgrx::prelude::*;
use pgrx::StringInfo;
use serde::{Deserialize, Serialize};
use std::str::FromStr;

#[derive(Copy, Clone, PostgresType, Serialize, Deserialize)]
#[pgvarlena_inoutfuncs]
pub struct VarlenaType {
    a: f32,
    b: f32,
    c: i64,
}

impl PgVarlenaInOutFuncs for VarlenaType {
    fn input(input: &CStr) -> PgVarlena<Self> where {
        let mut iter = input.to_str().unwrap().split(',');
        let (a, b, c) = (iter.next(), iter.next(), iter.next());

        let mut result = PgVarlena::<VarlenaType>::new();
        result.a = f32::from_str(a.unwrap()).expect("a is not a valid f32");
        result.b = f32::from_str(b.unwrap()).expect("b is not a valid f32");
        result.c = i64::from_str(c.unwrap()).expect("c is not a valid i64");
        result
    }

    fn output(&self, buffer: &mut StringInfo) {
        buffer.push_str(&format!("{},{},{}", self.a, self.b, self.c))
    }
}

#[derive(Copy, Clone, PostgresType, Serialize, Deserialize)]
#[pgvarlena_inoutfuncs]
pub enum VarlenaEnumType {
    A,
    B,
}

impl PgVarlenaInOutFuncs for VarlenaEnumType {
    fn input(input: &CStr) -> PgVarlena<Self> where {
        let mut result = PgVarlena::<Self>::new();
        let s = input.to_str().unwrap();
        match s {
            "A" => {
                *result = VarlenaEnumType::A;
            }
            "B" => {
                *result = VarlenaEnumType::B;
            }
            _ => panic!("unexpected input"),
        }
        result
    }

    fn output(&self, buffer: &mut StringInfo) {
        let s = match self {
            VarlenaEnumType::A => "A",
            VarlenaEnumType::B => "B ",
        };
        buffer.push_str(s)
    }
}

#[derive(Serialize, Deserialize, PostgresType)]
#[inoutfuncs]
pub struct CustomTextFormatSerializedType {
    a: f32,
    b: f32,
    c: i64,
}

impl InOutFuncs for CustomTextFormatSerializedType {
    fn input(input: &CStr) -> Self {
        let mut iter = input.to_str().unwrap().split(',');
        let (a, b, c) = (iter.next(), iter.next(), iter.next());

        CustomTextFormatSerializedType {
            a: f32::from_str(a.unwrap()).expect("a is not a valid f32"),
            b: f32::from_str(b.unwrap()).expect("b is not a valid f32"),
            c: i64::from_str(c.unwrap()).expect("c is not a valid i64"),
        }
    }

    fn output(&self, buffer: &mut StringInfo) {
        buffer.push_str(&format!("{},{},{}", self.a, self.b, self.c))
    }
}

#[pg_extern(immutable)]
fn fn_takes_option(input: Option<CustomTextFormatSerializedType>) -> String {
    input.map_or("nothing".to_string(), |c| {
        let mut b = StringInfo::new();
        c.output(&mut b);
        b.to_string()
    })
}

#[derive(Serialize, Deserialize, PostgresType)]
#[inoutfuncs]
pub enum CustomTextFormatSerializedEnumType {
    A,
    B,
}

impl InOutFuncs for CustomTextFormatSerializedEnumType {
    fn input(input: &CStr) -> Self {
        let s = input.to_str().unwrap();
        match s {
            "A" => CustomTextFormatSerializedEnumType::A,
            "B" => CustomTextFormatSerializedEnumType::B,
            _ => panic!("unexpected input"),
        }
    }

    fn output(&self, buffer: &mut StringInfo) {
        let s = match self {
            CustomTextFormatSerializedEnumType::A => "A",
            CustomTextFormatSerializedEnumType::B => "B",
        };
        buffer.push_str(s)
    }
}

#[pg_extern(immutable)]
fn fn_takes_option_enum(input: Option<CustomTextFormatSerializedEnumType>) -> String {
    input.map_or("nothing".to_string(), |c| {
        let mut b = StringInfo::new();
        c.output(&mut b);
        b.to_string()
    })
}

#[derive(Serialize, Deserialize, PostgresType)]
pub struct JsonType {
    a: f32,
    b: f32,
    c: i64,
}

#[derive(Serialize, Deserialize, PostgresType)]
#[serde(tag = "type")]
pub enum JsonEnumType {
    E1 { a: f32 },
    E2 { b: f32 },
}

#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;

    use super::{
        CustomTextFormatSerializedEnumType, CustomTextFormatSerializedType, JsonEnumType, JsonType,
        VarlenaEnumType, VarlenaType,
    };
    use pgrx::datum::PgVarlena;
    use pgrx::prelude::*;

    #[pg_test]
    fn test_mytype() -> Result<(), pgrx::spi::Error> {
        let result =
            Spi::get_one::<PgVarlena<VarlenaType>>("SELECT '1.0,2.0,3'::VarlenaType")?.unwrap();
        assert_eq!(result.a, 1.0);
        assert_eq!(result.b, 2.0);
        assert_eq!(result.c, 3);
        Ok(())
    }

    #[pg_test]
    fn test_my_enum_type() -> Result<(), pgrx::spi::Error> {
        let result =
            Spi::get_one::<PgVarlena<VarlenaEnumType>>("SELECT 'B'::VarlenaEnumType")?.unwrap();
        assert!(matches!(*result, VarlenaEnumType::B));
        Ok(())
    }

    #[pg_test]
    fn test_call_with_value() {
        let result = Spi::get_one::<String>(
            "SELECT fn_takes_option('1.0,2.0,3'::CustomTextFormatSerializedType);",
        );
        assert_eq!(Ok(Some("1,2,3".into())), result);
    }

    #[pg_test]
    fn test_call_with_null() {
        let result = Spi::get_one::<String>("SELECT fn_takes_option(NULL);");
        assert_eq!(Ok(Some("nothing".into())), result);
    }

    #[pg_test]
    fn test_serializedtype() -> Result<(), pgrx::spi::Error> {
        let result = Spi::get_one::<CustomTextFormatSerializedType>(
            "SELECT '1.0,2.0,3'::CustomTextFormatSerializedType",
        )?
        .unwrap();
        assert_eq!(result.a, 1.0);
        assert_eq!(result.b, 2.0);
        assert_eq!(result.c, 3);
        Ok(())
    }

    #[pg_test]
    fn test_call_with_enum_value() {
        let result = Spi::get_one::<String>(
            "SELECT fn_takes_option_enum('A'::CustomTextFormatSerializedEnumType);",
        );
        assert_eq!(Ok(Some("A".into())), result);
    }

    #[pg_test]
    fn test_call_with_enum_null() {
        let result = Spi::get_one::<String>("SELECT fn_takes_option_enum(NULL);");
        assert_eq!(Ok(Some("nothing".into())), result);
    }

    #[pg_test]
    fn test_serialized_enum_type() -> Result<(), pgrx::spi::Error> {
        let result = Spi::get_one::<CustomTextFormatSerializedEnumType>(
            "SELECT 'B'::CustomTextFormatSerializedEnumType",
        )?
        .unwrap();

        assert!(matches!(result, CustomTextFormatSerializedEnumType::B));
        Ok(())
    }

    #[pg_test]
    fn test_jsontype() -> Result<(), pgrx::spi::Error> {
        let result =
            Spi::get_one::<JsonType>(r#"SELECT '{"a": 1.0, "b": 2.0, "c": 3}'::JsonType"#)?
                .unwrap();
        assert_eq!(result.a, 1.0);
        assert_eq!(result.b, 2.0);
        assert_eq!(result.c, 3);
        Ok(())
    }

    #[pg_test]
    fn test_json_enum_type() -> Result<(), pgrx::spi::Error> {
        let result =
            Spi::get_one::<JsonEnumType>(r#"SELECT '{"type": "E1", "a": 1.0}'::JsonEnumType"#)?
                .unwrap();
        assert!(matches!(result, JsonEnumType::E1 { a } if a == 1.0));
        Ok(())
    }
}
