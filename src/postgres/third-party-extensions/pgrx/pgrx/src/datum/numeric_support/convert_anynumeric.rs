//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
//! Conversion implementations for from a thing into [AnyNumeric]
use core::ffi::CStr;
use core::str::FromStr;

use pg_sys::AsPgCStr;

use super::call_numeric_func;
use super::convert::{from_primitive_helper, FromPrimitiveFunc};
use super::error::Error;
use crate::{pg_sys, AnyNumeric, IntoDatum, Numeric};

impl<const P: u32, const S: u32> From<Numeric<P, S>> for AnyNumeric {
    #[inline]
    fn from(n: Numeric<P, S>) -> Self {
        n.0
    }
}

macro_rules! anynumeric_from_signed {
    ($ty:ty, $as_:ty, $func:path) => {
        impl From<$ty> for AnyNumeric {
            #[inline]
            fn from(value: $ty) -> Self {
                call_numeric_func($func.into(), &[(value as $as_).into_datum()])
            }
        }
    };
}

macro_rules! anynumeric_from_oversized_primitive {
    ($ty:ty, $signed:ty) => {
        impl From<$ty> for AnyNumeric {
            #[inline]
            fn from(value: $ty) -> Self {
                match <$signed>::try_from(value) {
                    Ok(value) => AnyNumeric::from(value),
                    Err(_) => AnyNumeric::try_from(value.to_string().as_str()).unwrap(),
                }
            }
        }
    };
}

anynumeric_from_signed!(isize, i64, FromPrimitiveFunc::Int8Numeric);
anynumeric_from_signed!(i64, i64, FromPrimitiveFunc::Int8Numeric);
anynumeric_from_signed!(i32, i32, FromPrimitiveFunc::Int4Numeric);
anynumeric_from_signed!(i16, i16, FromPrimitiveFunc::Int2Numeric);
anynumeric_from_signed!(i8, i16, FromPrimitiveFunc::Int2Numeric);

anynumeric_from_oversized_primitive!(usize, i64);
anynumeric_from_oversized_primitive!(u64, i64);
anynumeric_from_oversized_primitive!(u32, i32);
anynumeric_from_oversized_primitive!(u16, i16);
anynumeric_from_oversized_primitive!(u8, i8);

anynumeric_from_oversized_primitive!(i128, i64);
anynumeric_from_oversized_primitive!(u128, i64);

macro_rules! anynumeric_from_float {
    ($ty:ty, $func:path) => {
        impl TryFrom<$ty> for AnyNumeric {
            type Error = Error;

            #[inline]
            fn try_from(value: $ty) -> Result<Self, Self::Error> {
                // these versions of Postgres can't represent +/-Infinity as a NUMERIC
                // so we run through a PgTryBuilder to ask Postgres to do the conversion which will
                // simply return the proper Error
                #[cfg(feature = "pg13")]
                {
                    if value.is_infinite() {
                        return from_primitive_helper::<_, 0, 0>(value, $func).map(|n| n.into());
                    }
                }

                Ok(call_numeric_func($func.into(), &[value.into_datum()]))
            }
        }
    };
}

anynumeric_from_float!(f32, FromPrimitiveFunc::Float4Numeric);
anynumeric_from_float!(f64, FromPrimitiveFunc::Float8Numeric);

impl TryFrom<&CStr> for AnyNumeric {
    type Error = Error;

    #[inline]
    fn try_from(value: &CStr) -> Result<Self, Self::Error> {
        AnyNumeric::from_str(value.to_string_lossy().as_ref())
    }
}

impl TryFrom<&str> for AnyNumeric {
    type Error = Error;

    #[inline]
    fn try_from(value: &str) -> Result<Self, Self::Error> {
        AnyNumeric::from_str(value)
    }
}

impl FromStr for AnyNumeric {
    type Err = Error;

    #[inline]
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        unsafe {
            let ptr = s.as_pg_cstr();
            let cstr = CStr::from_ptr(ptr);
            let numeric = from_primitive_helper::<_, 0, 0>(cstr, FromPrimitiveFunc::NumericIn)
                .map(|n| n.into());
            pg_sys::pfree(ptr.cast());
            numeric
        }
    }
}
