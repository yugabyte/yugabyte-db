//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
//! Handing for easily converting Postgres Datum types into their corresponding Rust types
//! and converting Rust types into their corresponding Postgres types
#![allow(clippy::borrow_interior_mutable_const, clippy::declare_interior_mutable_const)] // https://github.com/pgcentralfoundation/pgrx/issues/1526
#![allow(clippy::needless_lifetimes)] // https://github.com/pgcentralfoundation/pgrx/issues?q=is%3Aissue+is%3Aopen+label%3Alifetime
#![allow(clippy::unused_unit)]
#![allow(clippy::unnecessary_fallible_conversions)] // https://github.com/pgcentralfoundation/pgrx/issues/1525
mod anyarray;
mod anyelement;
mod array;
mod borrow;
mod date;
pub mod datetime_support;
mod from;
mod geo;
mod inet;
mod internal;
mod interval;
mod into;
mod json;
pub mod numeric;
pub mod numeric_support;
#[deny(unsafe_op_in_unsafe_fn)]
mod range;
mod time;
mod time_stamp;
mod time_stamp_with_timezone;
mod time_with_timezone;
mod tuples;
mod unbox;
mod uuid;
mod varlena;
mod with_typeid;

pub use self::time::*;
pub use self::uuid::*;
pub use anyarray::*;
pub use anyelement::*;
pub use array::*;
pub use borrow::*;
pub use date::*;
pub use datetime_support::*;
pub use from::*;
pub use inet::*;
pub use internal::*;
pub use interval::*;
pub use into::*;
pub use json::*;
pub use numeric::{AnyNumeric, Numeric};
pub use range::*;
pub use time_stamp::*;
pub use time_stamp_with_timezone::*;
pub use time_with_timezone::*;
pub use unbox::*;
pub use varlena::*;

use crate::memcx::MemCx;
use crate::pg_sys;
use core::marker::PhantomData;
use core::ptr;
#[doc(hidden)]
pub use with_typeid::nonstatic_typeid;
pub use with_typeid::{WithArrayTypeIds, WithSizedTypeIds, WithTypeIds, WithVarlenaTypeIds};

/// How Postgres represents datatypes
///
/// The "no-frills" version is [`pg_sys::Datum`], which is abstractly a union of "pointer to void"
/// with other scalar types that can be packed within a pointer's bytes. In practical use, a "raw"
/// Datum can prove to have the same risks as a pointer: code may try to use it without knowing
/// whether its pointee has been deallocated. To lift a Datum into a Rust type requires making
/// implicit lifetimes into explicit bounds.
///
/// Merely having a lifetime does not make `Datum<'src>` "safe" to use. To abstractly represent a
/// full PostgreSQL value needs at least the tuple (Datum, bool, [`pg_sys::Oid`]): a tagged union.
/// `Datum<'src>` itself is effectively a dynamically-typed union *without a type tag*. It exists
/// not to make code manipulating it safe, but to make it possible to write unsafe code correctly,
/// passing Datums to and from Postgres without having to wonder if the implied `&'src T` would
/// actually refer to deallocated data.
///
/// # Designing safe abstractions
/// A function must only be declared safe if *all* inputs **cannot** cause [undefined behavior].
/// Transmuting a raw `pg_sys::Datum` into [`&'a T`] grants a potentially-unbounded lifetime,
/// breaking the rule borrows must not outlive the borrowed. Avoiding such transmutations infects
/// even simple generic functions with soundness obligations. Using only `&'a pg_sys::Datum` lasts
/// only until one must pass by-value, which is the entire point of the original type as Postgres
/// defined it, but can still be preferable.
///
/// `Datum<'src>` makes it theoretically possible to write functions with a signature like
/// ```
/// use pgrx::datum::Datum;
/// # use core::marker::PhantomData;
/// # use pgrx::memcx::MemCx;
/// # struct InCx<'mcx, T>(T, PhantomData<&'mcx MemCx<'mcx>>);
/// fn construct_type_from_datum<'src, T>(
///     datum: Datum<'src>,
///     func: impl FnOnce(Datum<'src>) -> InCx<'src, T>
/// ) -> InCx<'src, T> {
///    func(datum)
/// }
/// ```
/// However, it is possible for `T<'src>` to be insufficient to represent the real lifetime of the
/// abstract Postgres type's allocations. Often a Datum must be "detoasted", which may reallocate.
/// This may demand two constraints on the return type to represent both possible lifetimes, like:
/// ```
/// use pgrx::datum::Datum;
/// use pgrx::memcx::MemCx;
/// # use core::marker::PhantomData;
/// # struct Detoasted<'mcx, T>(T, PhantomData<&'mcx MemCx<'mcx>>);
/// # struct InCx<'mcx, T>(T, PhantomData<&'mcx MemCx<'mcx>>);
/// fn detoast_type_from_datum<'old, 'new, T>(
///     datum: Datum<'old>,
///     memcx: MemCx<'new>,
/// ) -> Detoasted<'new, InCx<'old, T>> {
///    todo!()
/// }
/// ```
/// In actual practice, these can be unified into a single lifetime: the lower bound of both.
/// This is both good and bad: types can use fewer lifetime annotations, even after detoasting.
/// However, in general, because lifetime unification can be done implicitly by the compiler,
/// it is often important to name each and every single lifetime involved in functions that
/// perform these tasks.
///
/// [`&'a T`]: reference
/// [undefined behavior]: https://doc.rust-lang.org/reference/behavior-considered-undefined.html
pub struct Datum<'src>(
    pg_sys::Datum,
    /// if a Datum borrows anything, it's "from" a [`pg_sys::MemoryContext`]
    /// as a memory context, like an arena, is deallocated "together".
    /// FIXME: a more-correct inner type later
    PhantomData<&'src MemCx<'src>>,
);

impl<'src> Datum<'src> {
    /// Strip a Datum of its lifetime for FFI purposes.
    pub fn sans_lifetime(self) -> pg_sys::Datum {
        self.0
    }

    /// Construct a Datum containing only a null pointer.
    pub fn null() -> Datum<'src> {
        Self(pg_sys::Datum::from(0), PhantomData)
    }

    /// Reborrow the Datum as `T`
    ///
    /// If the type is `PassBy::Ref`, this may be `None`.
    pub unsafe fn borrow_as<T: BorrowDatum>(&self) -> Option<&T> {
        let ptr = ptr::NonNull::new_unchecked(ptr::from_ref(self).cast_mut());
        borrow::datum_ptr_to_bytes::<T>(ptr).map(|ptr| BorrowDatum::borrow_unchecked(ptr))
    }
}

/// Represents a typed value as pair of [`Option<Datum>`] and [`Oid`].
///
/// [`Oid`]: pg_sys::Oid
pub struct DatumWithOid<'src> {
    datum: Option<Datum<'src>>,
    oid: pg_sys::Oid,
}

impl<'src> DatumWithOid<'src> {
    /// Construct a `DatumWithOid` containing the provided value and [`Oid`].
    ///
    /// [`Oid`]: pg_sys::Oid
    pub unsafe fn new<T: IntoDatum>(value: T, oid: pg_sys::Oid) -> Self {
        Self::new_from_datum(value.into_datum().map(|d| Datum(d, PhantomData::default())), oid)
    }

    /// Construct a `DatumWithOid` given an optional [`Datum`] and [`Oid`].
    ///
    /// SQL NULL is represented by passing `None` for `datum`.
    ///
    /// [`Datum`]: crate::datum::Datum
    /// [`Oid`]: pg_sys::Oid
    pub unsafe fn new_from_datum(datum: Option<Datum<'src>>, oid: pg_sys::Oid) -> Self {
        Self { datum, oid }
    }

    /// Constructs a `DatumWithOid` representing SQL NULL
    pub fn null_oid(oid: pg_sys::Oid) -> Self {
        Self { datum: None, oid }
    }

    /// Construct a `DatumWithOid` containing a null value for type `T`.
    pub fn null<T: IntoDatum>() -> Self {
        Self { datum: None, oid: T::type_oid() }
    }

    /// Returns an [`Option<Datum>`].
    pub fn datum(&self) -> Option<Datum<'src>> {
        self.datum.as_ref().map(|d| Datum(d.0, PhantomData::default()))
    }

    /// Returns an [`Oid`].
    ///
    /// [`Oid`]: pg_sys::Oid
    pub fn oid(&self) -> pg_sys::Oid {
        self.oid
    }
}

impl<'src, T: IntoDatum> From<T> for DatumWithOid<'src> {
    fn from(value: T) -> Self {
        unsafe {
            // SAFETY: The oid is provided by the type.
            Self::new(value, T::type_oid())
        }
    }
}

/// A tagging trait to indicate a user type is also meant to be used by Postgres
/// Implemented automatically by `#[derive(PostgresType)]`
pub trait PostgresType {}

/// Creates an array of [`pg_sys::Oid`] with the OID of each provided type
///
/// # Examples
///
/// ```
/// use pgrx::{oids_of, datum::IntoDatum};
///
/// let oids = oids_of![i32, f64];
/// assert_eq!(oids[0], i32::type_oid().into());
/// assert_eq!(oids[1], f64::type_oid().into());
///
/// // the usual conversions or coercions are available
/// let oid_vec = oids_of![i8, i16].to_vec();
/// let no_oid = &oids_of![];
/// assert_eq!(no_oid.len(), 0);
/// ```
#[macro_export]
macro_rules! oids_of {
    () =>(
        // avoid coercions to an ambiguously-typed array or slice
        [$crate::pg_sys::PgOid::Invalid; 0]
    );
    ($($t:path),+ $(,)?) => (
        [$($crate::pg_sys::PgOid::from(<$t>::type_oid())),*]
    );
}
