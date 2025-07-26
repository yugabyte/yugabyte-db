//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
#![allow(clippy::precedence)]
use crate::datum::Array;
use crate::toast::{Toast, Toasty};
use crate::{layout, pg_sys, varlena};
use bitvec::ptr::{self as bitptr, BitPtr, BitPtrError, Mut};
use bitvec::slice::BitSlice;
use core::ptr::{self, NonNull};
use core::slice;

mod port;

/**
An aligned, dereferenceable `NonNull<ArrayType>` with low-level accessors.

It offers safe accessors to the fields of [pg_sys::ArrayType] and mostly-safe accessors
to the "dynamic fields" of the defined Postgres varlena array, but only requires validity
of ArrayType itself and the dimensions slice (always valid if `ndim == 0`).
This means the [NonNull] pointers that are returned may not be valid to read.
Validating the correctness of the entire array requires a bit more effort.

It is not Copy or Clone to make it slightly harder to misuse versus *mut ArrayType.
However, `&mut self` accessors do not give lifetimes to returned [`NonNull<[T]>`][nonnull]!
Instead, these are raw pointers, and `&mut RawArray` only makes `&RawArray` safer.

The reason RawArray works almost entirely with raw pointers is that
it is not currently valid to go from `&mut ArrayType` to `*mut ArrayType`,
take an offset beyond ArrayType's fields, and then create a new slice there
and read from that. The result is currently undefined behavior,
though with emphasis on "undefined": it may become defined in the future of Rust.

At the current moment, however, it is best to exercise an abundance of caution.

# On sizes and subscripts

Postgres uses C's `int` (`c_int` in Rust) for sizes, and Rust uses [usize].
Thus various functions of RawArray return `c_int` values, but you must convert to usize.
On 32-bit or 64-bit machines with 32-bit `c_int`s, you may losslessly upgrade `as usize`,
except with negative indices, which Postgres asserts against creating.
PGRX currently only intentionally supports 64-bit machines,
and while support for ILP32 or I64LP128 C data models may become possible,
PGRX will **not** support 16-bit machines in any practical case, even though Rust does.

# Detoasted

This type currently only implements functionality for interacting with a [detoasted] array (i.e.
that has been made contiguous and decompressed). This is a consequence of ArrayType having an
aligned varlena header, which will cause undefined behavior if it is interacted with while packed.

[nonnull]: NonNull
[detoasted]: https://www.postgresql.org/docs/current/storage-toast.html
*/
#[derive(Debug)]
pub struct RawArray {
    ptr: NonNull<pg_sys::ArrayType>,
}

#[deny(unsafe_op_in_unsafe_fn)]
impl RawArray {
    /**
    Returns a handle to the raw array header.

    # Safety

    When calling this method, you have to ensure that all of the following is true:
    * The pointer must be properly aligned.
    * It must be "dereferenceable" in the sense defined in [the std documentation].
    * The pointer must point to an initialized instance of [pg_sys::ArrayType].
    * The `ndim` field must be a correct value, or **0**, so `dims` is aligned and readable,
      or no data is actually read at all.
    * This is a unique, "owning pointer" for the varlena, so it won't be aliased while held,
      and it points to data in the Postgres ArrayType format.
    * The underlying ArrayType has been detoasted and is not stored in a compressed form.

    It should be noted that despite all these requirements, RawArray has no lifetime,
    nor produces slices with such, so it can still be racy and unsafe!

    [the std documentation]: core::ptr#safety
    */
    pub unsafe fn from_ptr(ptr: NonNull<pg_sys::ArrayType>) -> RawArray {
        RawArray { ptr }
    }

    pub(crate) unsafe fn detoast_from_varlena(stale: NonNull<pg_sys::varlena>) -> Toast<RawArray> {
        // SAFETY: Validity asserted by the caller.
        unsafe {
            let toast = NonNull::new(pg_sys::pg_detoast_datum(stale.as_ptr().cast())).unwrap();
            if stale == toast {
                Toast::Stale(RawArray::from_ptr(toast.cast()))
            } else {
                Toast::Fresh(RawArray::from_ptr(toast.cast()))
            }
        }
    }

    #[allow(dead_code)]
    pub(crate) unsafe fn deconstruct(
        &mut self,
        layout: layout::Layout,
    ) -> (*mut pg_sys::Datum, *mut bool) {
        let oid = self.oid();
        let array = self.ptr.as_ptr();

        // outvals for deconstruct_array
        let mut elements = core::ptr::null_mut();
        let mut nulls = core::ptr::null_mut();
        let mut nelems = 0;

        unsafe {
            pg_sys::deconstruct_array(
                array,
                oid,
                layout.size.as_typlen().into(),
                matches!(layout.pass, layout::PassBy::Value),
                layout.align.as_typalign(),
                &mut elements,
                &mut nulls,
                &mut nelems,
            );

            (elements, nulls)
        }
    }

    /// # Safety
    /// Array must have been made from an ArrayType pointer,
    /// or a null value, as-if [RawArray::from_ptr].
    pub unsafe fn from_array<T>(arr: Array<T>) -> Option<RawArray> {
        let array_type = arr.into_array_type() as *mut _;
        Some(RawArray { ptr: NonNull::new(array_type)? })
    }

    /// Returns the inner raw pointer to the ArrayType.
    #[inline]
    pub fn into_ptr(self) -> NonNull<pg_sys::ArrayType> {
        self.ptr
    }

    /// Get the number of dimensions.
    /// Will be in 0..=[pg_sys::MAXDIM].
    #[inline]
    fn ndim(&self) -> libc::c_int {
        // SAFETY: Validity asserted on construction.
        unsafe {
            (*self.ptr.as_ptr()).ndim
            /*
            FIXME: While this is a c_int, the max ndim is normally 6
            While the value can be set higher, it is... unlikely
            that it is going to actually challenge even 16-bit pointer widths.
            It would be preferable to return a usize instead,
            however, PGRX has trouble with that, unfortunately.
            */
            as _
        }
    }

    /**
    A slice of the dimensions.

    Oxidized form of [ARR_DIMS(ArrayType*)][ARR_DIMS].
    The length will be within 0..=[pg_sys::MAXDIM].

    Safe to use because validity of this slice was asserted on construction.

    [ARR_DIMS]: <https://git.postgresql.org/gitweb/?p=postgresql.git;a=blob;f=src/include/utils/array.h;h=4ae6c3be2f8b57afa38c19af2779f67c782e4efc;hb=278273ccbad27a8834dfdf11895da9cd91de4114#l287>
    */
    pub fn dims(&self) -> &[libc::c_int] {
        /*
        SAFETY: Welcome to the infernal bowels of FFI.
        Because the initial ptr was NonNull, we can assume this is also NonNull.
        Validity of the ptr and ndim field was asserted on construction of RawArray,
        so can assume the dims ptr is also valid, allowing making the slice.
        */
        unsafe {
            let ndim = self.ndim() as usize;
            slice::from_raw_parts(port::ARR_DIMS(self.ptr.as_ptr()), ndim)
        }
    }

    /// The flattened length of the array over every single element.
    /// Includes all items, even the ones that might be null.
    ///
    /// # Panics
    /// Panics if the Array's dimensions, multiplied together, exceed sizes Postgres can handle.
    #[inline]
    pub fn len(&self) -> usize {
        // Calculating the product mostly mirrors the Postgres implementation,
        // except we can use checked_mul instead of trying to cast to 64 bits and
        // hoping that doesn't also overflow on multiplication.
        // Also integer promotion doesn't real, so bitcast negatives.
        let dims = self.dims();
        if dims.is_empty() {
            0
        } else {
            // bindgen whiffs MaxArraySize AND MaxAllocSize!
            const MAX_ARRAY_SIZE: u32 = 0x3fffffff / 8;
            dims.iter()
                .map(|i| *i as u32) // treat negatives as huge
                .try_fold(1u32, |prod, d| prod.checked_mul(d))
                .filter(|prod| prod <= &MAX_ARRAY_SIZE)
                .expect("product of array dimensions must be < 2.pow(27)") as usize
        }
    }

    /// Accessor for ArrayType's elemtype.
    #[inline]
    pub fn oid(&self) -> pg_sys::Oid {
        // SAFETY: Validity asserted on construction.
        unsafe { (*self.ptr.as_ptr()).elemtype }
    }

    /// Gets the offset to the ArrayType's data.
    /// Should not be "taken literally".
    #[inline]
    fn data_offset(&self) -> i32 {
        // SAFETY: Validity asserted on construction.
        unsafe { (*self.ptr.as_ptr()).dataoffset }
        // This field is an "int32" in Postgres
    }

    /**
    Equivalent to [ARR_HASNULL(ArrayType*)][ARR_HASNULL].

    Note this means that it only asserts that there MIGHT be a null

    [ARR_HASNULL]: <https://git.postgresql.org/gitweb/?p=postgresql.git;a=blob;f=src/include/utils/array.h;h=4ae6c3be2f8b57afa38c19af2779f67c782e4efc;hb=278273ccbad27a8834dfdf11895da9cd91de4114#l284>
    */
    #[allow(unused)]
    fn nullable(&self) -> bool {
        self.data_offset() != 0
    }

    /// May return null.
    #[inline]
    fn nulls_mut_ptr(&mut self) -> *mut u8 {
        // SAFETY: This isn't public for a reason: it's a maybe-null *mut BitSlice, which is easy to misuse.
        // Obtaining it, however, is perfectly safe.
        unsafe { port::ARR_NULLBITMAP(self.ptr.as_ptr()) }
    }

    #[inline]
    fn nulls_bitptr(&mut self) -> Option<BitPtr<Mut, u8>> {
        match BitPtr::try_from(self.nulls_mut_ptr()) {
            Ok(ptr) => Some(ptr),
            Err(BitPtrError::Null(_)) => None,
            Err(BitPtrError::Misaligned(_)) => unreachable!("impossible to misalign *mut u8"),
        }
    }

    /**
    Oxidized form of [ARR_NULLBITMAP(ArrayType*)][ARR_NULLBITMAP]

    If this returns None, the array cannot have nulls.
    If this returns Some, it points to the bitslice that marks nulls in this array.

    Note that unlike the `is_null: bool` that appears elsewhere, here a 0 bit is null,
    or possibly out of bounds for the final byte of the bitslice.

    Note that if this is None, that does not mean it's always okay to read!
    If len is 0, then this slice will be valid for 0 bytes.

    [ARR_NULLBITMAP]: <https://git.postgresql.org/gitweb/?p=postgresql.git;a=blob;f=src/include/utils/array.h;h=4ae6c3be2f8b57afa38c19af2779f67c782e4efc;hb=278273ccbad27a8834dfdf11895da9cd91de4114#l293>
    */
    pub fn nulls(&mut self) -> Option<NonNull<[u8]>> {
        let len = self.len() + 7 >> 3; // Obtains 0 if len was 0.

        NonNull::new(ptr::slice_from_raw_parts_mut(self.nulls_mut_ptr(), len))
    }

    /**
    The [bitvec] equivalent of [RawArray::nulls].
    If this returns `None`, the array cannot have nulls.
    If this returns `Some`, it points to the bitslice that marks nulls in this array.

    Note that unlike the `is_null: bool` that appears elsewhere, here a 0 bit is null.
    Unlike [RawArray::nulls], this slice is bit-exact in length, so there are no caveats for safely-used BitSlices.

    [bitvec]: https://docs.rs/bitvec/latest
    [BitPtrError::Null]: <https://docs.rs/bitvec/latest/bitvec/ptr/enum.BitPtrError.html>
    [ARR_NULLBITMAP]: <https://git.postgresql.org/gitweb/?p=postgresql.git;a=blob;f=src/include/utils/array.h;h=4ae6c3be2f8b57afa38c19af2779f67c782e4efc;hb=278273ccbad27a8834dfdf11895da9cd91de4114#l293>
    */
    pub fn nulls_bitslice(&mut self) -> Option<NonNull<BitSlice<u8>>> {
        NonNull::new(bitptr::bitslice_from_raw_parts_mut(self.nulls_bitptr()?, self.len()))
    }

    /**
    Checks the array for any NULL values by assuming it is a proper varlena array,

    # Safety

    * This requires every index is valid to read or correctly marked as null.
    */
    pub unsafe fn any_nulls(&self) -> bool {
        // SAFETY: Caller asserted safety conditions.
        unsafe { pg_sys::array_contains_nulls(self.ptr.as_ptr()) }
    }

    /**
    Oxidized form of [ARR_DATA_PTR(ArrayType*)][ARR_DATA_PTR]

    # Safety

    While this function is safe to call, using the slice may risk undefined behavior.
    The raw slice is not guaranteed to be legible at any given index as T,
    e.g. it may be an "SQL null" if so indicated in the null bitmap.
    As a result, it is dangerous to reborrow this as `&[T]` or `&mut [T]`
    unless the type considers all bitpatterns to be valid values.

    That is the primary reason this returns [`NonNull<[T]>`][nonnull]. If it returned `&mut [T]`,
    then for many possible types that can be **undefined behavior**,
    as it would assert each particular index was a valid `T`.
    A Rust borrow, including of a slice, will always be
    * non-null
    * aligned
    * **validly initialized**, except in the case of [MaybeUninit] types

    It is reasonable to assume data Postgres exposes logically to SQL is initialized,
    but it may be incorrect to assume data Postgres has marked "null"
    otherwise follows Rust-level initialization requirements.

    As Postgres handles alignment requirements in its own particular ways,
    it is up to you to validate that each index is aligned correctly.
    The first element should be correctly aligned to the type, but that is not certain.
    Successive indices are even less likely to match the data type you want
    unless Postgres also uses an identical layout.

    This returns a slice to make it somewhat harder to fail to read it correctly.
    However, it should be noted that a len 0 slice may not be read via raw pointers.

    [MaybeUninit]: core::mem::MaybeUninit
    [nonnull]: NonNull
    [ARR_DATA_PTR]: <https://git.postgresql.org/gitweb/?p=postgresql.git;a=blob;f=src/include/utils/array.h;h=4ae6c3be2f8b57afa38c19af2779f67c782e4efc;hb=278273ccbad27a8834dfdf11895da9cd91de4114#l315>
    */
    pub fn data<T>(&mut self) -> NonNull<[T]> {
        /*
        SAFETY: Welcome to the infernal bowels of FFI.
        Because the initial ptr was NonNull, we can assume this is also NonNull.
        As validity of the initial ptr was asserted on construction of RawArray,
        this can assume the data ptr is also valid, or harmlessly incorrect.

        This code doesn't assert validity per se, but in practice,
        the caller may immediately turn this into a borrowed slice,
        opening up the methods that are available on borrowed slices.
        This is fine as long as the caller heeds the caveats already given.
        In particular, for simply sized and aligned data, where alignment is the size
        (e.g. u8, i16, f32, u64), and there are no invalid bitpatterns to worry about,
        the caller can almost certainly go to town with it,
        needing only their initial assertion regarding the type being correct.
        */
        unsafe {
            NonNull::new_unchecked(ptr::slice_from_raw_parts_mut(
                port::ARR_DATA_PTR(self.ptr.as_ptr()).cast(),
                self.len(),
            ))
        }
    }

    #[inline]
    pub(crate) fn data_ptr(&self) -> *const u8 {
        unsafe { port::ARR_DATA_PTR(self.ptr.as_ptr()) }
    }

    /// "one past the end" pointer for the entire array's bytes
    pub(crate) fn end_ptr(&self) -> *const u8 {
        let ptr = self.ptr.as_ptr().cast::<u8>();
        ptr.wrapping_add(unsafe { varlena::varsize_any(ptr.cast()) })
    }
}

impl Toasty for RawArray {
    unsafe fn drop_toast(&mut self) {
        unsafe { pg_sys::pfree(self.ptr.as_ptr().cast()) }
    }
}
