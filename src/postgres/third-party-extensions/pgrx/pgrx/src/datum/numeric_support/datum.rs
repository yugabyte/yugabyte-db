//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::{pg_sys, varsize_any, AnyNumeric, FromDatum, IntoDatum, Numeric};

impl FromDatum for AnyNumeric {
    #[inline]
    unsafe fn from_polymorphic_datum(
        datum: pg_sys::Datum,
        is_null: bool,
        _typoid: pg_sys::Oid,
    ) -> Option<Self>
    where
        Self: Sized,
    {
        if is_null {
            None
        } else {
            // Going back to Postgres v9.1, `pg_sys::NumericData` is really a "varlena" in disguise.
            //
            // We want to copy it out of the Postgres-allocated memory it's in and into something
            // managed by Rust.
            //
            // First we'll detoast it and then copy the backing varlena bytes into a `Vec<u8>`.
            // This is what we'll use later if we need to convert back into a postgres-allocated Datum
            // or to provide a view over the bytes as a Datum.  The latter is what most of the AnyNumeric
            // support functions use, via the `as_datum()` function.

            // detoast
            let numeric = pg_sys::pg_detoast_datum(datum.cast_mut_ptr());
            let is_copy = !std::ptr::eq(
                numeric.cast::<pg_sys::varlena>(),
                datum.cast_mut_ptr::<pg_sys::varlena>(),
            );

            // copy us into a rust-owned/allocated Box<[u8]>
            let size = varsize_any(numeric);
            let slice = std::slice::from_raw_parts(numeric.cast::<u8>(), size);
            let boxed: Box<[u8]> = slice.into();

            // free the copy detoast might have made
            if is_copy {
                pg_sys::pfree(numeric.cast());
            }

            Some(AnyNumeric { inner: boxed })
        }
    }
}

impl IntoDatum for AnyNumeric {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        unsafe {
            let size = self.inner.len();
            let src = self.inner.as_ptr();
            let dest = pg_sys::palloc(size).cast();
            std::ptr::copy_nonoverlapping(src, dest, size);
            Some(pg_sys::Datum::from(dest))
        }
    }

    #[inline]
    fn type_oid() -> pg_sys::Oid {
        pg_sys::NUMERICOID
    }
}

impl<const P: u32, const S: u32> FromDatum for Numeric<P, S> {
    #[inline]
    unsafe fn from_polymorphic_datum(
        datum: pg_sys::Datum,
        is_null: bool,
        typoid: pg_sys::Oid,
    ) -> Option<Self>
    where
        Self: Sized,
    {
        if is_null {
            None
        } else {
            Some(Numeric(AnyNumeric::from_polymorphic_datum(datum, false, typoid).unwrap()))
        }
    }
}

impl<const P: u32, const S: u32> IntoDatum for Numeric<P, S> {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        self.0.into_datum()
    }

    #[inline]
    fn type_oid() -> pg_sys::Oid {
        pg_sys::NUMERICOID
    }
}
