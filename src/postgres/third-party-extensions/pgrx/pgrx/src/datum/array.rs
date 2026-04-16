//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
#![allow(clippy::question_mark)]
use super::{unbox, UnboxDatum};
use crate::array::RawArray;
use crate::nullable::{
    BitSliceNulls, IntoNullableIterator, MaybeStrictNulls, NullLayout, Nullable, NullableContainer,
};
use crate::toast::Toast;
use crate::{layout::*, nullable};
use crate::{pg_sys, FromDatum, IntoDatum, PgMemoryContexts};
use core::fmt::{Debug, Formatter};
use core::ops::DerefMut;
use core::ptr::NonNull;
use pgrx_sql_entity_graph::metadata::{
    ArgumentError, Returns, ReturnsError, SqlMapping, SqlTranslatable,
};
use serde::{Serialize, Serializer};
use std::iter::FusedIterator;

/** An array of some type (eg. `TEXT[]`, `int[]`)

While conceptually similar to a [`Vec<T>`][std::vec::Vec], arrays are lazy.

Using a [`Vec<T>`][std::vec::Vec] here means each element of the passed array will be eagerly fetched and converted into a Rust type:

```rust,no_run
use pgrx::prelude::*;

#[pg_extern]
fn with_vec(elems: Vec<String>) {
    // Elements all already converted.
    for elem in elems {
        todo!()
    }
}
```

Using an array, elements are only fetched and converted into a Rust type on demand:

```rust,no_run
use pgrx::prelude::*;

#[pg_extern]
fn with_vec(elems: Array<String>) {
    // Elements converted one by one
    for maybe_elem in elems {
        let elem = maybe_elem.unwrap();
        todo!()
    }
}
```
*/
// An array is a detoasted varlena type, so we reason about the lifetime of
// the memory context that the varlena is actually detoasted into.
pub struct Array<'mcx, T> {
    null_slice: MaybeStrictNulls<BitSliceNulls<'mcx>>,
    slide_impl: ChaChaSlideImpl<T>,
    // Rust drops in FIFO order, drop this last
    raw: Toast<RawArray>,
}

impl<T: UnboxDatum> Debug for Array<'_, T>
where
    for<'arr> <T as UnboxDatum>::As<'arr>: Debug,
{
    fn fmt(&self, f: &mut Formatter<'_>) -> core::fmt::Result {
        f.debug_list().entries(self.iter()).finish()
    }
}

type ChaChaSlideImpl<T> = Box<dyn casper::ChaChaSlide<T>>;

impl<'mcx, T: UnboxDatum> serde::Serialize for Array<'mcx, T>
where
    for<'arr> <T as UnboxDatum>::As<'arr>: Serialize,
{
    fn serialize<S>(&self, serializer: S) -> Result<<S as Serializer>::Ok, <S as Serializer>::Error>
    where
        S: Serializer,
    {
        serializer.collect_seq(self.iter())
    }
}

#[deny(unsafe_op_in_unsafe_fn)]
impl<'mcx, T: UnboxDatum> Array<'mcx, T> {
    /// # Safety
    ///
    /// This function requires that the RawArray was obtained in a properly-constructed form
    /// (probably from Postgres).
    pub(crate) unsafe fn deconstruct_from(mut raw: Toast<RawArray>) -> Array<'mcx, T> {
        let oid = raw.oid();
        let elem_layout = Layout::lookup_oid(oid);
        let null_inner = raw
            .nulls_bitslice()
            .map(|nonnull| unsafe { nullable::BitSliceNulls(&*nonnull.as_ptr()) });
        let null_slice = MaybeStrictNulls::new(null_inner);
        // do a little two-step before jumping into the Cha-Cha Slide and figure out
        // which implementation is correct for the type of element in this Array.
        let slide_impl: ChaChaSlideImpl<T> = match elem_layout.pass {
            PassBy::Value => match elem_layout.size {
                // The layout is one that we know how to handle efficiently.
                Size::Fixed(1) => Box::new(casper::FixedSizeByVal::<1>),
                Size::Fixed(2) => Box::new(casper::FixedSizeByVal::<2>),
                Size::Fixed(4) => Box::new(casper::FixedSizeByVal::<4>),
                #[cfg(target_pointer_width = "64")]
                Size::Fixed(8) => Box::new(casper::FixedSizeByVal::<8>),

                _ => {
                    panic!("unrecognized pass-by-value array element layout: {elem_layout:?}")
                }
            },

            PassBy::Ref => match elem_layout.size {
                // Array elements are varlenas, which are pass-by-reference and have a known alignment size
                Size::Varlena => Box::new(casper::PassByVarlena { align: elem_layout.align }),

                // Array elements are C strings, which are pass-by-reference and alignments are
                // determined at runtime based on the length of the string
                Size::CStr => Box::new(casper::PassByCStr),

                // Array elements are fixed sizes yet the data is "pass-by-reference"
                // Most commonly, this is because of elements larger than a Datum.
                Size::Fixed(size) => Box::new(casper::PassByFixed {
                    padded_size: elem_layout.align.pad(size.into()),
                }),
            },
        };

        Array { raw, slide_impl, null_slice }
    }

    /// Return an iterator of `Option<T>`.
    #[inline]
    pub fn iter(&self) -> ArrayIterator<'_, T> {
        let ptr = self.raw.data_ptr();
        ArrayIterator { array: self, curr: 0, ptr }
    }

    /// Return an iterator over the Array's elements.
    ///
    /// # Panics
    /// This function will panic when called if the array contains any SQL NULL values.
    #[inline]
    pub fn iter_deny_null(&self) -> ArrayTypedIterator<'_, T> {
        if self.null_slice.contains_nulls() {
            panic!("array contains NULL");
        }

        let ptr = self.raw.data_ptr();
        ArrayTypedIterator { array: self, curr: 0, ptr }
    }

    /// Retrieve a value from a known-not-null (strict) array.
    fn get_strict_inner<'arr>(&'arr self, index: usize) -> Option<T::As<'arr>> {
        if index >= self.raw.len() {
            return None;
        };

        // This pointer is what's walked over the entire array's data buffer.
        // If the array has varlena or cstr elements, we can't index into the array.
        // If the elements are fixed size, we could, but we do not exploit that optimization yet
        // as it would significantly complicate the code and impact debugging it.
        // Such improvements should wait until a later version.
        //
        // This remains true even in a strict array, because, again,
        // varlena and cstr are variable-length.
        let mut at_byte = self.raw.data_ptr();
        for _i in 0..index {
            // SAFETY: Note this entire function has to be correct,
            // not just this one call, for this to be correct!
            at_byte = unsafe { self.one_hop_this_time(at_byte) };
        }

        // If this has gotten this far, it is known to be non-null,
        // all the null values in the array up to this index were skipped,
        // and the only offsets were via our hopping function.
        Some(unsafe { self.bring_it_back_now(at_byte, false).expect("Null value in Strict array") })
    }

    /// Retrieve a value from a known-nullable (not strict) array.
    fn get_nullable_inner<'arr>(
        &'arr self,
        nulls: &'arr BitSliceNulls,
        index: usize,
    ) -> Option<Option<T::As<'arr>>> {
        // This assertion should only fail if null_slice is longer than the
        // actual array,thanks to the check above.
        debug_assert!(index < self.raw.len());

        // This pointer is what's walked over the entire array's data buffer.
        // If the array has varlena or cstr elements, we can't index into the array.
        // If the elements are fixed size, we could, but we do not exploit that optimization yet
        // as it would significantly complicate the code and impact debugging it.
        // Such improvements should wait until a later version (today's: 0.7.4, preparing 0.8.0).
        let mut at_byte = self.raw.data_ptr();
        for i in 0..index {
            match nulls.is_null(i) {
                None => unreachable!("array was exceeded while walking to known non-null index???"),
                // Skip nulls: the data buffer has no placeholders for them!
                Some(true) => continue,
                Some(false) => {
                    // SAFETY: Note this entire function has to be correct,
                    // not just this one call, for this to be correct!
                    at_byte = unsafe { self.one_hop_this_time(at_byte) };
                }
            }
        }

        // If this has gotten this far, it is known to be non-null,
        // all the null values in the array up to this index were skipped,
        // and the only offsets were via our hopping function.
        Some(unsafe { self.bring_it_back_now(at_byte, false) })
    }

    #[allow(clippy::option_option)]
    #[inline]
    pub fn get<'arr>(&'arr self, index: usize) -> Option<Option<T::As<'arr>>> {
        if index >= self.len() {
            return None;
        }
        match self.null_slice.get_inner().map(|v| (v, v.is_null(index))) {
            // No null_slice, strict array.
            None => self.get_strict_inner(index).map(Some),
            // self.null_slice exists but index is not in bounds.
            Some((_, None)) => None,
            // Elem is null
            Some((_, Some(true))) => Some(None),
            // Elem is not null.
            Some((nulls, Some(false))) => self.get_nullable_inner(nulls, index),
        }
    }

    /// Extracts an element from a Postgres Array's data buffer
    ///
    /// # Safety
    /// This assumes the pointer is to a valid element of that type.
    #[inline]
    unsafe fn bring_it_back_now<'arr>(
        &'arr self,
        ptr: *const u8,
        is_null: bool,
    ) -> Option<T::As<'arr>> {
        match is_null {
            true => None,
            false => {
                // Ensure we are not attempting to dereference a pointer
                // outside of the array.
                debug_assert!(self.is_within_bounds(ptr));
                // Prevent a datum that begins inside the array but would end
                // outside the array from being dereferenced.
                debug_assert!(self.is_within_bounds_inclusive(
                    ptr.wrapping_add(unsafe { self.slide_impl.hop_size(ptr) })
                ));

                unsafe { self.slide_impl.bring_it_back_now(self, ptr) }
            }
        }
    }

    /// Walk the data of a Postgres Array, "hopping" according to element layout.
    ///
    /// # Safety
    /// For the varlena/cstring layout, data in the buffer is read.
    /// In either case, pointer arithmetic is done, with the usual implications,
    /// e.g. the pointer must be <= a "one past the end" pointer
    /// This means this function must be invoked with the correct layout, and
    /// either the array's `data_ptr` or a correctly offset pointer into it.
    ///
    /// Null elements will NOT be present in a Postgres Array's data buffer!
    /// Do not cumulatively invoke this more than `len - null_count`!
    /// Doing so will result in reading uninitialized data, which is UB!
    #[inline]
    unsafe fn one_hop_this_time(&self, ptr: *const u8) -> *const u8 {
        unsafe {
            let offset = self.slide_impl.hop_size(ptr);
            // SAFETY: ptr stops at 1-past-end of the array's varlena
            debug_assert!(ptr.wrapping_add(offset) <= self.raw.end_ptr());
            ptr.add(offset)
        }
    }

    /// Returns true if the pointer provided is within the bounds of the array.
    /// Primarily intended for use with debug_assert!()s.
    /// Note that this will return false for the 1-past-end, which is a useful
    /// position for a pointer to be in, but not valid to dereference.
    #[inline]
    pub(crate) fn is_within_bounds(&self, ptr: *const u8) -> bool {
        // Cast to usize, to prevent LLVM from doing counterintuitive things
        // with pointer equality.
        // See https://github.com/pgcentralfoundation/pgrx/pull/1514#discussion_r1480447846
        let ptr: usize = ptr as usize;
        let data_ptr = self.raw.data_ptr() as usize;
        let end_ptr = self.raw.end_ptr() as usize;
        (data_ptr <= ptr) && (ptr < end_ptr)
    }
    /// Similar to [`Self::is_within_bounds()`], but also returns true for the
    /// 1-past-end position.
    #[inline]
    pub(crate) fn is_within_bounds_inclusive(&self, ptr: *const u8) -> bool {
        // Cast to usize, to prevent LLVM from doing counterintuitive things
        // with pointer equality.
        // See https://github.com/pgcentralfoundation/pgrx/pull/1514#discussion_r1480447846
        let ptr = ptr as usize;
        let data_ptr = self.raw.data_ptr() as usize;
        let end_ptr = self.raw.end_ptr() as usize;
        (data_ptr <= ptr) && (ptr <= end_ptr)
    }
}

#[deny(unsafe_op_in_unsafe_fn)]
impl<T> Array<'_, T> {
    /// Rips out the underlying `pg_sys::ArrayType` pointer.
    /// Note that Array may have caused Postgres to allocate to unbox the datum,
    /// and this can hypothetically cause a memory leak if so.
    #[inline]
    pub fn into_array_type(self) -> *const pg_sys::ArrayType {
        // may be worth replacing this function when Toast<T> matures enough
        // to be used as a public type with a fn(self) -> Toast<RawArray>

        let Array { raw, .. } = self;
        // Wrap the Toast<RawArray> to prevent it from deallocating itself
        let mut raw = core::mem::ManuallyDrop::new(raw);
        let ptr = raw.deref_mut().deref_mut() as *mut RawArray;
        // SAFETY: Leaks are safe if they aren't use-after-frees!
        unsafe { ptr.read() }.into_ptr().as_ptr() as _
    }

    /// Returns `true` if this [`Array`] contains one or more SQL "NULL" values
    #[inline]
    pub fn contains_nulls(&self) -> bool {
        self.null_slice.get_inner().is_some_and(|slice| slice.contains_nulls())
    }

    #[inline]
    pub fn len(&self) -> usize {
        self.raw.len()
    }

    #[inline]
    pub fn is_empty(&self) -> bool {
        self.raw.len() == 0
    }
}

/// Adapter to use `Nullable<T>` for array iteration.
pub struct NullableArrayIterator<'mcx, T>
where
    T: UnboxDatum,
{
    inner: ArrayIterator<'mcx, T>,
}

impl<'mcx, T> Iterator for NullableArrayIterator<'mcx, T>
where
    T: UnboxDatum,
{
    type Item = Nullable<<T as unbox::UnboxDatum>::As<'mcx>>;

    #[inline]
    fn next(&mut self) -> Option<Self::Item> {
        self.inner.next().map(|v| v.into())
    }
}

impl<'mcx, T> IntoNullableIterator<<T as unbox::UnboxDatum>::As<'mcx>> for &'mcx Array<'mcx, T>
where
    T: UnboxDatum,
{
    type Iter = NullableArrayIterator<'mcx, T>;

    fn into_nullable_iter(self) -> Self::Iter {
        NullableArrayIterator { inner: self.iter() }
    }
}

impl<'mcx, T: UnboxDatum> NullableContainer<'mcx, usize, <T as unbox::UnboxDatum>::As<'mcx>>
    for Array<'mcx, T>
{
    type Layout = MaybeStrictNulls<BitSliceNulls<'mcx>>;

    #[inline]
    fn get_layout(&'mcx self) -> &'mcx Self::Layout {
        &self.null_slice
    }

    #[inline]
    unsafe fn get_raw(&'mcx self, idx: usize) -> <T as unbox::UnboxDatum>::As<'mcx> {
        self.get_strict_inner(idx).expect(
            "get_raw() called with an invalid index, bounds-checking\
            *should* occur before calling this method.",
        )
    }

    #[inline]
    fn len(&'mcx self) -> usize {
        self.len()
    }
}

#[derive(thiserror::Error, Debug, Copy, Clone, Eq, PartialEq)]
pub enum ArraySliceError {
    #[error("Cannot create a slice of an Array that contains nulls")]
    ContainsNulls,
}

#[cfg(target_pointer_width = "64")]
impl Array<'_, f64> {
    /// Returns a slice of `f64`s which comprise this [`Array`].
    ///
    /// # Errors
    ///
    /// Returns a [`ArraySliceError::ContainsNulls`] error if this [`Array`] contains one or more
    /// SQL "NULL" values.  In this case, you'd likely want to fallback to using [`Array::iter()`].
    #[inline]
    pub fn as_slice(&self) -> Result<&[f64], ArraySliceError> {
        as_slice(self)
    }
}

impl Array<'_, f32> {
    /// Returns a slice of `f32`s which comprise this [`Array`].
    ///
    /// # Errors
    ///
    /// Returns a [`ArraySliceError::ContainsNulls`] error if this [`Array`] contains one or more
    /// SQL "NULL" values.  In this case, you'd likely want to fallback to using [`Array::iter()`].
    #[inline]
    pub fn as_slice(&self) -> Result<&[f32], ArraySliceError> {
        as_slice(self)
    }
}

#[cfg(target_pointer_width = "64")]
impl Array<'_, i64> {
    /// Returns a slice of `i64`s which comprise this [`Array`].
    ///
    /// # Errors
    ///
    /// Returns a [`ArraySliceError::ContainsNulls`] error if this [`Array`] contains one or more
    /// SQL "NULL" values.  In this case, you'd likely want to fallback to using [`Array::iter()`].
    #[inline]
    pub fn as_slice(&self) -> Result<&[i64], ArraySliceError> {
        as_slice(self)
    }
}

impl Array<'_, i32> {
    /// Returns a slice of `i32`s which comprise this [`Array`].
    ///
    /// # Errors
    ///
    /// Returns a [`ArraySliceError::ContainsNulls`] error if this [`Array`] contains one or more
    /// SQL "NULL" values.  In this case, you'd likely want to fallback to using [`Array::iter()`].
    #[inline]
    pub fn as_slice(&self) -> Result<&[i32], ArraySliceError> {
        as_slice(self)
    }
}

impl Array<'_, i16> {
    /// Returns a slice of `i16`s which comprise this [`Array`].
    ///
    /// # Errors
    ///
    /// Returns a [`ArraySliceError::ContainsNulls`] error if this [`Array`] contains one or more
    /// SQL "NULL" values.  In this case, you'd likely want to fallback to using [`Array::iter()`].
    #[inline]
    pub fn as_slice(&self) -> Result<&[i16], ArraySliceError> {
        as_slice(self)
    }
}

impl Array<'_, i8> {
    /// Returns a slice of `i8`s which comprise this [`Array`].
    ///
    /// # Errors
    ///
    /// Returns a [`ArraySliceError::ContainsNulls`] error if this [`Array`] contains one or more
    /// SQL "NULL" values.  In this case, you'd likely want to fallback to using [`Array::iter()`].
    #[inline]
    pub fn as_slice(&self) -> Result<&[i8], ArraySliceError> {
        as_slice(self)
    }
}

#[inline(always)]
fn as_slice<'a, T: Sized>(array: &'a Array<'_, T>) -> Result<&'a [T], ArraySliceError> {
    if array.contains_nulls() {
        return Err(ArraySliceError::ContainsNulls);
    }

    let slice =
        unsafe { std::slice::from_raw_parts(array.raw.data_ptr() as *const _, array.len()) };
    Ok(slice)
}

mod casper {
    use super::UnboxDatum;
    use crate::layout::Align;
    use crate::{pg_sys, varlena, Array};

    // it's a pop-culture reference (https://en.wikipedia.org/wiki/Cha_Cha_Slide) not some fancy crypto thing you nerd
    /// Describes how to instantiate a value `T` from an [`Array`] and its backing byte array pointer.
    /// It also knows how to determine the size of an [`Array`] element value.
    pub(super) trait ChaChaSlide<T: UnboxDatum> {
        /// Instantiate a `T` from the head of `ptr`
        ///
        /// # Safety
        ///
        /// This function is unsafe as it cannot guarantee that `ptr` points to the proper bytes
        /// that represent a `T`, or even that it belongs to `array`.  Both of which must be true
        unsafe fn bring_it_back_now<'arr, 'mcx>(
            &self,
            array: &'arr Array<'mcx, T>,
            ptr: *const u8,
        ) -> Option<T::As<'arr>>;

        /// Determine how many bytes are used to represent `T`.  This could be fixed size or
        /// even determined at runtime by whatever `ptr` is known to be pointing at.
        ///
        /// # Safety
        ///
        /// This function is unsafe as it cannot guarantee that `ptr` points to the bytes of a `T`,
        /// which it must for implementations that rely on that.
        unsafe fn hop_size(&self, ptr: *const u8) -> usize;
    }

    #[inline(always)]
    fn is_aligned<T>(p: *const T) -> bool {
        (p as usize) & (core::mem::align_of::<T>() - 1) == 0
    }

    /// Safety: Equivalent to a (potentially) aligned read of `ptr`, which
    /// should be `Copy` (ideally...).
    #[track_caller]
    #[inline(always)]
    pub(super) unsafe fn byval_read<T: Copy>(ptr: *const u8) -> T {
        let ptr = ptr.cast::<T>();
        debug_assert!(is_aligned(ptr), "not aligned to {}: {ptr:p}", std::mem::align_of::<T>());
        ptr.read()
    }

    /// Fixed-size byval array elements. N should be 1, 2, 4, or 8. Note that
    /// `T` (the rust type) may have a different size than `N`.
    pub(super) struct FixedSizeByVal<const N: usize>;
    impl<T: UnboxDatum, const N: usize> ChaChaSlide<T> for FixedSizeByVal<N> {
        #[inline(always)]
        unsafe fn bring_it_back_now<'arr, 'mcx>(
            &self,
            _array: &'arr Array<'mcx, T>,
            ptr: *const u8,
        ) -> Option<T::As<'arr>> {
            // This branch is optimized away (because `N` is constant).
            let datum = match N {
                // for match with `Datum`, read through that directly to
                // preserve provenance (may not be relevant but doesn't hurt).
                1 => pg_sys::Datum::from(byval_read::<u8>(ptr)),
                2 => pg_sys::Datum::from(byval_read::<u16>(ptr)),
                4 => pg_sys::Datum::from(byval_read::<u32>(ptr)),
                8 => pg_sys::Datum::from(byval_read::<u64>(ptr)),
                _ => unreachable!("`N` must be 1, 2, 4, or 8 (got {N})"),
            };
            Some(T::unbox(core::mem::transmute(datum)))
        }

        #[inline(always)]
        unsafe fn hop_size(&self, _ptr: *const u8) -> usize {
            N
        }
    }

    /// Array elements are [`pg_sys::varlena`] types, which are pass-by-reference
    pub(super) struct PassByVarlena {
        pub(super) align: Align,
    }
    impl<T: UnboxDatum> ChaChaSlide<T> for PassByVarlena {
        #[inline]
        unsafe fn bring_it_back_now<'arr, 'mcx>(
            &self,
            // May need this array param for MemCx reasons?
            _array: &'arr Array<'mcx, T>,
            ptr: *const u8,
        ) -> Option<T::As<'arr>> {
            let datum = pg_sys::Datum::from(ptr);
            Some(T::unbox(core::mem::transmute(datum)))
        }

        #[inline]
        unsafe fn hop_size(&self, ptr: *const u8) -> usize {
            // SAFETY: This uses the varsize_any function to be safe,
            // and the caller was informed of pointer requirements.
            let varsize = varlena::varsize_any(ptr.cast());

            // Now make sure this is aligned-up
            self.align.pad(varsize)
        }
    }

    /// Array elements are standard C strings (`char *`), which are pass-by-reference
    pub(super) struct PassByCStr;
    impl<T: UnboxDatum> ChaChaSlide<T> for PassByCStr {
        #[inline]
        unsafe fn bring_it_back_now<'arr, 'mcx>(
            &self,
            _array: &'arr Array<'mcx, T>,
            ptr: *const u8,
        ) -> Option<T::As<'arr>> {
            let datum = pg_sys::Datum::from(ptr);
            Some(T::unbox(core::mem::transmute(datum)))
        }

        #[inline]
        unsafe fn hop_size(&self, ptr: *const u8) -> usize {
            // SAFETY: The caller was informed of pointer requirements.
            let strlen = core::ffi::CStr::from_ptr(ptr.cast()).to_bytes().len();

            // Skip over the null which points us to the head of the next cstr
            strlen + 1
        }
    }

    pub(super) struct PassByFixed {
        pub(super) padded_size: usize,
    }

    impl<T: UnboxDatum> ChaChaSlide<T> for PassByFixed {
        #[inline]
        unsafe fn bring_it_back_now<'arr, 'mcx>(
            &self,
            _array: &'arr Array<'mcx, T>,
            ptr: *const u8,
        ) -> Option<T::As<'arr>> {
            let datum = pg_sys::Datum::from(ptr);
            Some(T::unbox(core::mem::transmute(datum)))
        }

        #[inline]
        unsafe fn hop_size(&self, _ptr: *const u8) -> usize {
            self.padded_size
        }
    }
}

pub struct VariadicArray<'mcx, T>(Array<'mcx, T>);

impl<'mcx, T: UnboxDatum> Serialize for VariadicArray<'mcx, T>
where
    for<'arr> <T as UnboxDatum>::As<'arr>: Serialize,
{
    fn serialize<S>(&self, serializer: S) -> Result<<S as Serializer>::Ok, <S as Serializer>::Error>
    where
        S: Serializer,
    {
        serializer.collect_seq(self.0.iter())
    }
}

impl<'mcx, T: UnboxDatum> VariadicArray<'mcx, T> {
    pub(crate) fn wrap_array(arr: Array<'mcx, T>) -> Self {
        VariadicArray(arr)
    }

    /// Return an Iterator of `Option<T>` over the contained Datums.
    #[inline]
    pub fn iter(&self) -> ArrayIterator<'_, T> {
        self.0.iter()
    }

    /// Return an iterator over the Array's elements.
    ///
    /// # Panics
    /// This function will panic when called if the array contains any SQL NULL values.
    #[inline]
    pub fn iter_deny_null(&self) -> ArrayTypedIterator<'_, T> {
        self.0.iter_deny_null()
    }

    #[allow(clippy::option_option)]
    #[inline]
    pub fn get<'arr>(&'arr self, i: usize) -> Option<Option<<T as UnboxDatum>::As<'arr>>> {
        self.0.get(i)
    }
}

impl<T> VariadicArray<'_, T> {
    #[inline]
    pub fn into_array_type(self) -> *const pg_sys::ArrayType {
        self.0.into_array_type()
    }

    #[inline]
    pub fn len(&self) -> usize {
        self.0.len()
    }

    #[inline]
    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }
}

pub struct ArrayTypedIterator<'arr, T> {
    array: &'arr Array<'arr, T>,
    curr: usize,
    ptr: *const u8,
}

impl<'arr, T: UnboxDatum> Iterator for ArrayTypedIterator<'arr, T> {
    type Item = T::As<'arr>;

    #[inline]
    fn next(&mut self) -> Option<Self::Item> {
        let Self { array, curr, ptr } = self;
        if *curr >= array.raw.len() {
            None
        } else {
            // SAFETY: The constructor for this type instantly panics if any nulls are present!
            // Thus as an invariant, this will never have to reckon with the nullbitmap.
            let element = unsafe { array.bring_it_back_now(*ptr, false) };
            *curr += 1;
            *ptr = unsafe { array.one_hop_this_time(*ptr) };
            element
        }
    }

    #[inline]
    fn size_hint(&self) -> (usize, Option<usize>) {
        let len = self.array.raw.len().saturating_sub(self.curr);
        (len, Some(len))
    }
}

impl<'a, T: UnboxDatum> ExactSizeIterator for ArrayTypedIterator<'a, T> {}
impl<'a, T: UnboxDatum> core::iter::FusedIterator for ArrayTypedIterator<'a, T> {}

impl<'arr, T: UnboxDatum + serde::Serialize> serde::Serialize for ArrayTypedIterator<'arr, T>
where
    <T as UnboxDatum>::As<'arr>: serde::Serialize,
{
    fn serialize<S>(&self, serializer: S) -> Result<<S as Serializer>::Ok, <S as Serializer>::Error>
    where
        S: Serializer,
    {
        serializer.collect_seq(self.array.iter())
    }
}

pub struct ArrayIterator<'arr, T> {
    array: &'arr Array<'arr, T>,
    curr: usize,
    ptr: *const u8,
}

impl<'arr, T: UnboxDatum> Iterator for ArrayIterator<'arr, T> {
    type Item = Option<T::As<'arr>>;

    #[inline]
    fn next(&mut self) -> Option<Self::Item> {
        let Self { array, curr, ptr } = self;

        if *curr >= array.len() {
            return None;
        }
        let is_null = (match array.null_slice.get_inner().map(|slice| slice.is_null(*curr)) {
            // Null slice exists, use its logic for bounds-checking
            // and null status.
            Some(elem) => elem,
            // Strict array, no nulls.
            // Bounds-checking behavior is still needed though.
            None => (*curr < array.len()).then_some(false),
        })?;
        *curr += 1;

        let element = unsafe { array.bring_it_back_now(*ptr, is_null) };
        if !is_null {
            // SAFETY: This has to not move for nulls, as they occupy 0 data bytes,
            // and it has to move only after unpacking a non-null varlena element,
            // as the iterator starts by pointing to the first non-null element!
            *ptr = unsafe { array.one_hop_this_time(*ptr) };
        }
        Some(element)
    }

    #[inline]
    fn size_hint(&self) -> (usize, Option<usize>) {
        let len = self.array.raw.len().saturating_sub(self.curr);
        (len, Some(len))
    }
}

impl<'arr, T: UnboxDatum> ExactSizeIterator for ArrayIterator<'arr, T> {}
impl<'arr, T: UnboxDatum> FusedIterator for ArrayIterator<'arr, T> {}

pub struct ArrayIntoIterator<'a, T> {
    array: Array<'a, T>,
    curr: usize,
    ptr: *const u8,
}

// There's nowhere to name the lifetime contraction
impl<'mcx, T> IntoIterator for Array<'mcx, T>
where
    for<'arr> T: UnboxDatum<As<'arr> = T> + 'static,
{
    type Item = Option<T::As<'mcx>>;
    type IntoIter = ArrayIntoIterator<'mcx, T>;

    #[inline]
    fn into_iter(self) -> Self::IntoIter {
        let ptr = self.raw.data_ptr();
        ArrayIntoIterator { array: self, curr: 0, ptr }
    }
}

impl<'mcx, T> IntoIterator for VariadicArray<'mcx, T>
where
    for<'arr> T: UnboxDatum<As<'arr> = T> + 'static,
{
    type Item = Option<T::As<'mcx>>;
    type IntoIter = ArrayIntoIterator<'mcx, T>;

    #[inline]
    fn into_iter(self) -> Self::IntoIter {
        let ptr = self.0.raw.data_ptr();
        ArrayIntoIterator { array: self.0, curr: 0, ptr }
    }
}

impl<'mcx, T> Iterator for ArrayIntoIterator<'mcx, T>
where
    for<'arr> T: UnboxDatum<As<'arr> = T> + 'static,
{
    type Item = Option<T::As<'static>>;

    #[inline]
    fn next(&mut self) -> Option<Self::Item> {
        let Self { array, curr, ptr } = self;

        if *curr >= array.len() {
            return None;
        }

        let is_null = (match array.null_slice.get_inner().map(|slice| slice.is_null(*curr)) {
            // Null slice exists, use its logic for bounds-checking
            // and null status.
            Some(elem) => elem,
            // Strict array, no nulls.
            // Bounds-checking behavior is still needed though.
            None => (*curr < array.len()).then_some(false),
        })?;

        *curr += 1;
        debug_assert!(array.is_within_bounds(*ptr));
        let element = unsafe { array.bring_it_back_now(*ptr, is_null) };
        if !is_null {
            // SAFETY: This has to not move for nulls, as they occupy 0 data bytes,
            // and it has to move only after unpacking a non-null varlena element,
            // as the iterator starts by pointing to the first non-null element!
            *ptr = unsafe { array.one_hop_this_time(*ptr) };
        }
        Some(element)
    }

    #[inline]
    fn size_hint(&self) -> (usize, Option<usize>) {
        let len = self.array.raw.len().saturating_sub(self.curr);
        (len, Some(len))
    }
}

impl<'mcx, T> ExactSizeIterator for ArrayIntoIterator<'mcx, T> where
    for<'arr> T: UnboxDatum<As<'arr> = T> + 'static
{
}
impl<'mcx, T: UnboxDatum> FusedIterator for ArrayIntoIterator<'mcx, T> where
    for<'arr> T: UnboxDatum<As<'arr> = T> + 'static
{
}

impl<'a, T: FromDatum + UnboxDatum> FromDatum for VariadicArray<'a, T> {
    #[inline]
    unsafe fn from_polymorphic_datum(
        datum: pg_sys::Datum,
        is_null: bool,
        oid: pg_sys::Oid,
    ) -> Option<VariadicArray<'a, T>> {
        Array::from_polymorphic_datum(datum, is_null, oid).map(Self)
    }
}

impl<'a, T: UnboxDatum> FromDatum for Array<'a, T> {
    #[inline]
    unsafe fn from_polymorphic_datum(
        datum: pg_sys::Datum,
        is_null: bool,
        _typoid: pg_sys::Oid,
    ) -> Option<Array<'a, T>> {
        if is_null {
            None
        } else {
            let Some(ptr) = NonNull::new(datum.cast_mut_ptr()) else { return None };
            let raw = RawArray::detoast_from_varlena(ptr);
            Some(Array::deconstruct_from(raw))
        }
    }

    unsafe fn from_datum_in_memory_context(
        mut memory_context: PgMemoryContexts,
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
            memory_context.switch_to(|_| {
                // copy the Datum into this MemoryContext, and then instantiate the Array wrapper
                let copy = pg_sys::pg_detoast_datum_copy(datum.cast_mut_ptr());
                Array::<T>::from_polymorphic_datum(pg_sys::Datum::from(copy), false, typoid)
            })
        }
    }
}

impl<T: IntoDatum> IntoDatum for Array<'_, T> {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        let array_type = self.into_array_type();
        let datum = pg_sys::Datum::from(array_type);
        Some(datum)
    }

    #[inline]
    fn type_oid() -> pg_sys::Oid {
        unsafe { pg_sys::get_array_type(T::type_oid()) }
    }

    fn composite_type_oid(&self) -> Option<pg_sys::Oid> {
        Some(unsafe { pg_sys::get_array_type(self.raw.oid()) })
    }
}

impl<T> FromDatum for Vec<T>
where
    for<'arr> T: UnboxDatum<As<'arr> = T> + 'arr,
{
    #[inline]
    unsafe fn from_polymorphic_datum(
        datum: pg_sys::Datum,
        is_null: bool,
        typoid: pg_sys::Oid,
    ) -> Option<Vec<T>> {
        if is_null {
            None
        } else {
            Array::<T>::from_polymorphic_datum(datum, is_null, typoid)
                .map(|array| array.iter_deny_null().collect::<Vec<_>>())
        }
    }

    unsafe fn from_datum_in_memory_context(
        memory_context: PgMemoryContexts,
        datum: pg_sys::Datum,
        is_null: bool,
        typoid: pg_sys::Oid,
    ) -> Option<Self>
    where
        Self: Sized,
    {
        Array::<T>::from_datum_in_memory_context(memory_context, datum, is_null, typoid)
            .map(|array| array.iter_deny_null().collect::<Vec<_>>())
    }
}

impl<T> FromDatum for Vec<Option<T>>
where
    for<'arr> T: UnboxDatum<As<'arr> = T> + 'arr,
{
    #[inline]
    unsafe fn from_polymorphic_datum(
        datum: pg_sys::Datum,
        is_null: bool,
        typoid: pg_sys::Oid,
    ) -> Option<Vec<Option<T>>> {
        Array::<T>::from_polymorphic_datum(datum, is_null, typoid)
            .map(|array| array.iter().collect::<Vec<_>>())
    }

    unsafe fn from_datum_in_memory_context(
        memory_context: PgMemoryContexts,
        datum: pg_sys::Datum,
        is_null: bool,
        typoid: pg_sys::Oid,
    ) -> Option<Self>
    where
        Self: Sized,
    {
        Array::<T>::from_datum_in_memory_context(memory_context, datum, is_null, typoid)
            .map(|array| array.iter().collect::<Vec<_>>())
    }
}

#[inline]
/// Converts an iterator into an array datum
fn array_datum_from_iter<T: IntoDatum>(elements: impl Iterator<Item = T>) -> Option<pg_sys::Datum> {
    let mut state = unsafe {
        pg_sys::initArrayResult(
            T::type_oid(),
            PgMemoryContexts::CurrentMemoryContext.value(),
            // All elements use the same memory context
            false,
        )
    };
    for s in elements {
        let datum = s.into_datum();
        let isnull = datum.is_none();

        unsafe {
            state = pg_sys::accumArrayResult(
                state,
                datum.unwrap_or(0.into()),
                isnull,
                T::type_oid(),
                PgMemoryContexts::CurrentMemoryContext.value(),
            );
        }
    }

    // Should not happen: {init, accum}ArrayResult both return non-null pointers
    assert!(!state.is_null());

    Some(unsafe { pg_sys::makeArrayResult(state, PgMemoryContexts::CurrentMemoryContext.value()) })
}

impl<T> IntoDatum for Vec<T>
where
    T: IntoDatum,
{
    fn into_datum(self) -> Option<pg_sys::Datum> {
        array_datum_from_iter(self.into_iter())
    }

    fn type_oid() -> pg_sys::Oid {
        unsafe { pg_sys::get_array_type(T::type_oid()) }
    }

    #[allow(clippy::get_first)] // https://github.com/pgcentralfoundation/pgrx/issues/1363
    fn composite_type_oid(&self) -> Option<pg_sys::Oid> {
        // the composite type oid for a vec of composite types is the array type of the base composite type
        // the use of first() would have presented a false certainty here: it's not actually relevant that it be the first.
        #[allow(clippy::get_first)]
        self.get(0)
            .and_then(|v| v.composite_type_oid().map(|oid| unsafe { pg_sys::get_array_type(oid) }))
    }

    #[inline]
    fn is_compatible_with(other: pg_sys::Oid) -> bool {
        Self::type_oid() == other || other == unsafe { pg_sys::get_array_type(T::type_oid()) }
    }
}

impl<'a, T> IntoDatum for &'a [T]
where
    T: IntoDatum + Copy + 'a,
{
    fn into_datum(self) -> Option<pg_sys::Datum> {
        array_datum_from_iter(self.into_iter().copied())
    }

    fn type_oid() -> pg_sys::Oid {
        unsafe { pg_sys::get_array_type(T::type_oid()) }
    }

    #[inline]
    fn is_compatible_with(other: pg_sys::Oid) -> bool {
        Self::type_oid() == other || other == unsafe { pg_sys::get_array_type(T::type_oid()) }
    }
}

unsafe impl<T> SqlTranslatable for Array<'_, T>
where
    T: SqlTranslatable,
{
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        match T::argument_sql()? {
            SqlMapping::As(sql) => Ok(SqlMapping::As(format!("{sql}[]"))),
            SqlMapping::Skip => Err(ArgumentError::SkipInArray),
            SqlMapping::Composite { .. } => Ok(SqlMapping::Composite { array_brackets: true }),
        }
    }

    fn return_sql() -> Result<Returns, ReturnsError> {
        match T::return_sql()? {
            Returns::One(SqlMapping::As(sql)) => {
                Ok(Returns::One(SqlMapping::As(format!("{sql}[]"))))
            }
            Returns::One(SqlMapping::Composite { array_brackets: _ }) => {
                Ok(Returns::One(SqlMapping::Composite { array_brackets: true }))
            }
            Returns::One(SqlMapping::Skip) => Err(ReturnsError::SkipInArray),
            Returns::SetOf(_) => Err(ReturnsError::SetOfInArray),
            Returns::Table(_) => Err(ReturnsError::TableInArray),
        }
    }
}

unsafe impl<T> SqlTranslatable for VariadicArray<'_, T>
where
    T: SqlTranslatable,
{
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        match T::argument_sql()? {
            SqlMapping::As(sql) => Ok(SqlMapping::As(format!("{sql}[]"))),
            SqlMapping::Skip => Err(ArgumentError::SkipInArray),
            SqlMapping::Composite { .. } => Ok(SqlMapping::Composite { array_brackets: true }),
        }
    }

    fn return_sql() -> Result<Returns, ReturnsError> {
        match T::return_sql()? {
            Returns::One(SqlMapping::As(sql)) => {
                Ok(Returns::One(SqlMapping::As(format!("{sql}[]"))))
            }
            Returns::One(SqlMapping::Composite { array_brackets: _ }) => {
                Ok(Returns::One(SqlMapping::Composite { array_brackets: true }))
            }
            Returns::One(SqlMapping::Skip) => Err(ReturnsError::SkipInArray),
            Returns::SetOf(_) => Err(ReturnsError::SetOfInArray),
            Returns::Table(_) => Err(ReturnsError::TableInArray),
        }
    }

    fn variadic() -> bool {
        true
    }
}
