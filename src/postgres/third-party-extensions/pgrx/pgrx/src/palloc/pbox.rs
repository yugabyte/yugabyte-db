use crate::callconv::{BoxRet, FcInfo};
use crate::datum::{BorrowDatum, Datum};
use crate::layout::PassBy;
use crate::memcx::{MemCx, OutOfMemory};
use crate::pg_sys;
use core::marker::PhantomData;
use core::ops::{Deref, DerefMut};
use core::ptr::NonNull;

use pgrx_sql_entity_graph::metadata::{
    ArgumentError, ReturnsError, ReturnsRef, SqlMappingRef, SqlTranslatable,
};

/** As [`Box<T, A>`][stdbox] where `A` is a [`MemCx`]


[stdbox]: alloc::boxed::Box
*/
#[repr(transparent)]
pub struct PBox<'mcx, T: ?Sized> {
    ptr: NonNull<T>,
    _cx: PhantomData<MemCx<'mcx>>,
}

impl<'mcx, T: ?Sized> PBox<'mcx, T> {
    // # Safety
    // The same constraints as [`Box::from_raw`], AND
    // - you assert the pointer was allocated in the `MemCx`
    // - you assert the pointer may be freed by `pfree`
    pub unsafe fn from_raw_in(ptr: NonNull<T>, _cx: &MemCx<'mcx>) -> PBox<'mcx, T> {
        PBox { ptr, _cx: PhantomData }
    }
}

impl<'mcx, T: Sized> PBox<'mcx, T> {
    #[track_caller]
    pub fn new_in(val: T, memcx: &MemCx<'mcx>) -> Self {
        PBox::try_new_in(val, memcx).unwrap()
    }

    pub fn try_new_in(val: T, memcx: &MemCx<'mcx>) -> Result<Self, OutOfMemory> {
        const { assert!(align_of::<T>() <= size_of::<pg_sys::Datum>()) };
        let ptr = memcx.alloc_bytes(size_of::<T>())?.cast();
        // SAFETY: We were guaranteed an appropriately sized allocation to write to,
        // and we have asserted our alignment maximum was upheld
        unsafe { ptr.write(val) };
        Ok(PBox { ptr, _cx: PhantomData })
    }
}

unsafe impl<'mcx, T> BoxRet for PBox<'mcx, T>
where
    T: ?Sized + BorrowDatum,
{
    unsafe fn box_into<'fcx>(self, fcinfo: &mut FcInfo<'fcx>) -> Datum<'fcx> {
        let datum = match T::PASS {
            PassBy::Value => {
                // start with a zeroed Datum, just to minimize funny business
                let mut datum = pg_sys::Datum::null();
                // SAFETY: Due to BorrowDatum, this type has a definite size less than a Datum,
                // and PBox must have an initialized pointee, so a copy is sound-by-construction.
                unsafe {
                    let size = size_of_val(&*self.ptr.as_ptr());
                    debug_assert!(size <= size_of::<pg_sys::Datum>());
                    // using `BorrowDatum::point_from` handles endianness
                    let datum_ptr = T::point_from(NonNull::from_mut(&mut datum).cast::<u8>());
                    datum_ptr.cast::<u8>().copy_from_nonoverlapping(self.ptr.cast(), size);
                }
                datum
            }
            PassBy::Ref => pg_sys::Datum::from(self.ptr.cast::<u8>().as_ptr()),
        };
        // SAFETY: by proxy, BorrowDatum is an `unsafe trait` so the above impl must be correct
        unsafe { fcinfo.return_raw_datum(datum) }
    }
}

/// SAFETY: SQL has no "pointers" so by-val and by-ref calling conventions are identical,
/// and all `PBox` truly does is enable pass-by-ref returns of unsized values.
unsafe impl<'mcx, T> SqlTranslatable for PBox<'mcx, T>
where
    T: SqlTranslatable + ?Sized,
{
    const TYPE_IDENT: &'static str = T::TYPE_IDENT;
    const TYPE_ORIGIN: pgrx_sql_entity_graph::metadata::TypeOrigin = T::TYPE_ORIGIN;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> = T::ARGUMENT_SQL;
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> = T::RETURN_SQL;
}

impl<'mcx, T> Deref for PBox<'mcx, T>
where
    T: BorrowDatum + ?Sized,
{
    type Target = T;

    fn deref(&self) -> &Self::Target {
        // SAFETY: by construction
        unsafe { self.ptr.as_ref() }
    }
}

impl<'mcx, T> DerefMut for PBox<'mcx, T>
where
    T: BorrowDatum + ?Sized,
{
    fn deref_mut(&mut self) -> &mut Self::Target {
        // SAFETY: by construction
        unsafe { self.ptr.as_mut() }
    }
}
