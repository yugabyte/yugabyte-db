//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
//! Conversion implementations for converting from a thing into [`Numeric<P, S>`]
use core::ffi::CStr;
use core::str::FromStr;

use pgrx_pg_sys::AsPgCStr;

use super::convert::{from_primitive_helper, FromPrimitiveFunc};
use super::error::Error;
use crate::datum::{AnyNumeric, Numeric};
use crate::pg_sys;

impl<const P: u32, const S: u32> TryFrom<AnyNumeric> for Numeric<P, S> {
    type Error = Error;

    #[inline]
    fn try_from(value: AnyNumeric) -> Result<Self, Self::Error> {
        from_primitive_helper::<_, P, S>(value.clone(), FromPrimitiveFunc::Numeric)
    }
}

macro_rules! numeric_try_from_signed {
    ($ty:ty, $as_:ty, $pg_func:path) => {
        impl<const P: u32, const S: u32> TryFrom<$ty> for Numeric<P, S> {
            type Error = Error;

            #[inline]
            fn try_from(value: $ty) -> Result<Self, Self::Error> {
                from_primitive_helper::<_, P, S>((value as $as_), $pg_func)
            }
        }
    };
}

numeric_try_from_signed!(i64, i64, FromPrimitiveFunc::Int8Numeric);
numeric_try_from_signed!(i32, i32, FromPrimitiveFunc::Int4Numeric);
numeric_try_from_signed!(i16, i16, FromPrimitiveFunc::Int2Numeric);
numeric_try_from_signed!(i8, i16, FromPrimitiveFunc::Int2Numeric);

macro_rules! numeric_try_from_oversized_primitive {
    ($ty:ty, $as_:ty, $pg_func:path) => {
        impl<const P: u32, const S: u32> TryFrom<$ty> for Numeric<P, S> {
            type Error = Error;

            #[inline]
            fn try_from(value: $ty) -> Result<Self, Self::Error> {
                match <$as_>::try_from(value) {
                    Ok(value) => from_primitive_helper::<_, P, S>(value, $pg_func),
                    Err(_) => Numeric::from_str(value.to_string().as_str()),
                }
            }
        }
    };
}

numeric_try_from_oversized_primitive!(i128, i64, FromPrimitiveFunc::Int8Numeric);
numeric_try_from_oversized_primitive!(isize, i64, FromPrimitiveFunc::Int8Numeric);

numeric_try_from_oversized_primitive!(u128, i64, FromPrimitiveFunc::Int8Numeric);
numeric_try_from_oversized_primitive!(usize, i64, FromPrimitiveFunc::Int8Numeric);
numeric_try_from_oversized_primitive!(u64, i64, FromPrimitiveFunc::Int8Numeric);
numeric_try_from_oversized_primitive!(u32, i32, FromPrimitiveFunc::Int4Numeric);
numeric_try_from_oversized_primitive!(u16, i16, FromPrimitiveFunc::Int2Numeric);
numeric_try_from_oversized_primitive!(u8, i16, FromPrimitiveFunc::Int2Numeric);

numeric_try_from_oversized_primitive!(f32, f32, FromPrimitiveFunc::Float4Numeric);
numeric_try_from_oversized_primitive!(f64, f64, FromPrimitiveFunc::Float8Numeric);

impl<const P: u32, const S: u32> TryFrom<&CStr> for Numeric<P, S> {
    type Error = Error;

    #[inline]
    fn try_from(value: &CStr) -> Result<Self, Self::Error> {
        Numeric::from_str(value.to_string_lossy().as_ref())
    }
}

impl<const P: u32, const S: u32> TryFrom<&str> for Numeric<P, S> {
    type Error = Error;

    #[inline]
    fn try_from(value: &str) -> Result<Self, Self::Error> {
        Numeric::from_str(value)
    }
}

impl<const P: u32, const S: u32> FromStr for Numeric<P, S> {
    type Err = Error;

    #[inline]
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        unsafe {
            let ptr = s.as_pg_cstr();
            let cstr = CStr::from_ptr(ptr);
            let numeric = from_primitive_helper::<_, P, S>(cstr, FromPrimitiveFunc::NumericIn);
            pg_sys::pfree(ptr.cast());
            numeric
        }
    }
}
