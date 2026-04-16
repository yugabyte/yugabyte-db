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
use core::fmt::{Debug, Display, Formatter};
use std::cmp::Ordering;
use std::fmt;
use std::iter::Sum;

use crate::datum::numeric_support::convert::{from_primitive_helper, FromPrimitiveFunc};
pub use crate::datum::numeric_support::error::Error;
use crate::{direct_function_call, pg_sys};

/// A wrapper around the Postgres SQL `NUMERIC(P, S)` type.  Its `Precision` and `Scale` values
/// are known at compile-time to assist with scale conversions and general type safety.
///
/// PostgreSQL permits the scale in a numeric type declaration to be any value in the range
/// -1000 to 1000. However, the SQL standard requires the scale to be in the range 0 to precision.
/// Using scales outside that range may not be portable to other database systems.
///
/// `pgrx`, by using a `u32` for the scale, restricts it to be in line with the SQL standard.  While
/// it is possible to specify value larger than 1000, ultimately Postgres will reject such values.
///
/// All the various Rust arithmetic traits are implemented for [`AnyNumeric`], but using them, even
/// if the (P, S) is the same on both sides of the operator converts the result to an [`AnyNumeric`].
/// It is [`AnyNumeric`] that also supports the various "Assign" (ie, [`AddAssign`][core::ops::AddAssign]) traits.
///
/// [`Numeric`] is well-suited for a `#[pg_extern]` return type, more so than a function argument.
/// Use [`AnyNumeric`] for those.
#[derive(Debug, Clone)]
#[repr(transparent)]
pub struct Numeric<const P: u32, const S: u32>(pub(crate) AnyNumeric);

/// A plain Postgres SQL `NUMERIC` with default precision and scale values.  This is a sufficient type
/// to represent any Rust primitive value from `i128::MIN` to `u128::MAX` and anything in between.
///
/// Generally, this is the type you'll want to use as function arguments.
///
#[derive(Debug, Clone)]
pub struct AnyNumeric {
    // we represent a NUMERIC as opaque bytes -- we never inspect these ourselves, only a pointer
    // to them (cast as a [`Datum`]) are passed to Postgres
    pub(super) inner: Box<[u8]>,
}

impl Display for AnyNumeric {
    fn fmt(&self, fmt: &mut Formatter<'_>) -> fmt::Result {
        let numeric_out = unsafe {
            direct_function_call::<&CStr>(pg_sys::numeric_out, &[self.as_datum()]).unwrap()
        };
        let s = numeric_out.to_str().expect("numeric_out is not a valid UTF8 string");
        fmt.pad(s)
    }
}

impl<const P: u32, const S: u32> Display for Numeric<P, S> {
    fn fmt(&self, fmt: &mut Formatter<'_>) -> Result<(), fmt::Error> {
        write!(fmt, "{}", self.0)
    }
}

impl Default for AnyNumeric {
    fn default() -> Self {
        0.into()
    }
}

impl Sum for AnyNumeric {
    fn sum<I: Iterator<Item = Self>>(mut iter: I) -> Self {
        let mut sum = iter.next().unwrap_or_default();
        for n in iter {
            sum += n;
        }
        sum
    }
}

#[inline(always)]
pub(crate) const fn make_typmod(precision: u32, scale: u32) -> i32 {
    let (precision, scale) = (precision as i32, scale as i32);
    match (precision, scale) {
        (0, 0) => -1,
        (p, s) => ((p << 16) | s) + pg_sys::VARHDRSZ as i32,
    }
}

/// Which way does a [`AnyNumeric`] sign face?
#[derive(Debug, PartialEq, Eq, Hash)]
pub enum Sign {
    Negative,
    Positive,
    Zero,
    NaN,
}

impl AnyNumeric {
    /// Returns the sign of this [`AnyNumeric`]
    pub fn sign(&self) -> Sign {
        if self.is_nan() {
            Sign::NaN
        } else {
            match self.cmp(&0.into()) {
                Ordering::Less => Sign::Negative,
                Ordering::Greater => Sign::Positive,
                Ordering::Equal => Sign::Zero,
            }
        }
    }

    /// Is this [`AnyNumeric`] not-a-number?
    pub fn is_nan(&self) -> bool {
        unsafe { pg_sys::numeric_is_nan(self.as_ptr() as *mut _) }
    }

    /// The absolute value of this [`AnyNumeric`]
    pub fn abs(&self) -> Self {
        unsafe { direct_function_call(pg_sys::numeric_abs, &[self.as_datum()]).unwrap() }
    }

    /// Compute the logarithm of this [`AnyNumeric`] in the given base
    pub fn log(&self, base: AnyNumeric) -> Self {
        unsafe {
            direct_function_call(pg_sys::numeric_log, &[self.as_datum(), base.as_datum()]).unwrap()
        }
    }

    /// Raise `e` to the power of `x`
    pub fn exp(x: &AnyNumeric) -> Self {
        unsafe { direct_function_call(pg_sys::numeric_exp, &[x.as_datum()]).unwrap() }
    }

    /// Compute the square root of this [`AnyNumeric`]
    pub fn sqrt(&self) -> Self {
        unsafe { direct_function_call(pg_sys::numeric_sqrt, &[self.as_datum()]).unwrap() }
    }

    /// Return the smallest integer greater than or equal to this [`AnyNumeric`]
    pub fn ceil(&self) -> Self {
        unsafe { direct_function_call(pg_sys::numeric_ceil, &[self.as_datum()]).unwrap() }
    }

    /// Return the largest integer equal to or less than this [`AnyNumeric`]
    pub fn floor(&self) -> Self {
        unsafe { direct_function_call(pg_sys::numeric_floor, &[self.as_datum()]).unwrap() }
    }

    /// Calculate the greatest common divisor of this an another [`AnyNumeric`]
    pub fn gcd(&self, n: &AnyNumeric) -> AnyNumeric {
        unsafe {
            direct_function_call(pg_sys::numeric_gcd, &[self.as_datum(), n.as_datum()]).unwrap()
        }
    }

    /// Output function for numeric data type, suppressing insignificant trailing
    /// zeroes and then any trailing decimal point.  The intent of this is to
    /// produce strings that are equal if and only if the input numeric values
    /// compare equal.
    pub fn normalize(&self) -> &str {
        unsafe {
            let s = pg_sys::numeric_normalize(self.as_ptr() as *mut _);
            let cstr = CStr::from_ptr(s);
            let normalized = cstr.to_str().unwrap();
            normalized
        }
    }

    /// Consume this [`AnyNumeric`] and apply a `Precision` and `Scale`.
    ///
    /// Returns an [`Error`] if this [`AnyNumeric`] doesn't fit within the new constraints.
    ///
    /// ## Examples
    ///
    /// ```rust,no_run
    /// use pgrx::{AnyNumeric, Numeric};
    /// let start = AnyNumeric::try_from(42.42).unwrap();
    /// let rescaled:f32 = start.rescale::<3, 1>().unwrap().try_into().unwrap();
    /// assert_eq!(rescaled, 42.4);
    /// ```
    #[inline]
    pub fn rescale<const P: u32, const S: u32>(self) -> Result<Numeric<P, S>, Error> {
        Numeric::try_from(self)
    }

    #[inline]
    pub(crate) fn as_datum(&self) -> Option<pg_sys::Datum> {
        Some(pg_sys::Datum::from(self.inner.as_ptr()))
    }

    #[inline]
    fn as_ptr(&self) -> *const pg_sys::NumericData {
        self.inner.as_ptr().cast()
    }
}

impl<const P: u32, const S: u32> Numeric<P, S> {
    /// Borrow this [`AnyNumeric`] as the more generic [`AnyNumeric`]
    #[inline]
    pub fn as_anynumeric(&self) -> &AnyNumeric {
        &self.0
    }

    /// Consume this [`AnyNumeric`] and a new `Precision` and `Scale`.
    ///
    /// Returns an [`Error`] if this [`AnyNumeric`] doesn't fit within the new constraints.
    ///
    /// ## Examples
    ///
    /// ```rust,no_run
    /// use pgrx::{AnyNumeric, Numeric};
    /// use pgrx_pg_sys::float4;
    /// let start = AnyNumeric::try_from(42.42).unwrap();
    /// let rescaled:float4 = start.rescale::<3, 1>().unwrap().try_into().unwrap();
    /// assert_eq!(rescaled, 42.4);
    /// ```
    #[inline]
    pub fn rescale<const NEW_P: u32, const NEW_S: u32>(
        self,
    ) -> Result<Numeric<NEW_P, NEW_S>, Error> {
        from_primitive_helper::<_, NEW_P, NEW_S>(self, FromPrimitiveFunc::Numeric)
    }
}
