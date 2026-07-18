use super::uuid::Uuid;
use super::Datum;
use crate::prelude::*;
use crate::varlena::{text_to_rust_str_unchecked, varlena_to_byte_slice};
use crate::{Json, JsonB};
use alloc::ffi::CString;
use core::ffi::CStr;

/// Directly convert a Datum into this type
///
/// Previously, pgrx used FromDatum exclusively, which captures conversion into T, but
/// leaves ambiguous a number of possibilities and allows large swathes of behavior to
/// be folded under a single trait. This provided certain beneficial ergonomics at first,
/// but eventually behavior was incorrectly folded under FromDatum, which provided the
/// illusion of soundness.
///
/// UnboxDatum should only be implemented for a type that CAN be directly converted from a Datum,
/// and it doesn't say whether it can be used directly or if it should be detoasted via MemCx.
/// It's currently just a possibly-temporary shim to make pgrx work.
///
/// # Safety
/// This trait is used to bound the lifetime of certain types: thus the associated type must be
/// this type but "infected" by the Datum's lifetime. By implementing this, you verify that you
/// are implementing this in the way that satisfies that lifetime constraint. There isn't really
/// a good way to constrain lifetimes correctly without forcing from-Datum types to go through a
/// wrapper type bound by the lifetime of the Datum. And what would you use as the bound, hmm?
pub unsafe trait UnboxDatum {
    // TODO: Currently, this doesn't actually get used to identify all the cases where the Datum
    // is actually a pointer type. However, it has been noted that Postgres does yield nullptr
    // on occasion, even when they say something is not supposed to be nullptr. As it is common
    // for Postgres to init [Datum<'_>] with palloc0, it is reasonable to assume nullptr is a risk,
    // even if `is_null == false`.
    //
    // Wait, what are you about, Jubilee? In some cases, the chance of nullness doesn't exist!
    // This is because we are materializing the datums from e.g. pointers to an Array, which
    // requires you to have had a valid base pointer into an ArrayType to start!
    // That's why you started using this goofy GAT scheme in the first place!
    /// Self with the lifetime `'src`
    ///
    /// The lifetime `'src` represents the lifetime of the "root" source of this Datum, which
    /// may be a memory context or a shorter-lived type inside that context.
    ///
    type As<'src>
    where
        Self: 'src;
    /// Convert from `Datum<'src>` to `T::As<'src>`
    ///
    /// This should be the direct conversion in each case. It is very unsafe to use directly.
    ///
    /// # Safety
    /// Due to the absence of an `is_null` parameter, this does not validate "SQL nullness" of
    /// the Datum in question. The intention is that this core fn eschews an additional branch.
    /// Just... don't use it if it might be null?
    ///
    /// This also should not be used as the primary conversion mechanism if it requires a MemCx,
    /// as this is intended primarily to be used in cases where the datum is guaranteed to be
    /// detoasted already.
    unsafe fn unbox<'src>(datum: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src;
}

macro_rules! unbox_int {
    ($($int_ty:ty),*) => {
        $(
            unsafe impl UnboxDatum for $int_ty {
                type As<'src> = $int_ty;
                unsafe fn unbox<'src>(datum: Datum<'src>) -> Self::As<'src> where Self: 'src {
                    datum.0.value() as $int_ty
                }
            }
        )*
    }
}

unbox_int! {
    i8, i16, i32, i64
}

unsafe impl UnboxDatum for bool {
    type As<'src> = bool;
    #[inline]
    unsafe fn unbox<'src>(datum: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        datum.0.value() != 0
    }
}

unsafe impl UnboxDatum for f32 {
    type As<'src> = f32;
    #[inline]
    unsafe fn unbox<'src>(datum: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        f32::from_bits(datum.0.value() as u32)
    }
}

unsafe impl UnboxDatum for f64 {
    type As<'src> = f64;
    #[inline]
    unsafe fn unbox<'src>(datum: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        f64::from_bits(datum.0.value() as u64)
    }
}

unsafe impl UnboxDatum for str {
    type As<'src> = &'src str;
    #[inline]
    unsafe fn unbox<'src>(datum: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        unsafe { text_to_rust_str_unchecked(datum.0.cast_mut_ptr()) }
    }
}

unsafe impl UnboxDatum for &str {
    #[rustfmt::skip]
    type As<'src> = &'src str where Self: 'src;
    #[inline]
    unsafe fn unbox<'src>(datum: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        unsafe { text_to_rust_str_unchecked(datum.0.cast_mut_ptr()) }
    }
}

unsafe impl UnboxDatum for CStr {
    type As<'src> = &'src CStr;
    #[inline]
    unsafe fn unbox<'src>(datum: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        unsafe { CStr::from_ptr(datum.0.cast_mut_ptr()) }
    }
}

unsafe impl UnboxDatum for &CStr {
    #[rustfmt::skip]
    type As<'src> = &'src CStr where Self: 'src;
    #[inline]
    unsafe fn unbox<'src>(datum: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        unsafe { CStr::from_ptr(datum.0.cast_mut_ptr()) }
    }
}

unsafe impl UnboxDatum for [u8] {
    type As<'src> = &'src [u8];
    #[inline]
    unsafe fn unbox<'src>(datum: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        unsafe { varlena_to_byte_slice(datum.0.cast_mut_ptr()) }
    }
}

unsafe impl UnboxDatum for &[u8] {
    #[rustfmt::skip]
    type As<'src> = &'src [u8] where Self: 'src;
    #[inline]
    unsafe fn unbox<'src>(datum: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        unsafe { varlena_to_byte_slice(datum.0.cast_mut_ptr()) }
    }
}

unsafe impl UnboxDatum for pg_sys::Oid {
    type As<'src> = pg_sys::Oid;
    #[inline]
    unsafe fn unbox<'src>(datum: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        pg_sys::Oid::from(datum.0.value() as u32)
    }
}

unsafe impl UnboxDatum for String {
    type As<'src> = String;
    #[inline]
    unsafe fn unbox<'src>(datum: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        unsafe { str::unbox(datum) }.to_owned()
    }
}

unsafe impl UnboxDatum for CString {
    type As<'src> = CString;
    #[inline]
    unsafe fn unbox<'src>(datum: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        unsafe { CStr::unbox(datum) }.to_owned()
    }
}

unsafe impl UnboxDatum for pg_sys::Datum {
    type As<'src> = pg_sys::Datum;
    #[inline]
    unsafe fn unbox<'src>(datum: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        datum.0
    }
}

unsafe impl UnboxDatum for Uuid {
    type As<'src> = Uuid;
    #[inline]
    unsafe fn unbox<'src>(datum: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        Uuid::from_bytes(datum.0.cast_mut_ptr::<[u8; 16]>().read())
    }
}

unsafe impl UnboxDatum for Date {
    type As<'src> = Date;
    #[inline]
    unsafe fn unbox<'src>(datum: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        Date::try_from(i32::unbox(datum)).unwrap_unchecked()
    }
}

unsafe impl UnboxDatum for Time {
    type As<'src> = Time;
    #[inline]
    unsafe fn unbox<'src>(datum: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        Time::try_from(i64::unbox(datum)).unwrap_unchecked()
    }
}

unsafe impl UnboxDatum for Timestamp {
    type As<'src> = Timestamp;
    #[inline]
    unsafe fn unbox<'src>(datum: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        Timestamp::try_from(i64::unbox(datum)).unwrap_unchecked()
    }
}

unsafe impl UnboxDatum for TimestampWithTimeZone {
    type As<'src> = TimestampWithTimeZone;
    #[inline]
    unsafe fn unbox<'src>(datum: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        TimestampWithTimeZone::try_from(i64::unbox(datum)).unwrap_unchecked()
    }
}

macro_rules! unbox_with_fromdatum {
    ($($from_ty:ty,)*) => {
        $(
            unsafe impl UnboxDatum for $from_ty {
                type As<'src> = $from_ty;
                unsafe fn unbox<'src>(datum: Datum<'src>) -> Self::As<'src> where Self: 'src {
                    Self::from_datum(datum.0, false).unwrap()
                }
            }
        )*
    }
}

unbox_with_fromdatum! {
    TimeWithTimeZone, AnyNumeric, char, pg_sys::Point, Interval, pg_sys::BOX, pg_sys::ItemPointerData,
}

unsafe impl UnboxDatum for PgHeapTuple<'_, crate::AllocatedByRust> {
    #[rustfmt::skip]
    type As<'src> = PgHeapTuple<'src, AllocatedByRust> where Self: 'src;
    #[inline]
    unsafe fn unbox<'src>(d: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        PgHeapTuple::from_datum(d.0, false).unwrap()
    }
}

unsafe impl<T: FromDatum + UnboxDatum> UnboxDatum for Array<'_, T> {
    #[rustfmt::skip]
    type As<'src> = Array<'src, T> where Self: 'src;
    unsafe fn unbox<'src>(d: Datum<'src>) -> Array<'src, T>
    where
        Self: 'src,
    {
        Array::from_datum(d.0, false).unwrap()
    }
}

unsafe impl<T: FromDatum + UnboxDatum> UnboxDatum for VariadicArray<'_, T> {
    #[rustfmt::skip]
    type As<'src> = VariadicArray<'src, T> where Self: 'src;
    unsafe fn unbox<'src>(d: Datum<'src>) -> VariadicArray<'src, T>
    where
        Self: 'src,
    {
        VariadicArray::from_datum(d.0, false).unwrap()
    }
}

unsafe impl<T: FromDatum + UnboxDatum + RangeSubType> UnboxDatum for Range<T> {
    #[rustfmt::skip]
    type As<'src> = Range<T> where Self: 'src;
    unsafe fn unbox<'src>(d: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        Range::<T>::from_datum(d.0, false).unwrap()
    }
}

unsafe impl<const P: u32, const S: u32> UnboxDatum for Numeric<P, S> {
    type As<'src> = Numeric<P, S>;
    #[inline]
    unsafe fn unbox<'src>(d: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        Numeric::from_datum(d.0, false).unwrap()
    }
}

unsafe impl<T> UnboxDatum for PgBox<T, AllocatedByPostgres> {
    #[rustfmt::skip]
    type As<'src> = PgBox<T> where Self: 'src;
    #[inline]
    unsafe fn unbox<'src>(d: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        PgBox::from_datum(d.0, false).unwrap()
    }
}

unsafe impl UnboxDatum for Json {
    #[rustfmt::skip]
    type As<'src> = Json where Self: 'src;
    #[inline]
    unsafe fn unbox<'src>(d: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        Json::from_datum(d.0, false).unwrap()
    }
}

unsafe impl UnboxDatum for JsonB {
    #[rustfmt::skip]
    type As<'src> = JsonB where Self: 'src;
    #[inline]
    unsafe fn unbox<'src>(d: Datum<'src>) -> Self::As<'src>
    where
        Self: 'src,
    {
        JsonB::from_datum(d.0, false).unwrap()
    }
}
