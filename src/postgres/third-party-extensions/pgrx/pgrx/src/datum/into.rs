//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
//! for converting primitive types into Datums
//!
//! Primitive types can never be null, so we do a direct
//! cast of the primitive type to pg_sys::Datum

use crate::{pg_sys, rust_regtypein, set_varsize_4b, PgBox, PgOid, WhoAllocated};
use core::fmt::Display;
use pgrx_pg_sys::panic::ErrorReportable;
use std::{
    any::Any,
    ffi::{CStr, CString},
    ptr::addr_of_mut,
    str,
};

/// Convert a Rust type into a `pg_sys::Datum`.
///
/// Default implementations are provided for the common Rust types.
///
/// If implementing this, also implement `FromDatum` for the reverse
/// conversion.
///
/// Note that any conversions that need to allocate memory (ie, for a `varlena *` representation
/// of a Rust type, that memory **must** be allocated within a [`PgMemoryContexts`](crate::PgMemoryContexts).
pub trait IntoDatum {
    fn into_datum(self) -> Option<pg_sys::Datum>;
    fn type_oid() -> pg_sys::Oid;

    fn composite_type_oid(&self) -> Option<pg_sys::Oid> {
        None
    }

    /// Is a Datum of this type compatible with another Postgres type?
    ///
    /// An example of this are the Postgres `text` and `varchar` types, which are both
    /// technically compatible from a Rust type perspective.  They're both represented in Rust as
    /// `String` (or `&str`), but the underlying Postgres types are different.
    ///
    /// If implementing this yourself, you likely want to follow a pattern like this:
    ///
    /// ```rust,no_run
    /// # use pgrx::*;
    /// # #[repr(transparent)]
    /// # struct FooType(String);
    /// # impl pgrx::IntoDatum for FooType {
    ///     fn is_compatible_with(other: pg_sys::Oid) -> bool {
    ///         // first, if our type is the other type, then we're compatible
    ///         Self::type_oid() == other
    ///
    ///         // and here's the other type we're compatible with
    ///         || other == pg_sys::VARCHAROID
    ///     }
    ///
    /// #    fn into_datum(self) -> Option<pg_sys::Datum> {
    /// #        todo!()
    /// #    }
    /// #
    /// #    fn type_oid() -> pg_sys::Oid {
    /// #        pg_sys::TEXTOID
    /// #    }
    /// # }
    /// ```
    #[inline]
    fn is_compatible_with(other: pg_sys::Oid) -> bool {
        Self::type_oid() == other
    }
}

/// for supporting NULL as the None value of an `Option<T>`
impl<T> IntoDatum for Option<T>
where
    T: IntoDatum,
{
    fn into_datum(self) -> Option<pg_sys::Datum> {
        match self {
            Some(t) => t.into_datum(),
            None => None,
        }
    }

    fn type_oid() -> pg_sys::Oid {
        T::type_oid()
    }
}

impl<T, E> IntoDatum for Result<T, E>
where
    T: IntoDatum,
    E: Any + Display,
{
    /// Returns The `Option<pg_sys::Datum>` representation of this Result's `Ok` variant.
    ///
    /// ## Panics
    ///
    /// If this Result represents an error, then that error is raised as a Postgres ERROR, using
    /// the [`ERRCODE_DATA_EXCEPTION`] error code.
    ///
    /// If we detect that the `Err()` variant contains [ErrorReport], then we
    /// directly raise that as the error.  This enables users to set a specific "sql error code"
    /// for a returned error, along with providing the HINT and DETAIL lines of the error.
    ///
    /// [ErrorReport]: pg_sys::panic::ErrorReport
    /// [`ERRCODE_DATA_EXCEPTION`]: pg_sys::errcodes::PgSqlErrorCode::ERRCODE_DATA_EXCEPTION
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        self.unwrap_or_report().into_datum()
    }

    #[inline]
    fn type_oid() -> pg_sys::Oid {
        T::type_oid()
    }
}

/// for bool
impl IntoDatum for bool {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        Some(pg_sys::Datum::from(if self { 1 } else { 0 }))
    }

    fn type_oid() -> pg_sys::Oid {
        pg_sys::BOOLOID
    }
}

/// for "char"
impl IntoDatum for i8 {
    fn into_datum(self) -> Option<pg_sys::Datum> {
        Some(pg_sys::Datum::from(self))
    }

    fn type_oid() -> pg_sys::Oid {
        pg_sys::CHAROID
    }
}

/// for smallint
impl IntoDatum for i16 {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        Some(pg_sys::Datum::from(self))
    }

    fn type_oid() -> pg_sys::Oid {
        pg_sys::INT2OID
    }

    fn is_compatible_with(other: pg_sys::Oid) -> bool {
        Self::type_oid() == other || i8::type_oid() == other
    }
}

/// for integer
impl IntoDatum for i32 {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        Some(pg_sys::Datum::from(self))
    }

    fn type_oid() -> pg_sys::Oid {
        pg_sys::INT4OID
    }

    fn is_compatible_with(other: pg_sys::Oid) -> bool {
        Self::type_oid() == other || i8::type_oid() == other || i16::type_oid() == other
    }
}

/// for bigint
impl IntoDatum for i64 {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        Some(pg_sys::Datum::from(self))
    }

    fn type_oid() -> pg_sys::Oid {
        pg_sys::INT8OID
    }

    fn is_compatible_with(other: pg_sys::Oid) -> bool {
        Self::type_oid() == other
            || i8::type_oid() == other
            || i16::type_oid() == other
            || i32::type_oid() == other
            || i64::type_oid() == other
    }
}

/// for real
impl IntoDatum for f32 {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        Some(self.to_bits().into())
    }

    fn type_oid() -> pg_sys::Oid {
        pg_sys::FLOAT4OID
    }
}

/// for double precision
impl IntoDatum for f64 {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        Some(self.to_bits().into())
    }

    fn type_oid() -> pg_sys::Oid {
        pg_sys::FLOAT8OID
    }
}

impl IntoDatum for pg_sys::Oid {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        if self == pg_sys::Oid::INVALID {
            None
        } else {
            Some(pg_sys::Datum::from(self.to_u32()))
        }
    }

    #[inline]
    fn type_oid() -> pg_sys::Oid {
        pg_sys::OIDOID
    }
}

impl IntoDatum for pg_sys::TransactionId {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        if self == Self::INVALID {
            None
        } else {
            Some(self.into())
        }
    }

    #[inline]
    fn type_oid() -> pg_sys::Oid {
        pg_sys::XIDOID
    }
}

impl IntoDatum for PgOid {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        match self {
            PgOid::Invalid => None,
            oid => Some(oid.value().into()),
        }
    }

    fn type_oid() -> pg_sys::Oid {
        pg_sys::OIDOID
    }
}

// for text, varchar
macro_rules! impl_into_datum_str {
    ($t:ty) => {
        impl IntoDatum for $t {
            #[inline]
            fn into_datum(self) -> Option<$crate::pg_sys::Datum> {
                self.as_bytes().into_datum()
            }

            #[inline]
            fn type_oid() -> pg_sys::Oid {
                pg_sys::TEXTOID
            }

            #[inline]
            fn is_compatible_with(other: pg_sys::Oid) -> bool {
                Self::type_oid() == other || other == pg_sys::VARCHAROID
            }
        }
    };
}

impl_into_datum_str!(String);
impl_into_datum_str!(&String);
impl_into_datum_str!(&str);

impl IntoDatum for char {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        let mut buf = [0; 4];
        self.encode_utf8(&mut buf);
        unsafe {
            // SAFETY: The buffer contains only valid UTF8 data
            // coming from the encode_utf8 method used above.
            let len = self.len_utf8();
            str::from_utf8_unchecked(&buf[..len]).into_datum()
        }
    }

    fn type_oid() -> pg_sys::Oid {
        pg_sys::VARCHAROID
    }

    #[inline]
    fn is_compatible_with(other: pg_sys::Oid) -> bool {
        Self::type_oid() == other || other == pg_sys::VARCHAROID
    }
}

// for cstring
macro_rules! impl_into_datum_c_str {
    ($t:ty) => {
        impl IntoDatum for $t {
            #[inline]
            fn into_datum(self) -> Option<$crate::pg_sys::Datum> {
                unsafe {
                    // SAFETY:  A `CStr` has already been validated to be a non-null pointer to a null-terminated
                    // "char *", and it won't ever overlap with a newly palloc'd block of memory. Using
                    // `to_bytes_with_nul()` ensures that we'll never try to palloc zero bytes -- it'll at
                    // least always be 1 byte to hold the null terminator for the empty string.
                    //
                    // This is akin to Postgres' `pg_sys::pstrdup` or even `pg_sys::pnstrdup` functions, but
                    // doing the copy ourselves allows us to elide the "strlen" or "strnlen" operations those
                    // functions need to do; the byte slice returned from `to_bytes_with_nul` knows its length.
                    let src = self.as_ref().to_bytes_with_nul();
                    let dst = $crate::pg_sys::palloc(src.len());
                    dst.copy_from(src.as_ptr().cast(), src.len());
                    Some(dst.into())
                }
            }

            #[inline]
            fn type_oid() -> pg_sys::Oid {
                pg_sys::CSTRINGOID
            }
        }
    };
}

impl_into_datum_c_str!(CString);
impl_into_datum_c_str!(&CString);
impl_into_datum_c_str!(&CStr);

/// for bytea
impl<'a> IntoDatum for &'a [u8] {
    /// # Panics
    ///
    /// This function will panic if the string being converted to a datum
    //  is longer than 1 GiB including 4 bytes used for a header.
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        let len = self.len().saturating_add(pg_sys::VARHDRSZ);
        assert!(len < (u32::MAX as usize >> 2));
        unsafe {
            // SAFETY:  palloc gives us a valid pointer and if there's not enough memory it'll raise an error
            let varlena = pg_sys::palloc(len) as *mut pg_sys::varlena;

            // SAFETY: `varlena` can properly cast into a `varattrib_4b` and all of what it contains is properly
            // allocated thanks to our call to `palloc` above
            let varattrib_4b: *mut _ =
                &mut varlena.cast::<pg_sys::varattrib_4b>().as_mut().unwrap_unchecked().va_4byte;

            // This is the same as Postgres' `#define SET_VARSIZE_4B` (which have over in
            // `pgrx/src/varlena.rs`), however we're asserting above that the input string
            // isn't too big for a Postgres varlena, since it's limited to 32 bits and,
            // in reality, it's a quarter that length, but this is good enough
            set_varsize_4b(varlena, len as i32);

            // SAFETY: src and dest pointers are valid, exactly `self.len()` bytes long,
            // and the `dest` was freshly allocated, thus non-overlapping
            std::ptr::copy_nonoverlapping(
                self.as_ptr(),
                // YB: To avoid DANGEROUS_IMPLICIT_AUTOREFS lint check error with rust >= 1.89.0
                addr_of_mut!((&mut (*varattrib_4b)).va_data).cast::<u8>(),
                self.len(),
            );

            Some(pg_sys::Datum::from(varlena))
        }
    }

    #[inline]
    fn type_oid() -> pg_sys::Oid {
        pg_sys::BYTEAOID
    }
}

impl IntoDatum for Vec<u8> {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        (&self[..]).into_datum()
    }

    #[inline]
    fn type_oid() -> pg_sys::Oid {
        pg_sys::BYTEAOID
    }
}

/// for VOID
impl IntoDatum for () {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        // VOID isn't very useful, but Postgres represents it as a non-null Datum with a zero value
        Some(pg_sys::Datum::from(0))
    }

    fn type_oid() -> pg_sys::Oid {
        pg_sys::VOIDOID
    }
}

/// for user types
impl<T, AllocatedBy: WhoAllocated> IntoDatum for PgBox<T, AllocatedBy> {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        if self.is_null() {
            None
        } else {
            Some(self.into_pg().into())
        }
    }

    fn type_oid() -> pg_sys::Oid {
        rust_regtypein::<T>()
    }
}

impl IntoDatum for pg_sys::Datum {
    fn into_datum(self) -> Option<pg_sys::Datum> {
        Some(self)
    }

    fn type_oid() -> pg_sys::Oid {
        pg_sys::INT8OID
    }
}
