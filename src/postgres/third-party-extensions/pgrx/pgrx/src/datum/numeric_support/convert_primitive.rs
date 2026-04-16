//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
//! Conversion implementations for converting [`AnyNumeric`] or [`Numeric<P, S>`] into primitives

use super::error::Error;
use crate::datum::{AnyNumeric, FromDatum, Numeric};
use crate::{direct_function_call, pg_sys};
use core::str::FromStr;
use pgrx_pg_sys::errcodes::PgSqlErrorCode;
use pgrx_pg_sys::panic::CaughtError;
use pgrx_pg_sys::PgTryBuilder;

macro_rules! anynumeric_to_primitive {
    ($ty:ty, $as_:ty, $pg_func:ident) => {
        impl TryFrom<AnyNumeric> for $ty {
            type Error = Error;

            #[inline]
            fn try_from(value: AnyNumeric) -> Result<Self, Self::Error> {
                to_primitive_helper::<$as_>(&value, pg_sys::$pg_func).map(|v| v as $ty)
            }
        }
    };
}

anynumeric_to_primitive!(isize, i64, numeric_int8);
anynumeric_to_primitive!(i64, i64, numeric_int8);
anynumeric_to_primitive!(i32, i32, numeric_int4);
anynumeric_to_primitive!(i16, i16, numeric_int2);
anynumeric_to_primitive!(i8, i16, numeric_int2);
anynumeric_to_primitive!(f32, f32, numeric_float4);
anynumeric_to_primitive!(f64, f64, numeric_float8);

macro_rules! anynumeric_to_oversized_primitive {
    ($ty:ty, $as_:ty, $pg_func:ident) => {
        /// Try to convert using the signed equivalent.  If that fails, then try using slower String
        /// conversion
        impl TryFrom<AnyNumeric> for $ty {
            type Error = Error;

            fn try_from(value: AnyNumeric) -> Result<Self, Self::Error> {
                match to_primitive_helper::<$as_>(&value, pg_sys::$pg_func) {
                    Ok(value) => Ok(value as $ty),
                    Err(Error::OutOfRange(_)) => <$ty>::from_str(value.to_string().as_str())
                        .map_err(|e| Error::OutOfRange(format!("{e}"))),
                    Err(e) => Err(e),
                }
            }
        }
    };
}
anynumeric_to_oversized_primitive!(i128, i64, numeric_int8);
anynumeric_to_oversized_primitive!(usize, i64, numeric_int8);
anynumeric_to_oversized_primitive!(u128, i64, numeric_int8);
anynumeric_to_oversized_primitive!(u64, i64, numeric_int8);
anynumeric_to_oversized_primitive!(u32, i32, numeric_int4);
anynumeric_to_oversized_primitive!(u16, i16, numeric_int2);
anynumeric_to_oversized_primitive!(u8, i16, numeric_int2);

macro_rules! numeric_to_primitive {
    ($ty:ty, $as_:ty, $pg_func:ident) => {
        impl<const P: u32, const S: u32> TryFrom<Numeric<P, S>> for $ty {
            type Error = Error;

            #[inline]
            fn try_from(value: Numeric<P, S>) -> Result<Self, Self::Error> {
                to_primitive_helper::<$as_>(&value, pg_sys::$pg_func).map(|v| v as $ty)
            }
        }
    };
}

numeric_to_primitive!(isize, i64, numeric_int8);
numeric_to_primitive!(i64, i64, numeric_int8);
numeric_to_primitive!(i32, i32, numeric_int4);
numeric_to_primitive!(i16, i16, numeric_int2);
numeric_to_primitive!(i8, i16, numeric_int2);
numeric_to_primitive!(f32, f32, numeric_float4);
numeric_to_primitive!(f64, f64, numeric_float8);

macro_rules! numeric_to_oversized_primitive {
    ($ty:ty, $as_:ty, $pg_func:ident) => {
        /// Try to convert using the signed equivalent.  If that fails, then try using slower String
        /// conversion
        impl<const P: u32, const S: u32> TryFrom<Numeric<P, S>> for $ty {
            type Error = Error;

            fn try_from(value: Numeric<P, S>) -> Result<Self, Self::Error> {
                match to_primitive_helper::<$as_>(&value, pg_sys::$pg_func) {
                    Ok(value) => Ok(value as $ty),
                    Err(Error::OutOfRange(_)) => <$ty>::from_str(value.to_string().as_str())
                        .map_err(|e| Error::OutOfRange(format!("{e}"))),
                    Err(e) => Err(e),
                }
            }
        }
    };
}
numeric_to_oversized_primitive!(i128, i64, numeric_int8);
numeric_to_oversized_primitive!(usize, i64, numeric_int8);
numeric_to_oversized_primitive!(u128, i64, numeric_int8);
numeric_to_oversized_primitive!(u64, i64, numeric_int8);
numeric_to_oversized_primitive!(u32, i32, numeric_int4);
numeric_to_oversized_primitive!(u16, i16, numeric_int2);
numeric_to_oversized_primitive!(u8, i16, numeric_int2);

fn to_primitive_helper<T: FromDatum>(
    value: &AnyNumeric,
    func: unsafe fn(pg_sys::FunctionCallInfo) -> pg_sys::Datum,
) -> Result<T, Error> {
    let datum = value.as_datum();
    PgTryBuilder::new(|| unsafe {
        // SAFETY: if 'func' returns, it won't be NULL
        Ok(direct_function_call::<T>(func, &[datum]).unwrap_unchecked())
    })
    .catch_when(PgSqlErrorCode::ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE, |e| {
        if let CaughtError::PostgresError(ref ereport) = e {
            Err(Error::OutOfRange(ereport.message().to_string()))
        } else {
            e.rethrow()
        }
    })
    .catch_when(PgSqlErrorCode::ERRCODE_FEATURE_NOT_SUPPORTED, |e| {
        if let CaughtError::PostgresError(ref ereport) = e {
            Err(Error::ConversionNotSupported(ereport.message().to_string()))
        } else {
            e.rethrow()
        }
    })
    .execute()
}
