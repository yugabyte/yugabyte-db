//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
//! Wrapper for Postgres 'varlena' type, over Rust types of a fixed size (ie, `impl Copy`)
use crate::{
    pg_sys, rust_regtypein, set_varsize_4b, set_varsize_short, vardata_any, varsize_any,
    varsize_any_exhdr, void_mut_ptr, FromDatum, IntoDatum, PgMemoryContexts, StringInfo,
};
use pgrx_sql_entity_graph::metadata::{
    ArgumentError, Returns, ReturnsError, SqlMapping, SqlTranslatable,
};
use serde::{Deserialize, Serialize};
use std::borrow::Cow;
use std::cmp::Ordering;
use std::hash::{Hash, Hasher};
use std::marker::PhantomData;
use std::ops::{Deref, DerefMut};

struct PallocdVarlena {
    ptr: *mut pg_sys::varlena,
    len: usize,
}

impl Clone for PallocdVarlena {
    fn clone(&self) -> Self {
        let len = self.len;

        // SAFETY:  we know that `self.ptr` is valid as the only way we could have gotten one
        // is internally via Postgres
        let ptr = unsafe {
            PgMemoryContexts::of(self.ptr as void_mut_ptr)
                .expect("could not determine owning memory context")
                .copy_ptr_into(self.ptr as void_mut_ptr, len) as *mut pg_sys::varlena
        };

        PallocdVarlena { ptr, len }
    }
}

/// Wraps a Postgres `varlena *`, presenting it as if it's a Rust type of a fixed size.
///
/// The wrapped `varlena *` is behind a Rust `alloc::borrow:Cow` which ensures that in the
/// common-use case of creating a `PgVarlena` from a Postgres-provided `Datum`, it's not
/// possible to scribble on that Postgres-allocated memory.
///
/// Generally, `PgVarlena` is meant to be used in conjunction with pgrx's `PostgresType` derive macro
/// to provide transparent mapping of fixed-size Rust types as Postgres datums.
///
/// ## Example
///
/// ```rust
/// use std::str::FromStr;
///
/// use pgrx::prelude::*;
/// use serde::{Serialize, Deserialize};
///
/// #[derive(Copy, Clone, PostgresType, Serialize, Deserialize)]
/// #[pgvarlena_inoutfuncs]
/// struct MyType {
///    a: f32,
///    b: f32,
///    c: i64
/// }
///
/// impl PgVarlenaInOutFuncs for MyType {
///     fn input(input: &core::ffi::CStr) -> PgVarlena<Self> {
///         let mut iter = input.to_str().unwrap().split(',');
///         let (a, b, c) = (iter.next(), iter.next(), iter.next());
///
///         let mut result = PgVarlena::<MyType>::new();
///         result.a = f32::from_str(a.unwrap()).expect("a is not a valid f32");
///         result.b = f32::from_str(b.unwrap()).expect("b is not a valid f32");
///         result.c = i64::from_str(c.unwrap()).expect("c is not a valid i64");
///
///         result
///     }
///
///     fn output(&self, buffer: &mut pgrx::StringInfo) {
///         buffer.push_str(&format!("{},{},{}", self.a, self.b, self.c));
///     }
/// }
///
/// #[pg_extern]
/// fn do_a_thing(mut input: PgVarlena<MyType>) -> PgVarlena<MyType> {
///     input.c += 99;  // performs a copy-on-write
///     input
/// }
/// ```
pub struct PgVarlena<T>
where
    T: Copy + Sized,
{
    leaked: Option<*mut PallocdVarlena>,
    varlena: Cow<'static, PallocdVarlena>,
    need_free: bool,
    __marker: PhantomData<T>,
}

impl<T> PgVarlena<T>
where
    T: Copy + Sized,
{
    /// Create a new `PgVarlena` representing a Rust type.  The backing varlena is allocated
    /// by Postgres and initially zero'd (using `pg_sys::palloc0`).  Unless `.into_pg()` is called,
    /// the Postgres-allocated memory will follow Rust's drop semantics.
    ///
    /// ## Example
    ///
    /// ```rust,no_run
    /// use pgrx::prelude::*;
    ///
    /// #[derive(Copy, Clone)]
    /// struct MyType {
    ///    a: f32,
    ///    b: f32,
    ///    c: i64
    /// }
    ///
    /// let mut v = PgVarlena::<MyType>::new();
    /// v.a = 42.0;
    /// v.b = 0.424242;
    /// v.c = 42;
    /// ```
    pub fn new() -> Self {
        let size_of = std::mem::size_of::<T>();

        let ptr = unsafe { pg_sys::palloc0(pg_sys::VARHDRSZ + size_of) as *mut pg_sys::varlena };

        // safe: ptr will halready be allocated
        unsafe {
            if size_of + pg_sys::VARHDRSZ_SHORT <= pg_sys::VARATT_SHORT_MAX as usize {
                // we can use the short header size
                set_varsize_short(ptr, (size_of + pg_sys::VARHDRSZ_SHORT) as i32);
            } else {
                // gotta use the full 4-byte header
                set_varsize_4b(ptr, (size_of + pg_sys::VARHDRSZ) as i32);
            }
        }

        PgVarlena {
            leaked: None,
            varlena: Cow::Owned(PallocdVarlena { ptr, len: unsafe { varsize_any(ptr) } }),
            need_free: true,
            __marker: PhantomData,
        }
    }

    /// Construct a `PgVarlena` from a known-to-be-non-null `pg_sys::Datum`.  As
    /// `FromDatum for PgVarlena<T> where T: Copy + Sized` is already implemented, it is unlikely
    /// that this function will need to be called directly.
    ///
    /// The provided datum is automatically detoasted and the returned `PgVarlena` will either
    /// be considered borrowed or owned based on if detoasting actually needed to allocate memory.
    /// If it didn't, then we're borrowed, otherwise we're owned.
    ///
    /// ## Safety
    ///
    /// This function is considered unsafe as it cannot guarantee the provided `pg_sys::Datum` is a
    /// valid `*mut pg_sys::varlena`.
    pub unsafe fn from_datum(datum: pg_sys::Datum) -> Self {
        let ptr = pg_sys::pg_detoast_datum(datum.cast_mut_ptr());
        let len = varsize_any(ptr);

        if ptr == datum.cast_mut_ptr() {
            // no detoasting happened so we're using borrowed memory
            let leaked = Box::leak(Box::new(PallocdVarlena { ptr, len }));
            PgVarlena {
                leaked: Some(leaked),
                varlena: Cow::Borrowed(leaked),
                need_free: false,
                __marker: PhantomData,
            }
        } else {
            // datum was detoasted so we own and need to free it
            PgVarlena {
                leaked: None,
                varlena: Cow::Owned(PallocdVarlena { ptr, len }),
                need_free: true,
                __marker: PhantomData,
            }
        }
    }

    /// Use when you need to pass the backing `*mut pg_sys::varlena` to a Postgres function.
    ///
    /// This method is also used by the `IntoDatum for PgVarlena<T> where T: Copy + Sized`
    /// implementation.
    pub fn into_pg(mut self) -> *mut pg_sys::varlena {
        // we don't want our varlena to be pfree'd
        self.need_free = false;
        self.varlena.ptr
    }
}

/// `pg_sys::pfree` a `PgVarlena` if we allocated it, instead of Postgres
impl<T> Drop for PgVarlena<T>
where
    T: Copy + Sized,
{
    fn drop(&mut self) {
        if self.need_free {
            unsafe {
                // safe: self.varlena.ptr will never be null
                pg_sys::pfree(self.varlena.ptr as void_mut_ptr);
            }
        }

        if let Some(leaked) = self.leaked {
            unsafe { drop(Box::from_raw(leaked)) }
        }
    }
}

impl<T> Eq for PgVarlena<T> where T: Eq + Copy + Sized {}
impl<T> PartialEq for PgVarlena<T>
where
    T: PartialEq + Copy + Sized,
{
    #[inline]
    fn eq(&self, other: &Self) -> bool {
        self.as_ref() == other.as_ref()
    }
}

impl<T> Ord for PgVarlena<T>
where
    T: Ord + Copy + Sized,
{
    #[inline]
    fn cmp(&self, other: &Self) -> Ordering {
        self.as_ref().cmp(other.as_ref())
    }
}

impl<T> PartialOrd for PgVarlena<T>
where
    T: Ord + Copy + Sized,
{
    #[inline]
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.as_ref().cmp(other.as_ref()))
    }
}

impl<T> Hash for PgVarlena<T>
where
    T: Hash + Copy + Sized,
{
    #[inline]
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.as_ref().hash(state)
    }
}

impl<T> Deref for PgVarlena<T>
where
    T: Copy + Sized,
{
    type Target = T;

    fn deref(&self) -> &Self::Target {
        self.as_ref()
    }
}

impl<T> DerefMut for PgVarlena<T>
where
    T: Copy + Sized,
{
    fn deref_mut(&mut self) -> &mut Self::Target {
        self.as_mut()
    }
}

impl<T> AsRef<T> for PgVarlena<T>
where
    T: Copy + Sized,
{
    fn as_ref(&self) -> &T {
        unsafe {
            // safe: ptr will never be null
            let ptr = vardata_any(self.varlena.ptr) as *const T;
            ptr.as_ref().unwrap()
        }
    }
}

impl<T> Default for PgVarlena<T>
where
    T: Default + Copy,
{
    fn default() -> Self {
        let mut ptr = Self::new();
        *ptr = T::default();
        ptr
    }
}

/// Does a copy-on-write if the backing varlena pointer is borrowed
impl<T> AsMut<T> for PgVarlena<T>
where
    T: Copy + Sized,
{
    fn as_mut(&mut self) -> &mut T {
        unsafe {
            // safe: ptr will never be null
            let ptr = vardata_any(self.varlena.to_mut().ptr) as *mut T;
            ptr.as_mut().unwrap()
        }
    }
}

impl<T> From<PgVarlena<T>> for Option<pg_sys::Datum>
where
    T: Copy + Sized,
{
    fn from(val: PgVarlena<T>) -> Self {
        Some(val.into_pg().into())
    }
}

impl<T> IntoDatum for PgVarlena<T>
where
    T: Copy + Sized,
{
    fn into_datum(self) -> Option<pg_sys::Datum> {
        Some(self.into_pg().into())
    }

    fn type_oid() -> pg_sys::Oid {
        rust_regtypein::<T>()
    }
}

impl<T> FromDatum for PgVarlena<T>
where
    T: Copy + Sized,
{
    unsafe fn from_polymorphic_datum(
        datum: pg_sys::Datum,
        is_null: bool,
        _typoid: pg_sys::Oid,
    ) -> Option<Self> {
        if is_null {
            None
        } else {
            Some(PgVarlena::<T>::from_datum(datum))
        }
    }

    unsafe fn from_datum_in_memory_context(
        mut memory_context: PgMemoryContexts,
        datum: pg_sys::Datum,
        is_null: bool,
        _typoid: pg_sys::Oid,
    ) -> Option<Self> {
        if is_null {
            None
        } else {
            memory_context.switch_to(|_| {
                // this gets the varlena Datum copied into this memory context
                let detoasted = pg_sys::pg_detoast_datum_copy(datum.cast_mut_ptr());

                // and we need to unpack it (if necessary), which will decompress it too
                let varlena = pg_sys::pg_detoast_datum_packed(detoasted);

                // and now we return it as a &str
                Some(PgVarlena::<T>::from_datum(varlena.into()))
            })
        }
    }
}

#[doc(hidden)]
pub unsafe fn cbor_encode<T>(input: T) -> *const pg_sys::varlena
where
    T: Serialize,
{
    let mut serialized = StringInfo::new();

    serialized.push_bytes(&[0u8; pg_sys::VARHDRSZ]); // reserve space for the header
    serde_cbor::to_writer(&mut serialized, &input).expect("failed to encode as CBOR");

    let size = serialized.len();
    let varlena = serialized.into_char_ptr();
    unsafe {
        set_varsize_4b(varlena as *mut pg_sys::varlena, size as i32);
    }

    varlena as *const pg_sys::varlena
}

#[doc(hidden)]
pub unsafe fn cbor_decode<'de, T>(input: *mut pg_sys::varlena) -> T
where
    T: Deserialize<'de>,
{
    let varlena = pg_sys::pg_detoast_datum_packed(input as *mut pg_sys::varlena);
    let len = varsize_any_exhdr(varlena);
    let data = vardata_any(varlena);
    let slice = std::slice::from_raw_parts(data as *const u8, len);
    serde_cbor::from_slice(slice).expect("failed to decode CBOR")
}

#[doc(hidden)]
#[deprecated(since = "0.12.0", note = "just use the FromDatum impl")]
pub unsafe fn cbor_decode_into_context<'de, T>(
    mut memory_context: PgMemoryContexts,
    input: *mut pg_sys::varlena,
) -> T
where
    T: Deserialize<'de>,
{
    memory_context.switch_to(|_| {
        // this gets the varlena Datum copied into this memory context
        let varlena = pg_sys::pg_detoast_datum_copy(input as *mut pg_sys::varlena);
        cbor_decode(varlena)
    })
}

unsafe impl<T> SqlTranslatable for PgVarlena<T>
where
    T: SqlTranslatable + Copy,
{
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        T::argument_sql()
    }

    fn return_sql() -> Result<Returns, ReturnsError> {
        T::return_sql()
    }
}
