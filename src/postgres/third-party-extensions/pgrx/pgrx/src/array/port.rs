/*! Ported array macros and functions.
*/
#![allow(non_snake_case)]
use crate::pg_sys;
use core::{mem, ptr};

#[inline(always)]
pub(super) const fn TYPEALIGN(alignval: usize, len: usize) -> usize {
    // #define TYPEALIGN(ALIGNVAL,LEN)  \
    // (((uintptr_t) (LEN) + ((ALIGNVAL) - 1)) & ~((uintptr_t) ((ALIGNVAL) - 1)))
    (len + (alignval - 1)) & !(alignval - 1)
}

#[inline(always)]
pub(super) const fn MAXALIGN(len: usize) -> usize {
    // #define MAXALIGN(LEN) TYPEALIGN(MAXIMUM_ALIGNOF, (LEN))
    TYPEALIGN(pg_sys::MAXIMUM_ALIGNOF as _, len)
}

/// # Safety
/// Does a field access, but doesn't deref out of bounds of ArrayType
#[inline(always)]
pub(super) unsafe fn ARR_NDIM(a: *mut pg_sys::ArrayType) -> usize {
    // #define ARR_NDIM(a)				((a)->ndim)

    // SAFETY:  caller has asserted that `a` is a properly allocated ArrayType pointer
    unsafe { (*a).ndim as usize }
}

/// True if [the array *may* have nulls][array.h]
///
/// # Safety
/// Does a field access, but doesn't deref out of bounds of ArrayType
///
/// [array.h]: https://github.com/postgres/postgres/blob/c4bd6ff57c9a7b188cbd93855755f1029d7a5662/src/include/utils/array.h#L9
#[inline(always)]
pub(super) unsafe fn ARR_HASNULL(a: *mut pg_sys::ArrayType) -> bool {
    // #define ARR_HASNULL(a)			((a)->dataoffset != 0)

    // SAFETY:  caller has asserted that `a` is a properly allocated ArrayType pointer
    unsafe { (*a).dataoffset != 0 }
}

/// # Safety
/// Does a field access, but doesn't deref out of bounds of ArrayType
///
/// [`pg_sys::ArrayType`] is typically allocated past its size, and its somewhere in that region
/// that the returned pointer points, so don't attempt to `pfree` it.
#[inline(always)]
pub(super) const unsafe fn ARR_DIMS(a: *mut pg_sys::ArrayType) -> *mut i32 {
    // #define ARR_DIMS(a) \
    // ((int *) (((char *) (a)) + sizeof(ArrayType)))

    // SAFETY:  caller has asserted that `a` is a properly allocated ArrayType pointer
    unsafe { a.cast::<u8>().add(mem::size_of::<pg_sys::ArrayType>()).cast::<i32>() }
}

/// Returns the "null bitmap" of the specified array.  If there isn't one (the array contains no nulls)
/// then the null pointer is returned.
///
/// # Safety
/// Does a field access, but doesn't deref out of bounds of ArrayType.  The caller asserts that
/// `a` is a properly allocated [`pg_sys::ArrayType`]
///
/// [`pg_sys::ArrayType`] is typically allocated past its size, and its somewhere in that region
/// that the returned pointer points, so don't attempt to `pfree` it.
#[inline(always)]
pub(super) unsafe fn ARR_NULLBITMAP(a: *mut pg_sys::ArrayType) -> *mut pg_sys::bits8 {
    // #define ARR_NULLBITMAP(a) \
    // (ARR_HASNULL(a) ? \
    // (bits8 *) (((char *) (a)) + sizeof(ArrayType) + 2 * sizeof(int) * ARR_NDIM(a)) \
    // : (bits8 *) NULL)
    //

    // SAFETY:  caller has asserted that `a` is a properly allocated ArrayType pointer
    unsafe {
        if ARR_HASNULL(a) {
            a.cast::<u8>()
                .add(mem::size_of::<pg_sys::ArrayType>() + 2 * mem::size_of::<i32>() * ARR_NDIM(a))
        } else {
            ptr::null_mut()
        }
    }
}

/// The total array header size (in bytes) for an array with the specified
/// number of dimensions and total number of items.
#[inline(always)]
pub(super) const fn ARR_OVERHEAD_NONULLS(ndims: usize) -> usize {
    // #define ARR_OVERHEAD_NONULLS(ndims) \
    // MAXALIGN(sizeof(ArrayType) + 2 * sizeof(int) * (ndims))

    MAXALIGN(mem::size_of::<pg_sys::ArrayType>() + 2 * mem::size_of::<i32>() * ndims)
}

/// # Safety
/// Does a field access, but doesn't deref out of bounds of ArrayType.  The caller asserts that
/// `a` is a properly allocated [`pg_sys::ArrayType`]
#[inline(always)]
pub(super) unsafe fn ARR_DATA_OFFSET(a: *mut pg_sys::ArrayType) -> usize {
    // #define ARR_DATA_OFFSET(a) \
    // (ARR_HASNULL(a) ? (a)->dataoffset : ARR_OVERHEAD_NONULLS(ARR_NDIM(a)))

    // SAFETY:  caller has asserted that `a` is a properly allocated ArrayType pointer
    unsafe {
        if ARR_HASNULL(a) {
            (*a).dataoffset as _
        } else {
            ARR_OVERHEAD_NONULLS(ARR_NDIM(a))
        }
    }
}

/// Returns a pointer to the actual array data.
///
/// # Safety
/// Does a field access, but doesn't deref out of bounds of ArrayType.  The caller asserts that
/// `a` is a properly allocated [`pg_sys::ArrayType`]
///
/// [`pg_sys::ArrayType`] is typically allocated past its size, and its somewhere in that region
/// that the returned pointer points, so don't attempt to `pfree` it.
#[inline(always)]
pub(super) unsafe fn ARR_DATA_PTR(a: *mut pg_sys::ArrayType) -> *mut u8 {
    // #define ARR_DATA_PTR(a) \
    // (((char *) (a)) + ARR_DATA_OFFSET(a))

    unsafe { a.cast::<u8>().add(ARR_DATA_OFFSET(a)) }
}
