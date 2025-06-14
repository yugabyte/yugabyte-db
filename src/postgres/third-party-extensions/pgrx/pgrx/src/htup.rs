//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
//! Utility functions for working with [`pg_sys::HeapTuple`] and [`pg_sys::HeapTupleHeader`] structs
use crate::*;
use std::num::NonZeroUsize;

/// Given a `pg_sys::Datum` representing a composite row type, return a boxed `HeapTupleData`,
/// which can be used by the various `heap_getattr` methods
///
/// ## Safety
///
/// This function is safe, but if the provided `HeapTupleHeader` is null, it will `panic!()`
#[inline]
pub fn composite_row_type_make_tuple(
    row: pg_sys::Datum,
) -> PgBox<pg_sys::HeapTupleData, AllocatedByRust> {
    let htup_header =
        unsafe { pg_sys::pg_detoast_datum_packed(row.cast_mut_ptr()) } as pg_sys::HeapTupleHeader;
    let mut tuple = unsafe { PgBox::<pg_sys::HeapTupleData>::alloc0() };

    tuple.t_len = heap_tuple_header_get_datum_length(htup_header) as u32;
    tuple.t_data = htup_header;

    tuple
}

/// ## Safety
///
/// This function is safe, but if the provided `HeapTupleHeader` is null, it will `panic!()`
#[inline]
pub fn heap_tuple_header_get_datum_length(htup_header: pg_sys::HeapTupleHeader) -> usize {
    if htup_header.is_null() {
        panic!("Attempt to dereference a null HeapTupleHeader");
    }

    unsafe { crate::varlena::varsize(htup_header as *const pg_sys::varlena) }
}

/// convert a HeapTupleHeader to a Datum.
#[inline]
pub unsafe fn heap_tuple_get_datum(heap_tuple: pg_sys::HeapTuple) -> pg_sys::Datum {
    unsafe { pg_sys::HeapTupleHeaderGetDatum((*heap_tuple).t_data) }
}

/// ```c
/// #define HeapTupleHeaderGetTypeId(tup) \
/// ( \
/// (tup)->t_choice.t_datum.datum_typeid \
/// )
/// ```
#[inline]
pub unsafe fn heap_tuple_header_get_type_id(htup_header: pg_sys::HeapTupleHeader) -> pg_sys::Oid {
    htup_header.as_ref().unwrap().t_choice.t_datum.datum_typeid
}

/// ```c
/// #define HeapTupleHeaderGetTypMod(tup) \
/// ( \
/// (tup)->t_choice.t_datum.datum_typmod \
/// )
/// ```
#[inline]
pub unsafe fn heap_tuple_header_get_typmod(htup_header: pg_sys::HeapTupleHeader) -> i32 {
    htup_header.as_ref().unwrap().t_choice.t_datum.datum_typmod
}

/// Extract an attribute of a heap tuple and return it as Rust type.
/// This works for either system or user attributes.  The given `attnum`
/// is properly range-checked.
///
/// If the field in question has a NULL value, we return `None`.
/// Otherwise, a `Some(T)`
///
/// 'tup' is the pointer to the heap tuple.  'attnum' is the attribute
/// number of the column (field) caller wants.  'tupleDesc' is a
/// pointer to the structure describing the row and all its fields.
///
/// `attno` is 1-based
#[inline]
pub fn heap_getattr<T: FromDatum, AllocatedBy: WhoAllocated>(
    tuple: &PgBox<pg_sys::HeapTupleData, AllocatedBy>,
    attno: NonZeroUsize,
    tupdesc: &PgTupleDesc,
) -> Option<T> {
    let mut is_null = false;
    let datum = unsafe {
        pg_sys::heap_getattr(tuple.as_ptr(), attno.get() as _, tupdesc.as_ptr(), &mut is_null)
    };
    let typoid = tupdesc.get(attno.get() - 1).expect("no attribute").type_oid();

    if is_null {
        None
    } else {
        unsafe { T::from_polymorphic_datum(datum, false, typoid.value()) }
    }
}

/// Extract an attribute of a heap tuple and return it as a Datum.
/// This works for either system or user attributes.  The given `attnum`
/// is properly range-checked.
///
/// If the field in question has a NULL value, we return `None`.
/// Otherwise, a `Some(pg_sys::Datum)`
///
/// 'tup' is the pointer to the heap tuple.  'attnum' is the attribute
/// number of the column (field) caller wants.  'tupleDesc' is a
/// pointer to the structure describing the row and all its fields.
///
/// `attno` is 1-based
///
/// ## Safety
///
/// This function is unsafe as it cannot validate that the provided pointers are valid.
#[inline]
pub unsafe fn heap_getattr_raw(
    tuple: *mut pg_sys::HeapTupleData,
    attno: NonZeroUsize,
    tupdesc: pg_sys::TupleDesc,
) -> Option<pg_sys::Datum> {
    let mut is_null = false;
    let datum = pg_sys::heap_getattr(tuple, attno.get() as _, tupdesc, &mut is_null);
    if is_null {
        None
    } else {
        Some(datum)
    }
}

#[derive(Debug, Clone)]
pub struct DatumWithTypeInfo {
    pub datum: pg_sys::Datum,
    pub is_null: bool,
    pub typoid: PgOid,
    pub typlen: i16,
    pub typbyval: bool,
}

impl DatumWithTypeInfo {
    #[inline]
    pub fn into_value<T: FromDatum>(self) -> T {
        unsafe { T::from_polymorphic_datum(self.datum, self.is_null, self.typoid.value()).unwrap() }
    }
}

/// Similar to `heap_getattr()`, but returns extended information about the requested attribute
/// `attno` is 1-based
#[inline]
pub fn heap_getattr_datum_ex(
    tuple: &PgBox<pg_sys::HeapTupleData>,
    attno: NonZeroUsize,
    tupdesc: &PgTupleDesc,
) -> DatumWithTypeInfo {
    let mut is_null = false;
    let datum = unsafe {
        pg_sys::heap_getattr(tuple.as_ptr(), attno.get() as _, tupdesc.as_ptr(), &mut is_null)
    };
    let typoid = tupdesc.get(attno.get() - 1).expect("no attribute").type_oid();

    let mut typlen = 0;
    let mut typbyval = false;
    let mut typalign = 0 as std::os::raw::c_char; // unused

    unsafe {
        pg_sys::get_typlenbyvalalign(typoid.value(), &mut typlen, &mut typbyval, &mut typalign);
    }

    DatumWithTypeInfo { datum, is_null, typoid, typlen, typbyval }
}

/// Implemented for Rust tuples that can be represented as a Postgres [`pg_sys::HeapTupleData`].
pub trait IntoHeapTuple {
    /// Convert `Self` into a `pg_sys::HeapTupleData`, returning a pointer to it.
    ///
    /// # Safety
    ///
    /// This function is unsafe as it cannot guarantee the specified `tupdesc` is valid.
    unsafe fn into_heap_tuple(
        self,
        tupdesc: *mut pg_sys::TupleDescData,
    ) -> *mut pg_sys::HeapTupleData;
}
