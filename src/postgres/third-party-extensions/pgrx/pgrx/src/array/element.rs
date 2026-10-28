use crate::callconv::DatumPass;
use crate::datum::BorrowDatum;
use crate::layout::PassBy;
use core::ptr;

/// `BorrowDatum` for array elements
///
/// # Safety
/// As BorrowDatum, due to the blanket implementation.
pub unsafe trait Element {
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
        unsafe { Element::point_from(ptr.cast()) }
    }

    /// Optimization for borrowing the referent
    unsafe fn borrow_unchecked<'dat>(ptr: ptr::NonNull<u8>) -> &'dat Self {
        unsafe { Element::point_from(ptr).as_ref() }
    }
}

unsafe impl<T> BorrowDatum for T
where
    T: ?Sized + Element + DatumPass,
{
    const PASS: PassBy = <T as DatumPass>::PASS;

    unsafe fn point_from(ptr: ptr::NonNull<u8>) -> std::ptr::NonNull<Self> {
        unsafe { Element::point_from(ptr) }
    }

    unsafe fn point_from_align4(ptr: ptr::NonNull<u32>) -> ptr::NonNull<Self> {
        unsafe { Element::point_from_align4(ptr.cast()) }
    }
}
