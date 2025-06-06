//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::{pg_sys, FromDatum, IntoDatum, PgMemoryContexts};

impl FromDatum for pg_sys::BOX {
    unsafe fn from_polymorphic_datum(
        datum: pg_sys::Datum,
        is_null: bool,
        _: pg_sys::Oid,
    ) -> Option<Self>
    where
        Self: Sized,
    {
        if is_null {
            None
        } else {
            let the_box = datum.cast_mut_ptr::<pg_sys::BOX>();
            Some(the_box.read())
        }
    }
}

impl IntoDatum for pg_sys::BOX {
    fn into_datum(mut self) -> Option<pg_sys::Datum> {
        unsafe {
            let ptr = PgMemoryContexts::CurrentMemoryContext
                .copy_ptr_into(&mut self, std::mem::size_of::<pg_sys::BOX>());
            Some(ptr.into())
        }
    }

    fn type_oid() -> pg_sys::Oid {
        pg_sys::BOXOID
    }
}

impl FromDatum for pg_sys::Point {
    unsafe fn from_polymorphic_datum(
        datum: pg_sys::Datum,
        is_null: bool,
        _: pg_sys::Oid,
    ) -> Option<Self>
    where
        Self: Sized,
    {
        if is_null {
            None
        } else {
            let point: *mut Self = datum.cast_mut_ptr();
            Some(point.read())
        }
    }
}

impl IntoDatum for pg_sys::Point {
    fn into_datum(mut self) -> Option<pg_sys::Datum> {
        unsafe {
            let copy = PgMemoryContexts::CurrentMemoryContext
                .copy_ptr_into(&mut self, std::mem::size_of::<pg_sys::Point>());
            Some(copy.into())
        }
    }

    fn type_oid() -> pg_sys::Oid {
        pg_sys::POINTOID
    }
}
