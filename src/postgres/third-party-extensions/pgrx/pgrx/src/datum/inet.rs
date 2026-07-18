//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::{direct_function_call, direct_function_call_as_datum, pg_sys, FromDatum, IntoDatum};
use core::ffi::CStr;
use pgrx_pg_sys::errcodes::PgSqlErrorCode;
use pgrx_pg_sys::PgTryBuilder;
use pgrx_sql_entity_graph::metadata::{
    ArgumentError, Returns, ReturnsError, SqlMapping, SqlTranslatable,
};
use serde::de::{Error, Visitor};
use serde::{Deserialize, Deserializer, Serialize, Serializer};
use std::fmt;
use std::ops::Deref;

/// An `inet` type from PostgreSQL
#[derive(Debug, Clone, Ord, PartialOrd, Eq, PartialEq)]
pub struct Inet(pub String);

impl Deref for Inet {
    type Target = str;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl Serialize for Inet {
    fn serialize<S>(&self, serializer: S) -> Result<<S as Serializer>::Ok, <S as Serializer>::Error>
    where
        S: Serializer,
    {
        serializer.serialize_str(&self.0)
    }
}

impl<'de> Deserialize<'de> for Inet {
    fn deserialize<D>(deserializer: D) -> Result<Self, <D as Deserializer<'de>>::Error>
    where
        D: Deserializer<'de>,
    {
        struct InetVisitor;
        impl<'de> Visitor<'de> for InetVisitor {
            type Value = Inet;

            fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
                formatter.write_str("a quoted JSON string in proper inet form")
            }

            fn visit_str<E>(self, v: &str) -> Result<Self::Value, E>
            where
                E: Error,
            {
                self.visit_string(v.to_owned())
            }

            fn visit_string<E>(self, v: String) -> Result<Self::Value, E>
            where
                E: Error,
            {
                // try to convert the provided String value into a Postgres Inet Datum
                // if it doesn't raise an conversion error, then we're good
                PgTryBuilder::new(|| {
                    // this might throw, but that's okay
                    let datum = Inet(v.clone()).into_datum().unwrap();

                    unsafe {
                        // and don't leak the 'inet' datum Postgres created
                        pg_sys::pfree(datum.cast_mut_ptr());
                    }

                    // we have it as a valid String
                    Ok(Inet(v.clone()))
                })
                .catch_when(PgSqlErrorCode::ERRCODE_INVALID_TEXT_REPRESENTATION, |_| {
                    Err(Error::custom(format!("invalid inet value: {v}")))
                })
                .execute()
            }
        }

        deserializer.deserialize_str(InetVisitor)
    }
}

impl FromDatum for Inet {
    unsafe fn from_polymorphic_datum(
        datum: pg_sys::Datum,
        is_null: bool,
        _typoid: pg_sys::Oid,
    ) -> Option<Inet> {
        if is_null {
            None
        } else {
            let cstr = direct_function_call::<&CStr>(pg_sys::inet_out, &[Some(datum)]);
            Some(Inet(
                cstr.unwrap().to_str().expect("unable to convert &cstr inet into &str").to_owned(),
            ))
        }
    }
}

impl IntoDatum for Inet {
    fn into_datum(self) -> Option<pg_sys::Datum> {
        let cstr = alloc::ffi::CString::new(self.0).expect("failed to convert inet into CString");
        unsafe { direct_function_call_as_datum(pg_sys::inet_in, &[cstr.as_c_str().into_datum()]) }
    }

    fn type_oid() -> pg_sys::Oid {
        pg_sys::INETOID
    }
}

impl From<String> for Inet {
    fn from(val: String) -> Self {
        Inet(val)
    }
}

unsafe impl SqlTranslatable for Inet {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("inet"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("inet")))
    }
}
