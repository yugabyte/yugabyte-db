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
use core::{iter, ptr};

use crate::callconv::{BoxRet, CallCx, RetAbi};
use crate::fcinfo::{pg_return_null, srf_is_first_call, srf_return_done, srf_return_next};
use crate::ptr::PointerExt;
use crate::{pg_sys, IntoDatum, IntoHeapTuple, PgMemoryContexts};
use pgrx_sql_entity_graph::metadata::{
    ArgumentError, Returns, ReturnsError, SqlMapping, SqlTranslatable,
};

/// Support for returning a `SETOF T` from an SQL function.
///
/// [`SetOfIterator`] is typically used as a return type on `#[pg_extern]`-style functions
/// and indicates that the SQL function should return a `SETOF` over the generic argument `T`.
///
/// It is a lightweight wrapper around an iterator, which you provide during construction.  The
/// iterator *can* borrow from its environment, following Rust's normal borrowing rules.  If no
/// borrowing is necessary, the `'static` lifetime should be used.
///
/// # Examples
///
/// This example simply returns a set of integers in the range `1..=5`.
///
/// ```rust,no_run
/// use pgrx::prelude::*;
/// #[pg_extern]
/// fn return_ints() -> SetOfIterator<'static, i32> {
///     SetOfIterator::new(1..=5)
/// }
/// ```
///
/// Here we return a set of `&str`s, borrowed from an argument:
///
/// ```rust,no_run
/// use pgrx::prelude::*;
/// #[pg_extern]
/// fn split_string<'a>(input: &'a str) -> SetOfIterator<'a, &'a str> {
///     SetOfIterator::new(input.split_whitespace())
/// }
/// ```
#[repr(transparent)]
pub struct SetOfIterator<'a, T>(
    // Postgres uses the same ABI for `returns setof` and 1-column `returns table`
    TableIterator<'a, (T,)>,
);

impl<'a, T: 'a> SetOfIterator<'a, T> {
    pub fn new(iter: impl IntoIterator<Item = T> + 'a) -> Self {
        // Forward the impl using remapping to minimize `unsafe` code and keep them in sync
        Self(TableIterator::new(iter.into_iter().map(|c| (c,))))
    }

    pub fn empty() -> Self {
        Self::new(iter::empty())
    }

    pub fn once(value: T) -> Self {
        Self::new(iter::once(value))
    }
}

impl<'a, T> Iterator for SetOfIterator<'a, T> {
    type Item = T;

    #[inline]
    fn next(&mut self) -> Option<Self::Item> {
        self.0.next().map(|(val,)| val)
    }
}

/// `SetOfIterator<'_, T>` differs from `TableIterator<'a, (T,)>` in generated SQL
unsafe impl<'a, T> SqlTranslatable for SetOfIterator<'a, T>
where
    T: SqlTranslatable,
{
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        T::argument_sql()
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        match T::return_sql() {
            Ok(Returns::One(sql)) => Ok(Returns::SetOf(sql)),
            Ok(Returns::SetOf(_)) => Err(ReturnsError::NestedSetOf),
            Ok(Returns::Table(_)) => Err(ReturnsError::SetOfContainingTable),
            err @ Err(_) => err,
        }
    }
}

/// Support for a `TABLE (...)` from an SQL function.
///
/// [`TableIterator`] is typically used as the return type of a `#[pg_extern]`-style function,
/// indicating that the function returns a table of named columns.  [`TableIterator`] is
/// generic over `Row`, but that `Row` must be a Rust tuple containing one or more elements.  They
/// must also be "named" using pgrx's [`name!`][crate::name] macro.  See the examples below.
///
/// It is a lightweight wrapper around an iterator, which you provide during construction.  The
/// iterator *can* borrow from its environment, following Rust's normal borrowing rules.  If no
/// borrowing is necessary, the `'static` lifetime should be used.
///
/// # Examples
///
/// This example returns a table of employee information.
///
/// ```rust,no_run
/// use pgrx::prelude::*;
/// #[pg_extern]
/// fn employees() -> TableIterator<'static,
///         (
///             name!(id, i64),
///             name!(dept_code, String),
///             name!(full_text, &'static str)
///         )
/// > {
///     TableIterator::new(vec![
///         (42, "ARQ".into(), "John Hammond"),
///         (87, "EGA".into(), "Mary Kinson"),
///         (3,  "BLA".into(), "Perry Johnson"),
///     ])
/// }
/// ```
///
/// And here we return a simple numbered list of words, borrowed from the input `&str`.
///
/// ```rust,no_run
/// use pgrx::prelude::*;
/// #[pg_extern]
/// fn split_string<'a>(input: &'a str) -> TableIterator<'a, ( name!(num, i32), name!(word, &'a str) )> {
///     TableIterator::new(input.split_whitespace().enumerate().map(|(n, w)| (n as i32, w)))
/// }
/// ```
pub struct TableIterator<'a, Row> {
    iter: Box<dyn Iterator<Item = Row> + 'a>,
}

impl<'a, Row: 'a> TableIterator<'a, Row> {
    pub fn new(iter: impl IntoIterator<Item = Row> + 'a) -> Self {
        Self { iter: Box::new(iter.into_iter()) }
    }

    pub fn empty() -> Self {
        Self::new(iter::empty())
    }

    pub fn once(value: Row) -> Self {
        Self::new(iter::once(value))
    }
}

impl<'a, Row> Iterator for TableIterator<'a, Row> {
    type Item = Row;

    #[inline]
    fn next(&mut self) -> Option<Self::Item> {
        self.iter.next()
    }
}

unsafe impl<'iter, C> SqlTranslatable for TableIterator<'iter, (C,)>
where
    C: SqlTranslatable + 'iter,
{
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Err(ArgumentError::Table)
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        let vec = vec![C::return_sql().and_then(|sql| match sql {
            Returns::One(sql) => Ok(sql),
            Returns::SetOf(_) => Err(ReturnsError::TableContainingSetOf),
            Returns::Table(_) => Err(ReturnsError::NestedTable),
        })?];
        Ok(Returns::Table(vec))
    }
}

unsafe impl<'a, T> RetAbi for SetOfIterator<'a, T>
where
    T: BoxRet,
{
    type Item = <Self as Iterator>::Item;
    type Ret = IterRet<Self>;

    unsafe fn check_fcinfo_and_prepare(fcinfo: pg_sys::FunctionCallInfo) -> CallCx {
        unsafe { TableIterator::<(T,)>::check_fcinfo_and_prepare(fcinfo) }
    }

    fn to_ret(self) -> Self::Ret {
        let mut iter = self;
        IterRet(match iter.next() {
            None => Step::Done,
            Some(value) => Step::Init(iter, value),
        })
    }

    unsafe fn box_ret_in_fcinfo(fcinfo: pg_sys::FunctionCallInfo, ret: Self::Ret) -> pg_sys::Datum {
        let ret = match ret.0 {
            Step::Done => Step::Done,
            Step::Once(value) => Step::Once((value,)),
            Step::Init(iter, value) => Step::Init(iter.0, (value,)),
        };
        unsafe { TableIterator::<(T,)>::box_ret_in_fcinfo(fcinfo, IterRet(ret)) }
    }

    unsafe fn fill_fcinfo_fcx(&self, _fcinfo: pg_sys::FunctionCallInfo) {}

    unsafe fn move_into_fcinfo_fcx(self, fcinfo: pg_sys::FunctionCallInfo) {
        unsafe { self.0.move_into_fcinfo_fcx(fcinfo) }
    }

    unsafe fn ret_from_fcinfo_fcx(fcinfo: pg_sys::FunctionCallInfo) -> Self::Ret {
        let step = match unsafe { TableIterator::<(T,)>::ret_from_fcinfo_fcx(fcinfo).0 } {
            Step::Done => Step::Done,
            Step::Once((item,)) => Step::Once(item),
            Step::Init(iter, (value,)) => Step::Init(Self(iter), value),
        };
        IterRet(step)
    }

    unsafe fn finish_call_fcinfo(fcinfo: pg_sys::FunctionCallInfo) {
        unsafe { TableIterator::<(T,)>::finish_call_fcinfo(fcinfo) }
    }
}

unsafe impl<'a, Row> RetAbi for TableIterator<'a, Row>
where
    Row: RetAbi,
{
    type Item = <Self as Iterator>::Item;
    type Ret = IterRet<Self>;

    unsafe fn check_fcinfo_and_prepare(fcinfo: pg_sys::FunctionCallInfo) -> CallCx {
        unsafe {
            if srf_is_first_call(fcinfo) {
                let fn_call_cx = pg_sys::init_MultiFuncCall(fcinfo);
                CallCx::WrappedFn((*fn_call_cx).multi_call_memory_ctx)
            } else {
                CallCx::RestoreCx
            }
        }
    }

    fn to_ret(self) -> Self::Ret {
        let mut iter = self;
        IterRet(match iter.next() {
            None => Step::Done,
            Some(value) => Step::Init(iter, value),
        })
    }

    unsafe fn box_ret_in_fcinfo(fcinfo: pg_sys::FunctionCallInfo, ret: Self::Ret) -> pg_sys::Datum {
        let value = unsafe {
            match ret.0 {
                Step::Done => return empty_srf(fcinfo),
                Step::Once(value) => value,
                Step::Init(iter, value) => {
                    // Move the iterator in and don't worry about putting it back
                    iter.move_into_fcinfo_fcx(fcinfo);
                    value.fill_fcinfo_fcx(fcinfo);
                    value
                }
            }
        };

        unsafe {
            let fcx = deref_fcx(fcinfo);
            srf_return_next(fcinfo, fcx);
            <Row as RetAbi>::box_ret_in_fcinfo(fcinfo, value.to_ret())
        }
    }

    unsafe fn fill_fcinfo_fcx(&self, _fcinfo: pg_sys::FunctionCallInfo) {}

    unsafe fn move_into_fcinfo_fcx(self, fcinfo: pg_sys::FunctionCallInfo) {
        unsafe {
            let fcx = deref_fcx(fcinfo);
            let ptr = srf_memcx(fcx).leak_and_drop_on_delete(self);
            // it's the first call so we need to finish setting up fcx
            (*fcx).user_fctx = ptr.cast();
        }
    }

    unsafe fn ret_from_fcinfo_fcx(fcinfo: pg_sys::FunctionCallInfo) -> Self::Ret {
        // SAFETY: fcx.user_fctx was set earlier, immediately before or in a prior call
        let iter = unsafe {
            let fcx = deref_fcx(fcinfo);
            &mut *(*fcx).user_fctx.cast::<TableIterator<Row>>()
        };
        IterRet(match iter.next() {
            None => Step::Done,
            Some(value) => Step::Once(value),
        })
    }

    unsafe fn finish_call_fcinfo(fcinfo: pg_sys::FunctionCallInfo) {
        unsafe {
            let fcx = deref_fcx(fcinfo);
            srf_return_done(fcinfo, fcx)
        }
    }
}

/// How iterators are returned
pub struct IterRet<T: RetAbi>(Step<T>);

/// ValuePerCall SRF steps
enum Step<T: RetAbi> {
    Done,
    Once(T::Item),
    Init(T, T::Item),
}

pub(crate) unsafe fn empty_srf(fcinfo: pg_sys::FunctionCallInfo) -> pg_sys::Datum {
    unsafe {
        let fcx = deref_fcx(fcinfo);
        srf_return_done(fcinfo, fcx);
        pg_return_null(fcinfo)
    }
}

/// "per_MultiFuncCall" but no FFI cost
pub(crate) unsafe fn deref_fcx(fcinfo: pg_sys::FunctionCallInfo) -> *mut pg_sys::FuncCallContext {
    unsafe { (*(*fcinfo).flinfo).fn_extra.cast() }
}

pub(crate) unsafe fn srf_memcx(fcx: *mut pg_sys::FuncCallContext) -> PgMemoryContexts {
    unsafe { PgMemoryContexts::For((*fcx).multi_call_memory_ctx) }
}

/// Return ABI for single-column tuples
///
/// Postgres extended SQL with `returns setof $ty` before SQL had `returns table($($ident $ty),*)`.
/// Due to this history, single-column `returns table` reuses the internals of `returns setof`.
/// This lets them simply return a simple Datum instead of handling a TupleDesc and HeapTuple, but
/// means we need to have this distinct impl, as the return type is not `TYPEFUNC_COMPOSITE`!
/// Fortunately, RetAbi lets `TableIterator<'a, Tup>` handle this by calling `<Tup as RetAbi>`.
unsafe impl<C> RetAbi for (C,)
where
    C: BoxRet, // so we support TableIterator<'a, (Option<T>,)> as well
{
    type Item = C;
    type Ret = C;

    fn to_ret(self) -> Self::Ret {
        self.0
    }

    /// Returning a "row" of only one column is identical to returning that single type
    unsafe fn box_ret_in_fcinfo(fcinfo: pg_sys::FunctionCallInfo, ret: Self::Ret) -> pg_sys::Datum {
        unsafe { C::box_ret_in_fcinfo(fcinfo, ret.to_ret()) }
    }

    unsafe fn fill_fcinfo_fcx(&self, _fcinfo: pg_sys::FunctionCallInfo) {}

    unsafe fn move_into_fcinfo_fcx(self, _fcinfo: pg_sys::FunctionCallInfo) {}
}

macro_rules! impl_table_iter {
    ($($C:ident),* $(,)?) => {
        unsafe impl<'iter, $($C,)*> SqlTranslatable for TableIterator<'iter, ($($C,)*)>
        where
            $($C: SqlTranslatable + 'iter,)*
        {
            fn argument_sql() -> Result<SqlMapping, ArgumentError> {
                Err(ArgumentError::Table)
            }
            fn return_sql() -> Result<Returns, ReturnsError> {
                let vec = vec![
                $(
                    $C::return_sql().and_then(|sql| match sql {
                        Returns::One(sql) => Ok(sql),
                        Returns::SetOf(_) => Err(ReturnsError::TableContainingSetOf),
                        Returns::Table(_) => Err(ReturnsError::NestedTable),
                    })?,
                )*
                ];
                Ok(Returns::Table(vec))
            }
        }

        impl<$($C: IntoDatum),*> IntoHeapTuple for ($($C,)*) {
            unsafe fn into_heap_tuple(self, tupdesc: pg_sys::TupleDesc) -> *mut pg_sys::HeapTupleData {
                // shadowing the type names with these identifiers
                #[allow(nonstandard_style)]
                let ($($C,)*) = self;
                let datums = [$($C.into_datum(),)*];
                let mut nulls = datums.map(|option| option.is_none());
                let mut datums = datums.map(|option| option.unwrap_or(pg_sys::Datum::from(0)));

                unsafe {
                    // SAFETY:  Caller has asserted that `tupdesc` is valid, and we just went
                    // through a little bit of effort to setup properly sized arrays for
                    // `datums` and `nulls`
                    pg_sys::heap_form_tuple(tupdesc, datums.as_mut_ptr(), nulls.as_mut_ptr())
                }
            }
        }

        unsafe impl<$($C),*> RetAbi for ($($C,)*)
        where
             $($C: BoxRet,)*
             Self: IntoHeapTuple,
        {
            type Item = Self;
            type Ret = Self;

            fn to_ret(self) -> Self::Ret {
                self
            }

            unsafe fn box_ret_in_fcinfo(fcinfo: pg_sys::FunctionCallInfo, ret: Self::Ret) -> pg_sys::Datum {
                unsafe {
                    let fcx = deref_fcx(fcinfo);
                    let heap_tuple = ret.into_heap_tuple((*fcx).tuple_desc);
                    pg_sys::HeapTupleHeaderGetDatum((*heap_tuple).t_data)
                }
            }

            unsafe fn move_into_fcinfo_fcx(self, _fcinfo: pg_sys::FunctionCallInfo) {}

            unsafe fn fill_fcinfo_fcx(&self, fcinfo: pg_sys::FunctionCallInfo) {
                // Pure side effect, leave the value in place.
                unsafe {
                    let fcx = deref_fcx(fcinfo);
                    srf_memcx(fcx).switch_to(|_| {
                        let mut tupdesc = ptr::null_mut();
                        let mut oid = pg_sys::Oid::default();
                        let ty_class = pg_sys::get_call_result_type(fcinfo, &mut oid, &mut tupdesc);
                        if tupdesc.is_non_null() && ty_class == pg_sys::TypeFuncClass::TYPEFUNC_COMPOSITE {
                            pg_sys::BlessTupleDesc(tupdesc);
                            (*fcx).tuple_desc = tupdesc;
                        }
                    });
                }
            }
        }

    }
}

impl_table_iter!(T0, T1);
impl_table_iter!(T0, T1, T2);
impl_table_iter!(T0, T1, T2, T3);
impl_table_iter!(T0, T1, T2, T3, T4);
impl_table_iter!(T0, T1, T2, T3, T4, T5);
impl_table_iter!(T0, T1, T2, T3, T4, T5, T6);
impl_table_iter!(T0, T1, T2, T3, T4, T5, T6, T7);
impl_table_iter!(T0, T1, T2, T3, T4, T5, T6, T7, T8);
impl_table_iter!(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9);
impl_table_iter!(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10);
impl_table_iter!(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11);
impl_table_iter!(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12);
impl_table_iter!(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13);
impl_table_iter!(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14);
impl_table_iter!(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15);
impl_table_iter!(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16);
impl_table_iter!(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16, T17);
impl_table_iter!(
    T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16, T17, T18
);
impl_table_iter!(
    T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16, T17, T18, T19
);
impl_table_iter!(
    T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16, T17, T18, T19, T20
);
impl_table_iter!(
    T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16, T17, T18, T19, T20,
    T21
);
impl_table_iter!(
    T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16, T17, T18, T19, T20,
    T21, T22
);
impl_table_iter!(
    T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16, T17, T18, T19, T20,
    T21, T22, T23
);
impl_table_iter!(
    T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16, T17, T18, T19, T20,
    T21, T22, T23, T24
);
impl_table_iter!(
    T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16, T17, T18, T19, T20,
    T21, T22, T23, T24, T25
);
impl_table_iter!(
    T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16, T17, T18, T19, T20,
    T21, T22, T23, T24, T25, T26
);
impl_table_iter!(
    T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16, T17, T18, T19, T20,
    T21, T22, T23, T24, T25, T26, T27
);
impl_table_iter!(
    T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16, T17, T18, T19, T20,
    T21, T22, T23, T24, T25, T26, T27, T28
);
impl_table_iter!(
    T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16, T17, T18, T19, T20,
    T21, T22, T23, T24, T25, T26, T27, T28, T29
);
impl_table_iter!(
    T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16, T17, T18, T19, T20,
    T21, T22, T23, T24, T25, T26, T27, T28, T29, T30
);
impl_table_iter!(
    T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16, T17, T18, T19, T20,
    T21, T22, T23, T24, T25, T26, T27, T28, T29, T30, T31
);
