//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use pgrx_pg_sys::errcodes::PgSqlErrorCode;
use pgrx_pg_sys::panic::CaughtError;
use pgrx_pg_sys::PgTryBuilder;

use super::error::Error;
use crate::datum::numeric::make_typmod;
use crate::datum::{AnyNumeric, FromDatum, IntoDatum, Numeric};
use crate::{direct_function_call, direct_function_call_as_datum, pg_sys};

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub(crate) enum FromPrimitiveFunc {
    NumericIn,
    Numeric,
    Float4Numeric,
    Float8Numeric,
    Int2Numeric,
    Int4Numeric,
    Int8Numeric,
}

impl From<FromPrimitiveFunc> for unsafe fn(pg_sys::FunctionCallInfo) -> pg_sys::Datum {
    fn from(value: FromPrimitiveFunc) -> Self {
        match value {
            FromPrimitiveFunc::NumericIn => pg_sys::numeric_in,
            FromPrimitiveFunc::Numeric => pg_sys::numeric,
            FromPrimitiveFunc::Float4Numeric => pg_sys::float4_numeric,
            FromPrimitiveFunc::Float8Numeric => pg_sys::float8_numeric,
            FromPrimitiveFunc::Int2Numeric => pg_sys::int2_numeric,
            FromPrimitiveFunc::Int4Numeric => pg_sys::int4_numeric,
            FromPrimitiveFunc::Int8Numeric => pg_sys::int8_numeric,
        }
    }
}

pub(crate) fn from_primitive_helper<I: IntoDatum, const P: u32, const S: u32>(
    value: I,
    func: FromPrimitiveFunc,
) -> Result<Numeric<P, S>, Error> {
    let datum = value.into_datum();
    let materialize_numeric_datum = move || unsafe {
        if func == FromPrimitiveFunc::NumericIn {
            debug_assert_eq!(I::type_oid(), pg_sys::CSTRINGOID);
            direct_function_call(
                pg_sys::numeric_in,
                &[datum, pg_sys::InvalidOid.into_datum(), make_typmod(P, S).into_datum()],
            )
        } else if func == FromPrimitiveFunc::Numeric {
            debug_assert_eq!(I::type_oid(), pg_sys::NUMERICOID);
            direct_function_call(pg_sys::numeric, &[datum, make_typmod(P, S).into_datum()])
        } else {
            debug_assert!(matches!(
                func,
                FromPrimitiveFunc::Float4Numeric
                    | FromPrimitiveFunc::Float8Numeric
                    | FromPrimitiveFunc::Int2Numeric
                    | FromPrimitiveFunc::Int4Numeric
                    | FromPrimitiveFunc::Int8Numeric
            ));
            // use the user-provided `func` to make a Numeric from some primitive type
            let numeric_datum = direct_function_call_as_datum(func.into(), &[datum]);

            if P != 0 || S != 0 {
                // and if it has a precision or a scale, try to coerce it into those constraints
                direct_function_call(
                    pg_sys::numeric,
                    &[numeric_datum, make_typmod(P, S).into_datum()],
                )
            } else {
                numeric_datum
            }
        }
        .unwrap_unchecked()
    };

    PgTryBuilder::new(|| {
        unsafe {
            let datum = materialize_numeric_datum();
            let anynumeric = AnyNumeric::from_datum(datum, false).unwrap();

            // SAFETY:  We asked Postgres to create a new NUMERIC instance, so it now needs to be freed
            // after we've copied it into Rust memory
            pg_sys::pfree(datum.cast_mut_ptr());
            Ok(Numeric(anynumeric))
        }
    })
    .catch_when(PgSqlErrorCode::ERRCODE_INVALID_TEXT_REPRESENTATION, |e| {
        if let CaughtError::PostgresError(ref ereport) = e {
            Err(Error::Invalid(ereport.message().to_string()))
        } else {
            e.rethrow()
        }
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
