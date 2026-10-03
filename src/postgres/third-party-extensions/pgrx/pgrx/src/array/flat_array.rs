#![deny(missing_docs)]
use crate::datum::{Array, BorrowDatum, Datum};
use crate::layout::{Align, Layout};
use crate::memcx::{MemCx, OutOfMemory};
use crate::nullable::Nullable;
use crate::palloc::PBox;
use crate::pgrx_sql_entity_graph::metadata::{
    ArgumentError, ReturnsError, ReturnsRef, SqlMappingRef, SqlTranslatable, array_argument_sql,
    array_return_sql,
};
use crate::toast::{Toast, Toasty};
use crate::{layout, pg_sys, varlena};
use bitvec::ptr::{self as bitptr, BitPtr, BitPtrError, Const, Mut};
use bitvec::slice::{self as bitslice, BitSlice};
use core::iter::{ExactSizeIterator, FusedIterator};
use core::marker::PhantomData;
use core::{ffi, mem, ptr, slice};

use super::port;
use super::{Element, RawArray, Scalar};

/** `pg_sys::ArrayType` and its unsized varlena

# Safety
`&FlatArray<'_, T>` assumes its tail is the remainder of a Postgres array of element `T`.
*/
#[repr(C)]
#[derive(Debug)]
pub struct FlatArray<'mcx, T: ?Sized> {
    scalar: PhantomData<&'mcx T>,
    head: pg_sys::ArrayType,
    tail: [u8],
}

impl<'mcx, T> FlatArray<'mcx, T>
where
    T: ?Sized,
{
    fn as_raw(&self) -> RawArray {
        unsafe {
            let ptr = ptr::NonNull::new_unchecked(ptr::from_ref(self).cast_mut());
            RawArray::from_ptr(ptr.cast())
        }
    }

    /// Number of elements in the array, including nulls
    ///
    /// Note that for many arrays, this doesn't have a linear relationship with array byte-len.
    #[doc(alias = "cardinality")]
    pub fn nelems(&self) -> usize {
        self.as_raw().len()
    }

    /// Number of dimensions the array has
    ///
    /// This will be between `0` and `pg_sys::MAXDIM`.
    pub fn ndims(&self) -> usize {
        self.head.ndim as _
    }

    /// Does the array contain nulls?
    ///
    /// Note this is still `false` if the array has a null bitmap but no actual SQL-null elements.
    pub fn contains_nulls(&self) -> bool {
        // SAFETY: Constructive validity from ref and function is non-mutating
        unsafe { pg_sys::array_contains_nulls((&raw const self.head).cast_mut()) }
    }
}

// TODO: remove `non_exhaustive` when the errors have been worked out
/// Errors occurring when constructing fresh arrays
#[non_exhaustive]
#[derive(Debug)]
pub enum ArrayAllocError {
    /// Postgres has a maximum varlena size imit
    TooManyBytes,
    /// Postgres has a maximum array element limit
    TooManyElems,
    /// One or more dimensions are zero
    ZeroLenDim,
    /// There is insufficient memory in this context
    OutOfMemory,
}

const MAX_ALLOC_SIZE: usize = 0x3fffffff;
const MAX_ARRAY_SIZE: usize = MAX_ALLOC_SIZE / size_of::<pg_sys::Datum>();
const MAX_DIMS: usize = pg_sys::MAXDIM as usize;
// COMPAT: this has to be the last field of ArrayType
const _ARRAY_TYPE_IS_PADDING_FREE: () = assert!(
    size_of::<pg_sys::ArrayType>()
        == (mem::offset_of!(pg_sys::ArrayType, elemtype) + size_of::<pg_sys::Oid>())
);
const _MAX_ARRAY_SIZE_FITS_CINT_MAX: () = assert!(ffi::c_int::MAX as usize >= MAX_ARRAY_SIZE);
const _MAX_ALLOC_SIZE_FITS_CINT_MAX: () = assert!(ffi::c_int::MAX as usize >= MAX_ALLOC_SIZE);

impl<'mcx, T> FlatArray<'mcx, T>
where
    T: Scalar + Sized,
{
    /// Create a zeroed array with arbitrary dimensions for sized, zeroable elements
    pub fn new_zeroed_in<'cx, const N: usize>(
        dim_lens: [usize; N],
        has_nulls: bool,
        memcx: &MemCx<'cx>,
    ) -> Result<PBox<'cx, FlatArray<'cx, T>>, ArrayAllocError> {
        let base_size = size_of::<pg_sys::ArrayType>();

        let ndims = N;
        if N == 0 {
            return FlatArray::new_empty(memcx);
        }
        const { assert!(N <= MAX_DIMS) };

        let dims_size = size_of::<ffi::c_int>() * ndims;
        let mut dim_ints = [0 as ffi::c_int; N];
        for (&dsize, dint) in dim_lens.iter().zip(dim_ints.iter_mut()) {
            if dsize == 0 {
                return Err(ArrayAllocError::ZeroLenDim);
            } else {
                *dint = dsize as ffi::c_int;
            }
        }
        let mut product = 1 as ffi::c_int;
        let mut lbounds = [0 as ffi::c_int; N];
        for (&dim, lbound) in dim_lens.iter().zip(lbounds.iter_mut()) {
            // current lower bound is last product
            *lbound = product;
            // We handle the multiplication as usize, then use try_from to fit it down,
            // to avoid a risk of an unguarded overflow happening from casts
            product = if let Some(val) = dim.checked_mul(product as usize)
                && let Ok(val) = ffi::c_int::try_from(val)
            {
                val
            } else {
                return Err(ArrayAllocError::TooManyElems);
            };
        }
        let nelems = product as usize;
        if nelems > MAX_ARRAY_SIZE {
            return Err(ArrayAllocError::TooManyElems);
        }

        let null_size = if has_nulls { nelems.div_ceil(8) } else { 0 };

        let prefix_size = base_size + dims_size * 2 + null_size;
        const MAX_ELEM_ALIGN: usize = pg_sys::MAXIMUM_ALIGNOF as _;
        const { assert!(align_of::<T>() <= MAX_ELEM_ALIGN) };
        let prefix_size = prefix_size.next_multiple_of(MAX_ELEM_ALIGN);
        let size = prefix_size + size_of::<T>() * nelems;
        if size > MAX_ALLOC_SIZE {
            return Err(ArrayAllocError::TooManyBytes);
        }

        let dataoffset = if has_nulls { prefix_size as ffi::c_int } else { 0 };
        let elemtype = <T as Scalar>::OID;
        let tail_size = size - base_size;

        let ptr = alloc_zeroed_head(memcx, tail_size, ndims as i32, dataoffset, elemtype)?;
        // SAFETY: we allocated enough for our dimensions and lbounds
        unsafe {
            ptr.byte_add(base_size).cast().write(dim_ints);
            ptr.byte_add(base_size + dims_size).cast().write(lbounds);
        }

        // SAFETY: size of the metadata matches the bytes of the varlena header,
        // and there is no padding in ArrayType to make any offsets incorrect
        Ok(unsafe { PBox::from_raw_in(FlatArray::cast_tailed(ptr), memcx) })
    }

    /// Allocate a 0-dimension array
    pub fn new_empty<'cx>(
        memcx: &MemCx<'cx>,
    ) -> Result<PBox<'cx, FlatArray<'cx, T>>, ArrayAllocError> {
        let nbytes = mem::size_of::<pg_sys::ArrayType>();
        let ptr = alloc_zeroed_head(memcx, 0, 0, 0, <T as Scalar>::OID)?;
        // SAFETY: it's valid, if 0-dimensional
        Ok(unsafe { PBox::from_raw_in(FlatArray::cast_tailed(ptr), memcx) })
    }

    /// Allocate an array sized to fit a slice and copy it
    ///
    /// This produces a 0-dimension array if the slice has 0 length. Otherwise it is 1-dimensional.
    pub fn new_from_slice<'cx>(
        data: &[T],
        memcx: &MemCx<'cx>,
    ) -> Result<PBox<'cx, FlatArray<'cx, T>>, ArrayAllocError> {
        match data.len() {
            0 => FlatArray::new_empty(memcx),
            len => {
                let mut array = FlatArray::new_zeroed_in([len], false, memcx)?;
                array.as_non_null_slice_mut().unwrap().copy_from_slice(data);
                Ok(array)
            }
        }
    }
}

impl<'mcx, T> FlatArray<'mcx, T>
where
    T: ?Sized + Element,
{
    /// Iterate all elements of the array, including nulls
    #[doc(alias = "unnest")]
    pub fn iter(&self) -> ArrayIter<'_, T> {
        let nelems = self.nelems();
        let raw = self.as_raw();
        let nulls =
            raw.nulls_bitptr().map(|p| unsafe { bitslice::from_raw_parts(p, nelems).unwrap() });

        let data = unsafe { ptr::NonNull::new_unchecked(raw.data_ptr().cast_mut()) };
        let arr = self;
        let index = 0;
        let offset = 0;
        let align = Layout::lookup_oid(self.head.elemtype).align;

        ArrayIter { data, nulls, nelems, arr, index, offset, align }
    }

    /// Iterate only non-null elements of the array
    ///
    /// This is permissive and disregards SQL-null elements entirely rather than causing a panic.
    /// It can perform better in some cases by minimizing handling overhead for SQL nulls.
    pub fn iter_non_null(&self) -> impl Iterator<Item = &T> {
        // FIXME(perf): the performance note in the doc comment is currently unimplemented
        self.iter().filter_map(|elem| elem.into_option())
    }

    /// Borrow the nth element (0-indexed)
    ///
    /// `FlatArray::get` may have to iterate elements, so this is `O(n)` in the general case
    pub fn get(&self, index: usize) -> Option<Nullable<&T>> {
        // FIXME(perf): we can do better than iteration if T: Sized and the array is null-free
        self.iter().nth(index)
    }

    /// Obtain `&[T]` if the array has no nulls
    ///
    /// Note this treats the data as linear even if the array is multidimensional
    pub fn as_non_null_slice(&self) -> Option<&[T]>
    where
        T: Scalar,
    {
        if self.contains_nulls() {
            None
        } else {
            let raw = self.as_raw();
            // SAFETY: Sound if the bound of `T: Scalar` holds and the type fulfills those requirements
            Some(unsafe { slice::from_raw_parts(raw.data_ptr() as *const _, raw.len()) })
        }
    }

    /// Obtain `&mut [T]` if the array has no nulls
    ///
    /// Note this treats the data as linear even if the array is multidimensional
    pub fn as_non_null_slice_mut(&mut self) -> Option<&mut [T]>
    where
        T: Scalar,
    {
        if self.contains_nulls() {
            None
        } else {
            let elements = self.nelems();
            // SAFETY: We start with a valid ArrayType
            let data_ptr = unsafe { port::ARR_DATA_PTR(&raw mut self.head as _) };
            // SAFETY: Sound if the bound of `T: Scalar` holds and there are no nulls
            Some(unsafe { slice::from_raw_parts_mut(data_ptr.cast(), elements) })
        }
    }

    /// A byte slice containing the null bitmap
    pub fn nullbitmap_bytes(&self) -> Option<&[u8]> {
        let len = self.nelems().div_ceil(8);

        // SAFETY: This obtains the nulls pointer from a function that must either
        // return a null pointer or a pointer to a valid null bitmap.
        unsafe {
            let nulls_ptr = port::ARR_NULLBITMAP(ptr::addr_of!(self.head).cast_mut());
            ptr::slice_from_raw_parts(nulls_ptr, len).as_ref()
        }
    }
}

// Internal constructors
impl<'mcx, T> FlatArray<'mcx, T>
where
    T: ?Sized,
{
    fn cast_tailed(ptr: ptr::NonNull<[u8]>) -> ptr::NonNull<FlatArray<'mcx, T>> {
        // SAFETY: round-tripped from NonNull
        unsafe { ptr::NonNull::new_unchecked(ptr.as_ptr() as *mut FlatArray<_>) }
    }
}

fn alloc_zeroed_head(
    memcx: &MemCx<'_>,
    tail_size: usize,
    ndim: ffi::c_int,
    dataoffset: i32,
    elemtype: pg_sys::Oid,
) -> Result<ptr::NonNull<[u8]>, ArrayAllocError> {
    let nbytes = size_of::<pg_sys::ArrayType>() + tail_size;
    let ptr = memcx.alloc_zeroed_bytes(nbytes)?;
    // SAFETY: We allocated at least the ArrayType's prefix of bytes
    unsafe {
        // COMPAT: write ArrayType so fields must be initialized even if ArrayType changes
        // SAFETY: _ARRAY_TYPE_IS_PADDING_FREE means we will not deinitialize any bytes
        ptr.cast().write(pg_sys::ArrayType {
            vl_len_: varlena::encode_vlen_4b(nbytes as i32) as i32,
            dataoffset,
            ndim,
            elemtype,
        })
    }
    Ok(ptr::NonNull::slice_from_raw_parts(ptr, tail_size))
}

unsafe impl<T: ?Sized> BorrowDatum for FlatArray<'_, T> {
    const PASS: layout::PassBy = layout::PassBy::Ref;
    unsafe fn point_from(ptr: ptr::NonNull<u8>) -> ptr::NonNull<Self> {
        unsafe {
            let len =
                varlena::varsize_any(ptr.as_ptr().cast()) - mem::size_of::<pg_sys::ArrayType>();
            ptr::NonNull::new_unchecked(
                ptr::slice_from_raw_parts_mut(ptr.as_ptr(), len) as *mut Self
            )
        }
    }
}

/// `T[]` in Postgres
///
/// As an unsized type, this relies on `impl<T> SqlTranslatable for &T where T: SqlTranslatable`.
unsafe impl<T> SqlTranslatable for FlatArray<'_, T>
where
    T: ?Sized + SqlTranslatable + Element,
{
    const TYPE_IDENT: &'static str = T::TYPE_IDENT;
    const TYPE_ORIGIN: pgrx_sql_entity_graph::metadata::TypeOrigin = T::TYPE_ORIGIN;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> = array_argument_sql(T::ARGUMENT_SQL);
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> = array_return_sql(T::RETURN_SQL);
}

/// Iterator for arrays
#[derive(Clone)]
pub struct ArrayIter<'arr, T>
where
    T: ?Sized + Element,
{
    arr: &'arr FlatArray<'arr, T>,
    data: ptr::NonNull<u8>,
    nulls: Option<&'arr BitSlice<u8>>,
    nelems: usize,
    index: usize,
    offset: usize,
    align: Align,
}

impl<'arr, T> Iterator for ArrayIter<'arr, T>
where
    T: ?Sized + Element,
{
    type Item = Nullable<&'arr T>;

    fn next(&mut self) -> Option<Nullable<&'arr T>> {
        if self.index >= self.nelems {
            return None;
        }
        let is_null = match self.nulls {
            Some(nulls) => !nulls.get(self.index).unwrap(),
            None => false,
        };
        // note the index freezes when we reach the end, fusing the iterator
        self.index += 1;

        if is_null {
            // note that we do NOT offset when the value is a null!
            Some(Nullable::Null)
        } else {
            let borrow = unsafe { T::borrow_unchecked(self.data.add(self.offset)) };
            // As we always have a borrow, we just ask Rust what the array element's size is
            self.offset += self.align.pad(mem::size_of_val(borrow));
            Some(Nullable::Valid(borrow))
        }
    }
}

impl<'arr, 'mcx, T> IntoIterator for &'arr FlatArray<'mcx, T>
where
    T: ?Sized + Element,
{
    type IntoIter = ArrayIter<'arr, T>;
    type Item = Nullable<&'arr T>;
    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<'arr, T> ExactSizeIterator for ArrayIter<'arr, T> where T: ?Sized + Element {}
impl<'arr, T> FusedIterator for ArrayIter<'arr, T> where T: ?Sized + Element {}

impl From<OutOfMemory> for ArrayAllocError {
    fn from(value: OutOfMemory) -> Self {
        ArrayAllocError::OutOfMemory
    }
}
