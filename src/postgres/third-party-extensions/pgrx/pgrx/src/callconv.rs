//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
#![deny(unsafe_op_in_unsafe_fn)]
//! Helper implementations for returning sets and tables from `#[pg_extern]`-style functions

use crate::datum::{
    AnyArray, AnyElement, AnyNumeric, Date, FromDatum, Inet, Internal, Interval, IntoDatum, Json,
    JsonB, Numeric, PgVarlena, Time, TimeWithTimeZone, Timestamp, TimestampWithTimeZone,
    UnboxDatum, Uuid,
};
use crate::datum::{BorrowDatum, Datum};
use crate::datum::{Range, RangeSubType};
use crate::heap_tuple::PgHeapTuple;
use crate::layout::PassBy;
use crate::nullable::Nullable;
use crate::pg_sys;
use crate::pgbox::*;
use crate::rel::PgRelation;
use crate::{PgBox, PgMemoryContexts};

use core::marker::PhantomData;
use core::{iter, mem, ptr, slice};
use std::ffi::{CStr, CString};
use std::ptr::NonNull;

struct ReadOnly<T>(T);

impl<T> ReadOnly<T> {
    unsafe fn refer_to(&self) -> &T {
        &self.0
    }
}

unsafe impl<T> Sync for ReadOnly<T> {}

static VIRTUAL_ARGUMENT: ReadOnly<pg_sys::NullableDatum> =
    ReadOnly(pg_sys::NullableDatum { value: pg_sys::Datum::null(), isnull: true });

/// How to pass a value from Postgres to Rust
///
/// This bound is necessary to distinguish things which can be passed into a `#[pg_extern] fn`.
/// This bound is not accurately described by FromDatum or similar traits, as value conversions are
/// handled in a special way at function argument boundaries and some types are never arguments,
/// due to Postgres not enforcing the type's described constraints.
///
/// In addition, this trait does more than traverse the Postgres boundary from caller to callee.
/// It allows requesting "virtual" arguments that are not part of the Postgres function arguments.
/// These virtual arguments typically have no representation in pgrx's generated SQL. Instead,
/// they can be found in the FunctionCallInfo or via various calls to Postgres on function entry.
/// When you include a virtual argument in the Rust function signature, acquiring the value is
/// handled automatically without you personally having to write the unsafe code.
///
/// This trait is exposed to external code so macro-generated wrapper fn may expand to calls to it.
/// The number of invariants implementers must uphold is unlikely to be adequately documented.
/// Prefer to use ArgAbi as a trait bound instead of implementing it, or even calling it, yourself.
pub unsafe trait ArgAbi<'fcx>: Sized {
    /// Unbox an argument with a matching type.
    ///
    /// # Safety
    /// - The argument's datum must have the matching "logical type" that can be "unboxed" from the
    ///   datum's object type.
    /// - Calling this with a null argument and a non-null type is *undefined behavior*.
    unsafe fn unbox_arg_unchecked(arg: Arg<'_, 'fcx>) -> Self;

    /// Unbox a nullable arg
    ///
    /// # Safety
    /// - The argument's datum must have the matching "logical type" that can be "unboxed" from the
    ///   datum's object type.
    /// - Calling this with a null argument and a non-null type is *undefined behavior*.
    ///
    /// # Note to implementers
    /// This function exists because Postgres conflates "SQL null" with "nullptr" in some cases.
    /// Override the default implementation for a by-reference type, returning `Nullable::Null`
    /// for nullptr.
    unsafe fn unbox_nullable_arg(arg: Arg<'_, 'fcx>) -> Nullable<Self> {
        if arg.is_null() {
            Nullable::Null
        } else {
            Nullable::Valid(unsafe { arg.unbox_unchecked() })
        }
    }

    /// "Is this a virtual arg?"
    fn is_virtual_arg() -> bool {
        false
    }
}

unsafe impl<'fcx, T> ArgAbi<'fcx> for crate::datum::Array<'fcx, T>
where
    T: ArgAbi<'fcx> + UnboxDatum,
{
    unsafe fn unbox_arg_unchecked(arg: Arg<'_, 'fcx>) -> Self {
        if arg.is_null() {
            panic!("{} was null", arg.1);
        }
        let raw_array = unsafe {
            crate::array::RawArray::detoast_from_varlena(
                NonNull::new(arg.2.value.cast_mut_ptr()).unwrap(),
            )
        };
        unsafe { crate::datum::Array::deconstruct_from(raw_array) }
    }
}

unsafe impl<'fcx, T> ArgAbi<'fcx> for crate::datum::VariadicArray<'fcx, T>
where
    T: ArgAbi<'fcx> + UnboxDatum,
{
    unsafe fn unbox_arg_unchecked(arg: Arg<'_, 'fcx>) -> Self {
        if arg.is_null() {
            panic!("{} was null", arg.1);
        }
        let raw_array = unsafe {
            crate::array::RawArray::detoast_from_varlena(
                NonNull::new(arg.2.value.cast_mut_ptr()).unwrap(),
            )
        };
        let array = unsafe { crate::datum::Array::deconstruct_from(raw_array) };
        crate::datum::VariadicArray::wrap_array(array)
    }
}

unsafe impl<'fcx, T> ArgAbi<'fcx> for crate::PgBox<T> {
    unsafe fn unbox_arg_unchecked(arg: Arg<'_, 'fcx>) -> Self {
        let index = arg.index();
        unsafe {
            Self::from_datum(arg.2.value, arg.is_null())
                .unwrap_or_else(|| panic!("argument {} must not be null", index))
        }
    }
}

unsafe impl<'fcx> ArgAbi<'fcx> for PgHeapTuple<'fcx, AllocatedByRust> {
    unsafe fn unbox_arg_unchecked(arg: Arg<'_, 'fcx>) -> Self {
        let index = arg.index();
        unsafe {
            FromDatum::from_datum(arg.2.value, arg.is_null())
                .unwrap_or_else(|| panic!("argument {} must not be null", index))
        }
    }
}

unsafe impl<'fcx, T> ArgAbi<'fcx> for Option<T>
where
    T: ArgAbi<'fcx>,
{
    unsafe fn unbox_arg_unchecked(arg: Arg<'_, 'fcx>) -> Self {
        if Self::is_virtual_arg() {
            unsafe { Some(T::unbox_arg_unchecked(arg)) }
        } else if arg.is_null() {
            None
        } else {
            unsafe { Some(T::unbox_nullable_arg(arg).into_option()).flatten() }
        }
    }

    fn is_virtual_arg() -> bool {
        T::is_virtual_arg()
    }
}

unsafe impl<'fcx, T> ArgAbi<'fcx> for Nullable<T>
where
    T: ArgAbi<'fcx>,
{
    unsafe fn unbox_arg_unchecked(arg: Arg<'_, 'fcx>) -> Self {
        unsafe { T::unbox_nullable_arg(arg) }
    }

    fn is_virtual_arg() -> bool {
        T::is_virtual_arg()
    }
}

unsafe impl<'fcx, T> ArgAbi<'fcx> for Range<T>
where
    T: FromDatum + RangeSubType,
{
    unsafe fn unbox_arg_unchecked(arg: Arg<'_, 'fcx>) -> Self {
        let index = arg.index();
        unsafe {
            arg.unbox_arg_using_from_datum()
                .unwrap_or_else(|| panic!("argument {} must not be null", index))
        }
    }
}

unsafe impl<'fcx, T> ArgAbi<'fcx> for Vec<T>
where
    for<'arr> T: UnboxDatum<As<'arr> = T> + FromDatum + 'arr,
{
    unsafe fn unbox_arg_unchecked(arg: Arg<'_, 'fcx>) -> Self {
        unsafe {
            if <T as FromDatum>::GET_TYPOID {
                Self::from_polymorphic_datum(arg.2.value, arg.is_null(), arg.raw_oid()).unwrap()
            } else {
                Self::from_datum(arg.2.value, arg.is_null()).unwrap()
            }
        }
    }
}

unsafe impl<'fcx, T> ArgAbi<'fcx> for Vec<Option<T>>
where
    for<'arr> T: UnboxDatum<As<'arr> = T> + FromDatum + 'arr,
{
    unsafe fn unbox_arg_unchecked(arg: Arg<'_, 'fcx>) -> Self {
        unsafe {
            if <T as FromDatum>::GET_TYPOID {
                Self::from_polymorphic_datum(arg.2.value, arg.is_null(), arg.raw_oid()).unwrap()
            } else {
                Self::from_datum(arg.2.value, arg.is_null()).unwrap()
            }
        }
    }
}

unsafe impl<'fcx, T: Copy> ArgAbi<'fcx> for PgVarlena<T> {
    unsafe fn unbox_arg_unchecked(arg: Arg<'_, 'fcx>) -> Self {
        unsafe { FromDatum::from_datum(arg.2.value, arg.is_null()).expect("unboxing pgvarlena") }
    }
}

unsafe impl<'fcx, const P: u32, const S: u32> ArgAbi<'fcx> for Numeric<P, S> {
    unsafe fn unbox_arg_unchecked(arg: Arg<'_, 'fcx>) -> Self {
        unsafe { arg.unbox_arg_using_from_datum().unwrap() }
    }
}

unsafe impl<'fcx> ArgAbi<'fcx> for pg_sys::FunctionCallInfo {
    unsafe fn unbox_arg_unchecked(arg: Arg<'_, 'fcx>) -> Self {
        unsafe { arg.0.as_mut_ptr() }
    }

    fn is_virtual_arg() -> bool {
        true
    }
}

macro_rules! argue_from_datum {
    ($lt:lifetime; $($unboxable:ty),*) => {
        $(unsafe impl<$lt> ArgAbi<$lt> for $unboxable {
            unsafe fn unbox_arg_unchecked(arg: Arg<'_, 'fcx>) -> Self {
                let index = arg.index();
                unsafe { arg.unbox_arg_using_from_datum().unwrap_or_else(|| panic!("argument {index} must not be null")) }
            }

            unsafe fn unbox_nullable_arg(arg: Arg<'_, 'fcx>) -> Nullable<Self> {
                unsafe { arg.unbox_arg_using_from_datum().into() }
            }
        })*
    };
}

argue_from_datum! { 'fcx; i8, i16, i32, i64, f32, f64, bool, char, String, Vec<u8> }
argue_from_datum! { 'fcx; Date, Interval, Time, TimeWithTimeZone, Timestamp, TimestampWithTimeZone }
argue_from_datum! { 'fcx; AnyArray, AnyElement, AnyNumeric }
argue_from_datum! { 'fcx; Inet, Internal, Json, JsonB, Uuid, PgRelation }
argue_from_datum! { 'fcx; pg_sys::BOX, pg_sys::ItemPointerData, pg_sys::Oid, pg_sys::Point, pg_sys::TransactionId }
// We could use the upcoming impl of ArgAbi for `&'fcx T where T: ?Sized + BorrowDatum`
// to support these types by implementing BorrowDatum for them also, but we reject this.
// It would greatly complicate other users of BorrowDatum like FlatArray, which want all impls
// of BorrowDatum to return a borrow of the entire pointee's len.
argue_from_datum! { 'fcx; &'fcx str, &'fcx [u8] }

unsafe impl<'fcx, T> ArgAbi<'fcx> for &'fcx T
where
    T: ?Sized + BorrowDatum,
{
    unsafe fn unbox_arg_unchecked(arg: Arg<'_, 'fcx>) -> &'fcx T {
        let ptr: *mut u8 = match T::PASS {
            PassBy::Ref => arg.2.value.cast_mut_ptr(),
            PassBy::Value => ptr::addr_of!(arg.0.raw_args()[arg.1].value).cast_mut().cast(),
        };
        unsafe {
            let ptr = ptr::NonNull::new_unchecked(ptr);
            T::borrow_unchecked(ptr)
        }
    }

    unsafe fn unbox_nullable_arg(arg: Arg<'_, 'fcx>) -> Nullable<Self> {
        let ptr: Option<ptr::NonNull<u8>> = NonNull::new(match T::PASS {
            PassBy::Ref => arg.2.value.cast_mut_ptr(),
            PassBy::Value => ptr::addr_of!(arg.0.raw_args()[arg.1].value).cast_mut().cast(),
        });
        match (arg.is_null(), ptr) {
            (true, _) | (false, None) => Nullable::Null,
            (false, Some(ptr)) => unsafe { Nullable::Valid(T::borrow_unchecked(ptr)) },
        }
    }
}

/// How to return a value from Rust to Postgres
///
/// This bound is necessary to distinguish things which can be returned from a `#[pg_extern] fn`.
/// This bound is not accurately described by IntoDatum or similar traits, as value conversions are
/// handled in a special way at function return boundaries, and may require mutating multiple fields
/// behind the FunctionCallInfo. The most exceptional case are set-returning functions, which
/// require special handling for the fcinfo and also for certain inner types.
///
/// This trait is exposed to external code so macro-generated wrapper fn may expand to calls to it.
/// The number of invariants implementers must uphold is unlikely to be adequately documented.
/// Prefer to use RetAbi as a trait bound instead of implementing it, or even calling it, yourself.
#[doc(hidden)]
pub unsafe trait RetAbi: Sized {
    /// Type returned to Postgres
    type Item: Sized;
    /// Driver for complex returns
    type Ret;

    /// Initialize the FunctionCallInfo for returns
    ///
    /// The implementer must pick the correct memory context for the wrapped fn's allocations.
    /// # Safety
    /// Requires a valid FunctionCallInfo.
    unsafe fn check_fcinfo_and_prepare(_fcinfo: pg_sys::FunctionCallInfo) -> CallCx {
        CallCx::WrappedFn(unsafe { pg_sys::YbCurrentMemoryContext })
    }

    fn check_and_prepare(fcinfo: &mut FcInfo<'_>) -> CallCx {
        unsafe { Self::check_fcinfo_and_prepare(fcinfo.0) }
    }

    /// answer what kind and how many returns happen from this type
    fn to_ret(self) -> Self::Ret;

    // move into the function context and obtain a Datum
    unsafe fn box_ret_in<'fcx>(fcinfo: &mut FcInfo<'fcx>, ret: Self::Ret) -> Datum<'fcx> {
        let fcinfo = fcinfo.0;
        unsafe { mem::transmute(Self::box_ret_in_fcinfo(fcinfo, ret)) }
    }

    /// box the return value
    /// # Safety
    /// must be called with a valid fcinfo
    unsafe fn box_ret_in_fcinfo(fcinfo: pg_sys::FunctionCallInfo, ret: Self::Ret) -> pg_sys::Datum;

    /// Multi-call types want to be in the fcinfo so they can be restored
    /// # Safety
    /// must be called with a valid fcinfo
    unsafe fn move_into_fcinfo_fcx(self, _fcinfo: pg_sys::FunctionCallInfo);

    /// Other types want to add metadata to the fcinfo
    /// # Safety
    /// must be called with a valid fcinfo
    unsafe fn fill_fcinfo_fcx(&self, _fcinfo: pg_sys::FunctionCallInfo);

    /// for multi-call types, how to restore them from the multi-call context
    ///
    /// for all others: panic
    /// # Safety
    /// must be called with a valid fcinfo
    unsafe fn ret_from_fcinfo_fcx(_fcinfo: pg_sys::FunctionCallInfo) -> Self::Ret {
        unimplemented!()
    }

    fn ret_from_fcx(fcinfo: &mut FcInfo<'_>) -> Self::Ret {
        let fcinfo = fcinfo.0;
        unsafe { Self::ret_from_fcinfo_fcx(fcinfo) }
    }

    /// must be called with a valid fcinfo
    unsafe fn finish_call_fcinfo(_fcinfo: pg_sys::FunctionCallInfo) {}
}

/// Simplified variant of *RetAbi*.
///
/// In order to handle all of the nuances of the calling convention for returning result sets from
/// Postgres functions, pgrx uses a very complicated trait, RetAbi. In practice, however, most
/// types do not need to think about its many sharp-edged cases. Instead, they should implement
/// this simplified trait, BoxRet. A blanket impl of RetAbi for BoxRet takes care of the rest.
pub unsafe trait BoxRet: Sized {
    /// # Safety
    /// You have to actually return the resulting Datum to the function.
    unsafe fn box_into<'fcx>(self, fcinfo: &mut FcInfo<'fcx>) -> Datum<'fcx>;
}

unsafe impl<T> RetAbi for T
where
    T: BoxRet,
{
    type Item = Self;
    type Ret = Self;

    fn to_ret(self) -> Self::Ret {
        self
    }

    unsafe fn box_ret_in_fcinfo(fcinfo: pg_sys::FunctionCallInfo, ret: Self::Ret) -> pg_sys::Datum {
        unsafe { ret.box_into(&mut FcInfo::from_ptr(fcinfo)) }.sans_lifetime()
    }

    unsafe fn box_ret_in<'fcx>(fcinfo: &mut FcInfo<'fcx>, ret: Self::Ret) -> Datum<'fcx> {
        unsafe { ret.box_into(fcinfo) }
    }

    unsafe fn check_fcinfo_and_prepare(_fcinfo: pg_sys::FunctionCallInfo) -> CallCx {
        CallCx::WrappedFn(unsafe { pg_sys::YbCurrentMemoryContext })
    }

    unsafe fn fill_fcinfo_fcx(&self, _fcinfo: pg_sys::FunctionCallInfo) {}
    unsafe fn move_into_fcinfo_fcx(self, _fcinfo: pg_sys::FunctionCallInfo) {}
    unsafe fn ret_from_fcinfo_fcx(_fcinfo: pg_sys::FunctionCallInfo) -> Self::Ret {
        unimplemented!()
    }
    unsafe fn finish_call_fcinfo(_fcinfo: pg_sys::FunctionCallInfo) {}
}

/// Control flow for RetAbi
#[doc(hidden)]
pub enum CallCx {
    RestoreCx,
    WrappedFn(pg_sys::MemoryContext),
}

unsafe impl<T> BoxRet for Option<T>
where
    T: BoxRet,
{
    unsafe fn box_into<'fcx>(self, fcinfo: &mut FcInfo<'fcx>) -> Datum<'fcx> {
        match self {
            Some(datum) => unsafe { datum.box_into(fcinfo) },
            None => fcinfo.return_null(),
        }
    }
}

unsafe impl<T> BoxRet for Nullable<T>
where
    T: BoxRet,
{
    unsafe fn box_into<'fcx>(self, fcinfo: &mut FcInfo<'fcx>) -> Datum<'fcx> {
        match self {
            Nullable::Valid(datum) => unsafe { datum.box_into(fcinfo) },
            Nullable::Null => fcinfo.return_null(),
        }
    }
}

unsafe impl<T, E> RetAbi for Result<T, E>
where
    T: RetAbi,
    T::Item: RetAbi,
    E: core::any::Any + core::fmt::Display,
{
    type Item = T::Item;
    type Ret = T::Ret;

    unsafe fn check_fcinfo_and_prepare(fcinfo: pg_sys::FunctionCallInfo) -> CallCx {
        unsafe { T::check_fcinfo_and_prepare(fcinfo) }
    }

    fn to_ret(self) -> Self::Ret {
        let value = pg_sys::panic::ErrorReportable::unwrap_or_report(self);
        value.to_ret()
    }

    unsafe fn box_ret_in_fcinfo(fcinfo: pg_sys::FunctionCallInfo, ret: Self::Ret) -> pg_sys::Datum {
        unsafe { T::box_ret_in_fcinfo(fcinfo, ret) }
    }

    unsafe fn fill_fcinfo_fcx(&self, fcinfo: pg_sys::FunctionCallInfo) {
        if let Ok(value) = self {
            unsafe { value.fill_fcinfo_fcx(fcinfo) }
        }
    }

    unsafe fn move_into_fcinfo_fcx(self, fcinfo: pg_sys::FunctionCallInfo) {
        if let Ok(value) = self {
            unsafe { value.move_into_fcinfo_fcx(fcinfo) }
        }
    }

    unsafe fn ret_from_fcinfo_fcx(fcinfo: pg_sys::FunctionCallInfo) -> Self::Ret {
        unsafe { T::ret_from_fcinfo_fcx(fcinfo) }
    }

    unsafe fn finish_call_fcinfo(fcinfo: pg_sys::FunctionCallInfo) {
        unsafe { T::finish_call_fcinfo(fcinfo) }
    }
}

macro_rules! return_packaging_for_primitives {
    ($($scalar:ty),*) => {
        $(  unsafe impl BoxRet for $scalar {
                unsafe fn box_into<'fcx>(self, fcinfo: &mut FcInfo<'fcx>) -> Datum<'fcx> {
                    unsafe { fcinfo.return_raw_datum($crate::pg_sys::Datum::from(self)) }
                }
            }
        )*
    }
}

return_packaging_for_primitives!(i8, i16, i32, i64, bool);

unsafe impl BoxRet for () {
    unsafe fn box_into<'fcx>(self, fcinfo: &mut FcInfo<'fcx>) -> Datum<'fcx> {
        unsafe { fcinfo.return_raw_datum(pg_sys::Datum::null()) }
    }
}

unsafe impl BoxRet for f32 {
    unsafe fn box_into<'fcx>(self, fcinfo: &mut FcInfo<'fcx>) -> Datum<'fcx> {
        unsafe { fcinfo.return_raw_datum(self.to_bits().into()) }
    }
}

unsafe impl BoxRet for f64 {
    unsafe fn box_into<'fcx>(self, fcinfo: &mut FcInfo<'fcx>) -> Datum<'fcx> {
        unsafe { fcinfo.return_raw_datum(self.to_bits().into()) }
    }
}

unsafe impl<'a> BoxRet for &'a [u8] {
    unsafe fn box_into<'fcx>(self, fcinfo: &mut FcInfo<'fcx>) -> Datum<'fcx> {
        match self.into_datum() {
            Some(datum) => unsafe { fcinfo.return_raw_datum(datum) },
            None => fcinfo.return_null(),
        }
    }
}

unsafe impl<'a> BoxRet for &'a str {
    unsafe fn box_into<'fcx>(self, fcinfo: &mut FcInfo<'fcx>) -> Datum<'fcx> {
        match self.into_datum() {
            Some(datum) => unsafe { fcinfo.return_raw_datum(datum) },
            None => fcinfo.return_null(),
        }
    }
}

unsafe impl<'a> BoxRet for &'a CStr {
    unsafe fn box_into<'fcx>(self, fcinfo: &mut FcInfo<'fcx>) -> Datum<'fcx> {
        match self.into_datum() {
            Some(datum) => unsafe { fcinfo.return_raw_datum(datum) },
            None => fcinfo.return_null(),
        }
    }
}

macro_rules! impl_repackage_into_datum {
    ($($boxable:ty),*) => {
        $(  unsafe impl BoxRet for $boxable {
                unsafe fn box_into<'fcx>(self, fcinfo: &mut FcInfo<'fcx>) -> Datum<'fcx> {
                    match self.into_datum() {
                        Some(datum) => unsafe { fcinfo.return_raw_datum(datum) },
                        None => fcinfo.return_null(),
                    }
                }
          })*
    };
}

impl_repackage_into_datum! {
    String, CString, Vec<u8>, char,
    Json, JsonB, Inet, Uuid, AnyNumeric, AnyArray, AnyElement, Internal,
    Date, Interval, Time, TimeWithTimeZone, Timestamp, TimestampWithTimeZone,
    pg_sys::BOX, pg_sys::ItemPointerData, pg_sys::Oid, pg_sys::Point, pg_sys::TransactionId
}

unsafe impl<const P: u32, const S: u32> BoxRet for Numeric<P, S> {
    unsafe fn box_into<'fcx>(self, fcinfo: &mut FcInfo<'fcx>) -> Datum<'fcx> {
        match self.into_datum() {
            Some(datum) => unsafe { fcinfo.return_raw_datum(datum) },
            None => fcinfo.return_null(),
        }
    }
}

unsafe impl<T> BoxRet for Range<T>
where
    T: IntoDatum + RangeSubType,
{
    unsafe fn box_into<'fcx>(self, fcinfo: &mut FcInfo<'fcx>) -> Datum<'fcx> {
        match self.into_datum() {
            Some(datum) => unsafe { fcinfo.return_raw_datum(datum) },
            None => fcinfo.return_null(),
        }
    }
}

unsafe impl<T> BoxRet for Vec<T>
where
    T: IntoDatum,
{
    unsafe fn box_into<'fcx>(self, fcinfo: &mut FcInfo<'fcx>) -> Datum<'fcx> {
        match self.into_datum() {
            Some(datum) => unsafe { fcinfo.return_raw_datum(datum) },
            None => fcinfo.return_null(),
        }
    }
}

unsafe impl<T: Copy> BoxRet for PgVarlena<T> {
    unsafe fn box_into<'fcx>(self, fcinfo: &mut FcInfo<'fcx>) -> Datum<'fcx> {
        match self.into_datum() {
            Some(datum) => unsafe { fcinfo.return_raw_datum(datum) },
            None => fcinfo.return_null(),
        }
    }
}

unsafe impl<'mcx, A> BoxRet for PgHeapTuple<'mcx, A>
where
    A: WhoAllocated,
{
    unsafe fn box_into<'fcx>(self, fcinfo: &mut FcInfo<'fcx>) -> Datum<'fcx> {
        match self.into_datum() {
            Some(datum) => unsafe { fcinfo.return_raw_datum(datum) },
            None => fcinfo.return_null(),
        }
    }
}

unsafe impl<T, A> BoxRet for PgBox<T, A>
where
    A: WhoAllocated,
{
    unsafe fn box_into<'fcx>(self, fcinfo: &mut FcInfo<'fcx>) -> Datum<'fcx> {
        match self.into_datum() {
            Some(datum) => unsafe { fcinfo.return_raw_datum(datum) },
            None => fcinfo.return_null(),
        }
    }
}

type FcInfoData = pg_sys::FunctionCallInfoBaseData;

#[derive(Clone)]
pub struct FcInfo<'fcx>(pgrx_pg_sys::FunctionCallInfo, PhantomData<&'fcx mut FcInfoData>);

// when talking about this, there's the lifetime for setreturningfunction, and then there's the current context's lifetime.
// Potentially <'srf, 'curr, 'ret: 'curr + 'srf> -> <'ret> but don't start with that.
// at first <'curr> or <'fcx>
// It's a heap-allocated stack frame for your function.
// 'fcx would apply to the arguments into this whole thing.
// PgHeapTuple is super not ideal and this enables a replacement of that.
// ArgAbi and unbox from fcinfo index
// Just this and the accessors, something that goes from raw_args(&'a self) -> &'fcx [NullableDatum]? &'a [NullableDatum]?
impl<'fcx> FcInfo<'fcx> {
    /// Constructor, used to wrap a raw FunctionCallInfo provided by Postgres.
    ///
    /// # Safety
    ///
    /// This function is unsafe as we cannot ensure the `fcinfo` argument is a valid
    /// [`pg_sys::FunctionCallInfo`] pointer.  This is your responsibility.
    #[inline]
    pub unsafe fn from_ptr(fcinfo: pg_sys::FunctionCallInfo) -> FcInfo<'fcx> {
        let _nullptr_check = NonNull::new(fcinfo).expect("fcinfo pointer must be non-null");
        Self(fcinfo, PhantomData)
    }
    /// Retrieve the arguments to this function call as a slice of [`pgrx_pg_sys::NullableDatum`]
    #[inline]
    pub fn raw_args(&self) -> &[pgrx_pg_sys::NullableDatum] {
        // Null pointer check already performed on immutable pointer
        // at construction time.
        unsafe {
            let arg_len = (*self.0).nargs;
            let args_ptr: *const pg_sys::NullableDatum = ptr::addr_of!((*self.0).args).cast();
            // A valid FcInfoWrapper constructed from a valid FuntionCallInfo should always have
            // at least nargs elements of NullableDatum.
            std::slice::from_raw_parts(args_ptr, arg_len as _)
        }
    }

    /// Retrieves the internal [`pg_sys::FunctionCallInfo`] wrapped by this type.
    /// A FunctionCallInfo is a mutable pointer to a FunctionCallInfoBaseData already, and so
    /// that type is sufficient.
    #[inline]
    pub unsafe fn as_mut_ptr(&self) -> pg_sys::FunctionCallInfo {
        self.0
    }

    /// Accessor for the "is null" flag
    ///
    /// # Safety
    /// If this flag is set to "false", then the resulting return must be a valid [`Datum`] for
    /// the function call's result type.
    pub unsafe fn set_return_is_null(&mut self) -> &mut bool {
        unsafe { &mut (*self.0).isnull }
    }

    /// Modifies the function call's return to be null
    ///
    /// Returns a null-pointer Datum for use as the calling function's return value.
    ///
    /// If this flag is set, regardless of what your function actually returns,
    /// Postgres will presume it is null and discard it. This means that, if you call this
    /// method and then return a value anyway, *your extension will leak memory.* Please
    /// ensure this method is only called for functions which, very definitely, return null.
    ///
    /// # Examples
    ///
    /// ```rust,no_run
    /// use pgrx::callconv::FcInfo;
    /// use pgrx::datum::Datum;
    /// use pgrx::prelude::*;
    /// fn foo<'a>(mut fcinfo: FcInfo<'a>) -> Datum<'a> {
    ///     unsafe { fcinfo.return_null() }
    /// }
    /// ```
    #[inline]
    pub fn return_null(&mut self) -> Datum<'fcx> {
        // SAFETY: returning null is always safe
        unsafe { *self.set_return_is_null() = true };
        Datum::null()
    }

    /// Set the return to be non-null and assign a raw Datum the correct lifetime.
    ///
    /// # Safety
    /// - If the function call expects a by-reference return, and the `pg_sys::Datum`
    ///   does not point to a valid object of the correct type, this is *undefined behavior*.
    /// - If the function call expects a by-reference return, and the `pg_sys::Datum` points
    ///   to an object that will not last for the caller's lifetime, this is *undefined behavior*.
    /// - If the function call expects a by-value return, and the `pg_sys::Datum` is not a valid
    ///   instance of that type when interpreted as such, this is **undefined behavior**.
    /// - If the return must be the SQL null, then calling this function is **undefined behavior**.
    ///
    /// Functionally, this can be thought of as a convenience for a [`mem::transmute`]
    /// from `pg_sys::Datum` to `pgrx::datum::Datum<'fcx>`. This gives it similar constraints.
    #[inline]
    pub unsafe fn return_raw_datum(&mut self, datum: pg_sys::Datum) -> Datum<'fcx> {
        // SAFETY: Caller asserts
        unsafe {
            *self.set_return_is_null() = false;
            mem::transmute(datum)
        }
    }

    /// Get the collation the function call should use.
    /// If the OID is 0 (invalid or no-type) this method will return None,
    /// otherwise it will return Some(oid).
    #[inline]
    pub fn get_collation(&self) -> Option<pg_sys::Oid> {
        // SAFETY: see FcInfo::from_ptr
        let fcinfo = unsafe { self.0.as_mut() }.unwrap();
        (fcinfo.fncollation.to_u32() != 0).then_some(fcinfo.fncollation)
    }

    /// Retrieve the type (as an Oid) of argument number `num`.
    /// In other words, the type of `self.raw_args()[num]`
    #[inline]
    pub fn get_arg_type(&self, num: usize) -> Option<pg_sys::Oid> {
        // SAFETY: see FcInfo::from_ptr
        unsafe {
            // bool::then() is lazy-evaluated, then_some is not.
            (num < ((*self.0).nargs as usize)).then(
                #[inline]
                || {
                    pg_sys::get_fn_expr_argtype(
                        self.0.as_ref().unwrap().flinfo,
                        num as std::os::raw::c_int,
                    )
                },
            )
        }
    }

    /// Retrieve the `.flinfo.fn_extra` pointer (as a PgBox'd type) from [`pg_sys::FunctionCallInfo`].
    /// If it was not already initialized, initialize it with `default`
    pub fn get_or_init_func_extra<DefaultValue: FnOnce() -> *mut pg_sys::FuncCallContext>(
        &self,
        default: DefaultValue,
    ) -> NonNull<pg_sys::FuncCallContext> {
        // Safety: User must supply a valid fcinfo to from_ptr() in order
        // to construct a FcInfo. If that constraint is maintained, this should
        // be safe.
        unsafe {
            let mut flinfo = NonNull::new((*self.0).flinfo).unwrap();
            if flinfo.as_ref().fn_extra.is_null() {
                flinfo.as_mut().fn_extra = PgMemoryContexts::For(flinfo.as_ref().fn_mcxt)
                    .leak_and_drop_on_delete(default())
                    as crate::void_mut_ptr;
            }

            // Safety: can new_unchecked() here because we just initialized it.
            NonNull::new_unchecked(flinfo.as_mut().fn_extra as *mut pg_sys::FuncCallContext)
        }
    }

    #[inline]
    pub fn srf_is_initialized(&self) -> bool {
        // Safety: User must supply a valid fcinfo to from_ptr() in order
        // to construct a FcInfo. If that constraint is maintained, this should
        // be safe.
        unsafe { !(*(*self.0).flinfo).fn_extra.is_null() }
    }

    /// Thin wrapper around [`pg_sys::init_MultiFuncCall`], made necessary
    /// because this structure's FunctionCallInfo is a private field.
    ///
    /// This should initialize `self.0.flinfo.fn_extra`
    #[inline]
    pub unsafe fn init_multi_func_call(&mut self) -> &'fcx mut pg_sys::FuncCallContext {
        unsafe {
            let fcx: *mut pg_sys::FuncCallContext = pg_sys::init_MultiFuncCall(self.0);
            debug_assert!(!fcx.is_null());
            &mut *fcx
        }
    }

    /// Equivalent to "per_MultiFuncCall" with no FFI cost, and a lifetime
    /// constraint.
    ///
    /// Safety: Assumes `self.0.flinfo.fn_extra` is non-null
    /// i.e. [`FcInfo::srf_is_initialized()`] would be `true`.
    #[inline]
    pub(crate) unsafe fn deref_fcx(&mut self) -> &'fcx mut pg_sys::FuncCallContext {
        unsafe {
            let fcx: *mut pg_sys::FuncCallContext = (*(*self.0).flinfo).fn_extra.cast();
            debug_assert!(!fcx.is_null());
            &mut *fcx
        }
    }

    /// Safety: Assumes `self.0.flinfo.fn_extra` is non-null
    /// i.e. [`FcInfo::srf_is_initialized()`] would be `true`.
    #[inline]
    pub unsafe fn srf_return_next(&mut self) {
        unsafe {
            self.deref_fcx().call_cntr += 1;
            self.get_result_info().set_is_done(pg_sys::ExprDoneCond::ExprMultipleResult);
        }
    }

    /// Safety: Assumes `self.0.flinfo.fn_extra` is non-null
    /// i.e. [`FcInfo::srf_is_initialized()`] would be `true`.
    #[inline]
    pub unsafe fn srf_return_done(&mut self) {
        unsafe {
            pg_sys::end_MultiFuncCall(self.0, self.deref_fcx());
            self.get_result_info().set_is_done(pg_sys::ExprDoneCond::ExprEndResult);
        }
    }

    /// # Safety
    /// Do not corrupt the `pg_sys::ReturnSetInfo` struct's data.
    #[inline]
    pub unsafe fn get_result_info(&self) -> ReturnSetInfoWrapper<'fcx> {
        unsafe {
            ReturnSetInfoWrapper::from_ptr((*self.0).resultinfo as *mut pg_sys::ReturnSetInfo)
        }
    }

    #[inline]
    pub fn args<'arg>(&'arg self) -> Args<'arg, 'fcx> {
        Args { iter: self.raw_args().iter().enumerate(), fcinfo: self }
    }
}

// TODO: rebadge this as AnyElement
pub struct Arg<'a, 'fcx>(&'a FcInfo<'fcx>, usize, &'a pg_sys::NullableDatum);

impl<'a, 'fcx> Arg<'a, 'fcx> {
    /// # Performance note
    /// This uses an FFI call to obtain the Oid, so avoid calling it if not necessary.
    pub fn raw_oid(&self) -> pg_sys::Oid {
        // we can just unwrap here because we know we were created using a valid index
        self.0.get_arg_type(self.1).unwrap()
    }

    #[doc(hidden)]
    /// # Safety
    /// The argument's datum must have the matching "logical type" that can be "unboxed" from the
    /// datum's object type.
    pub unsafe fn unbox_arg_using_from_datum<T: FromDatum>(self) -> Option<T> {
        unsafe {
            if <T as FromDatum>::GET_TYPOID {
                T::from_polymorphic_datum(self.2.value, self.is_null(), self.raw_oid())
            } else {
                T::from_datum(self.2.value, self.is_null())
            }
        }
    }

    /// # Safety
    /// - The argument's datum must have the matching "logical type" that can be "unboxed" from the
    ///   datum's object type.
    /// - If `T` cannot represent null arguments, calling this if the next arg is null is
    ///   *undefined behavior*.
    /// - If `T` is pass-by-reference, calling this if the next arg is null is *undefined behavior*.
    pub unsafe fn unbox_unchecked<T: ArgAbi<'fcx>>(self) -> T {
        unsafe { <T as ArgAbi<'fcx>>::unbox_arg_unchecked(self) }
    }

    pub fn is_null(&self) -> bool {
        self.2.isnull
    }

    pub fn index(&self) -> usize {
        self.1
    }
}

pub struct Args<'a, 'fcx> {
    iter: iter::Enumerate<slice::Iter<'a, pg_sys::NullableDatum>>,
    fcinfo: &'a FcInfo<'fcx>,
}

impl<'a, 'fcx> Iterator for Args<'a, 'fcx> {
    type Item = Arg<'a, 'fcx>;

    fn next(&mut self) -> Option<Self::Item> {
        self.iter.next().map(|(a, b)| Arg(self.fcinfo, a, b))
    }
}

impl<'a, 'fcx> Args<'a, 'fcx> {
    // generate an argument for use by virtual args
    fn synthesize_virtual_arg(&self) -> Arg<'a, 'fcx> {
        Arg(self.fcinfo, usize::MAX, unsafe { VIRTUAL_ARGUMENT.refer_to() })
    }

    /// # Safety
    /// - The argument's datum must have the matching "logical type" that can be "unboxed" from the
    ///   datum's object type.
    /// - In particular, if `T` cannot represent null arguments, calling this if the next arg is null is
    ///   *undefined behavior*.
    pub unsafe fn next_arg_unchecked<T: ArgAbi<'fcx>>(&mut self) -> Option<T> {
        if T::is_virtual_arg() {
            // SAFETY: trivial condition
            unsafe { Some(T::unbox_arg_unchecked(self.synthesize_virtual_arg())) }
        } else {
            // SAFETY: caller upholds
            unsafe { self.next().map(|next| T::unbox_arg_unchecked(next)) }
        }
    }

    /// # Safety
    /// - The argument's datum must have the matching "logical type" that can be "unboxed" from the
    ///   datum's object type.
    pub unsafe fn next_arg<T: ArgAbi<'fcx>>(&mut self) -> Option<Nullable<T>> {
        if T::is_virtual_arg() {
            // SAFETY: trivial condition
            unsafe { Some(T::unbox_nullable_arg(self.synthesize_virtual_arg())) }
        } else {
            // SAFETY: caller upholds
            unsafe { self.next().map(|next| T::unbox_nullable_arg(next)) }
        }
    }
}

#[derive(Clone)]
#[doc(hidden)]
pub struct ReturnSetInfoWrapper<'fcx>(
    *mut pg_sys::ReturnSetInfo,
    PhantomData<&'fcx mut pg_sys::ReturnSetInfo>,
);

impl<'fcx> ReturnSetInfoWrapper<'fcx> {
    /// Constructor, used to wrap a ReturnSetInfo provided by Postgres.
    ///
    /// # Safety
    ///
    /// This function is unsafe as we cannot ensure the `retinfo` argument is a valid
    /// [`pg_sys::ReturnSetInfo`] pointer.  This is your responsibility.
    #[inline]
    pub unsafe fn from_ptr(retinfo: *mut pg_sys::ReturnSetInfo) -> ReturnSetInfoWrapper<'fcx> {
        let _nullptr_check = NonNull::new(retinfo).expect("retinfo pointer must be non-null");
        Self(retinfo, PhantomData)
    }
    /*
    /* result status from function (but pre-initialized by caller): */
    SetFunctionReturnMode returnMode;	/* actual return mode */
    ExprDoneCond isDone;		/* status for ValuePerCall mode */
    /* fields filled by function in Materialize return mode: */
    Tuplestorestate *setResult; /* holds the complete returned tuple set */
    TupleDesc	setDesc;		/* actual descriptor for returned tuples */
    */
    // These four fields are, in-practice, owned by the callee.
    /// Status for ValuePerCall mode.
    pub fn set_is_done(&mut self, value: pg_sys::ExprDoneCond::Type) {
        unsafe {
            (*self.0).isDone = value;
        }
    }
    /// Status for ValuePerCall mode.
    pub fn get_is_done(&self) -> pg_sys::ExprDoneCond::Type {
        unsafe { (*self.0).isDone }
    }
    /// Actual return mode.
    pub fn set_return_mode(&mut self, return_mode: pgrx_pg_sys::SetFunctionReturnMode::Type) {
        unsafe {
            (*self.0).returnMode = return_mode;
        }
    }
    /// Actual return mode.
    pub fn get_return_mode(&self) -> pgrx_pg_sys::SetFunctionReturnMode::Type {
        unsafe { (*self.0).returnMode }
    }
    /// Holds the complete returned tuple set.
    pub fn set_tuple_result(&mut self, set_result: *mut pgrx_pg_sys::Tuplestorestate) {
        unsafe {
            (*self.0).setResult = set_result;
        }
    }
    /// Holds the complete returned tuple set.
    ///
    /// Safety: There is no guarantee this has been initialized.
    /// This may be a null pointer.
    pub fn get_tuple_result(&self) -> *mut pgrx_pg_sys::Tuplestorestate {
        unsafe { (*self.0).setResult }
    }

    /// Actual descriptor for returned tuples.
    pub fn set_tuple_desc(&mut self, desc: *mut pgrx_pg_sys::TupleDescData) {
        unsafe {
            (*self.0).setDesc = desc;
        }
    }

    /// Actual descriptor for returned tuples.
    pub fn get_tuple_desc(&mut self) -> *mut pgrx_pg_sys::TupleDescData {
        unsafe { (*self.0).setDesc }
    }
}
