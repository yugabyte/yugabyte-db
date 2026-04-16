#![deny(unsafe_op_in_unsafe_fn)]
use super::*;
use crate::layout::PassBy;
use core::{ffi, mem, ptr};

/// Types which can be "borrowed from" [`&Datum<'_>`] via simple cast, deref, or slicing
///
/// # Safety
/// Despite its pleasant-sounding name, this implements a fairly low-level detail.
/// It exists to allow other code to use that nice-sounding BorrowDatum bound.
/// Outside of the pgrx library, it is probably incorrect to call and rely on this:
/// instead use convenience functions like `Datum::borrow_as`.
///
/// Its behavior is trusted for ABI details, and it should not be implemented if any doubt
/// exists of whether the type would be suitable for passing via Postgres.
pub unsafe trait BorrowDatum {
    /// The "native" passing convention for this type.
    ///
    /// - `PassBy::Value` implies [`mem::size_of<T>()`][size_of] <= [`mem::size_of::<Datum>()`][Datum].
    /// - `PassBy::Ref` means the pointee will occupy at least 1 byte for variable-sized types.
    ///
    /// Note that this means a zero-sized type is inappropriate for `BorrowDatum`.
    const PASS: PassBy;

    /// Cast a pointer to this blob of bytes to a pointer to this type.
    ///
    /// This is not a simple `ptr.cast()` because it may be *unsizing*, which may require
    /// reading varlena headers. For all fixed-size types, `ptr.cast()` should be correct.
    ///
    /// # Safety
    /// - This must be correctly invoked for the pointee type, as it may deref and read one or more
    ///   bytes in its implementation in order to read the inline metadata and unsize the type.
    /// - This must be invoked with a pointee initialized for the dynamically specified length.
    ///
    /// ## For Implementers
    /// Reading the **first** byte pointed to is permitted if `T::PASS = PassBy::Ref`, assuming you
    /// are implementing a varlena type. As the other dynamic length type, CStr also does this.
    /// This function
    /// - must NOT mutate the pointee
    /// - must point to the entire datum's length (`size_of_val` must not lose bytes)
    ///
    /// Do not attempt to handle pass-by-value versus pass-by-ref in this fn's body!
    /// A caller may be in a context where all types are handled by-reference, for instance.
    unsafe fn point_from(ptr: ptr::NonNull<u8>) -> ptr::NonNull<Self>;

    /// Cast a pointer to aligned varlena headers to this type
    ///
    /// This version allows you to assume the pointer is aligned to, and readable for, 4 bytes.
    /// This optimization is not required. When in doubt, avoid implementing it, and rely on your
    /// `point_from` implementation alone.
    ///
    /// # Safety
    /// - This must be correctly invoked for the pointee type, as it may deref.
    /// - This must be 4-byte aligned!
    unsafe fn point_from_align4(ptr: ptr::NonNull<u32>) -> ptr::NonNull<Self> {
        debug_assert!(ptr.is_aligned());
        unsafe { BorrowDatum::point_from(ptr.cast()) }
    }

    /// Optimization for borrowing the referent
    unsafe fn borrow_unchecked<'dat>(ptr: ptr::NonNull<u8>) -> &'dat Self {
        unsafe { BorrowDatum::point_from(ptr).as_ref() }
    }
}

/// From a pointer to a Datum, obtain a pointer to T's bytes
///
/// This may be None if T is PassBy::Ref
///
/// # Safety
/// Assumes the Datum is init
pub(crate) unsafe fn datum_ptr_to_bytes<T>(ptr: ptr::NonNull<Datum<'_>>) -> Option<ptr::NonNull<u8>>
where
    T: BorrowDatum,
{
    match T::PASS {
        // Ptr<Datum> casts to Ptr<T>
        PassBy::Value => Some(ptr.cast()),
        // Ptr<Datum> derefs to Datum which to Ptr
        PassBy::Ref => unsafe {
            let datum = ptr.read();
            ptr::NonNull::new(datum.sans_lifetime().cast_mut_ptr())
        },
    }
}

macro_rules! impl_borrow_fixed_len {
    ($($value_ty:ty),*) => {
        $(
            unsafe impl BorrowDatum for $value_ty {
                const PASS: PassBy = if mem::size_of::<Self>() <= mem::size_of::<Datum>() {
                    PassBy::Value
                } else {
                    PassBy::Ref
                };

                unsafe fn point_from(ptr: ptr::NonNull<u8>) -> ptr::NonNull<Self> {
                    ptr.cast()
                }
            }
        )*
    }
}

impl_borrow_fixed_len! {
    i8, i16, i32, i64, bool, f32, f64,
    pg_sys::Oid, pg_sys::Point,
    Date, Time, Timestamp, TimestampWithTimeZone
}

/// It is rare to pass CStr via Datums, but not unheard of
unsafe impl BorrowDatum for ffi::CStr {
    const PASS: PassBy = PassBy::Ref;

    unsafe fn point_from(ptr: ptr::NonNull<u8>) -> ptr::NonNull<Self> {
        let char_ptr: *mut ffi::c_char = ptr.as_ptr().cast();
        unsafe {
            let len = ffi::CStr::from_ptr(char_ptr).to_bytes_with_nul().len();
            ptr::NonNull::new_unchecked(ptr::slice_from_raw_parts_mut(char_ptr, len) as *mut Self)
        }
    }

    unsafe fn borrow_unchecked<'dat>(ptr: ptr::NonNull<u8>) -> &'dat Self {
        let char_ptr: *const ffi::c_char = ptr.as_ptr().cast();
        unsafe { ffi::CStr::from_ptr(char_ptr) }
    }
}
