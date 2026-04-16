//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::{pg_sys, FromDatum, IntoDatum, PgMemoryContexts};
use pgrx_sql_entity_graph::metadata::{
    ArgumentError, Returns, ReturnsError, SqlMapping, SqlTranslatable,
};

/// Represents Postgres' `internal` data type, which is documented as:
///
///    The internal pseudo-type is used to declare functions that are meant only to be called
///    internally by the database system, and not by direct invocation in an SQL query. If a
///    function has at least one internal-type argument then it cannot be called from SQL. To
///    preserve the type safety of this restriction it is important to follow this coding rule: do
///    not create any function that is declared to return internal unless it has at least one
///    internal argument.
///
/// ## Implementation Notes
///
/// [Internal] is a wrapper around an `Option<pg_sys::Datum>`, which when retrieved via
/// `::get/get_mut()` is cast to a pointer of `T`, returning the respective reference.
///
/// ## Safety
///
/// We make no guarantees about what the internal [pg_sys::Datum] actually points to in memory, so
/// it is your responsibility to ensure that what you're casting it to is really what it is.
#[derive(Default)]
pub struct Internal(Option<pg_sys::Datum>);

impl Internal {
    /// Construct a new Internal from any type.  
    ///
    /// The value will be dropped when the [PgMemoryContexts::CurrentMemoryContext] is deleted.
    #[inline(always)]
    pub fn new<T>(t: T) -> Self {
        Self(Some(pg_sys::Datum::from(
            PgMemoryContexts::CurrentMemoryContext.leak_and_drop_on_delete(t),
        )))
    }

    /// Returns true if the internal value is initialized. If false, this is a null pointer.
    #[inline(always)]
    pub fn initialized(&self) -> bool {
        self.0.is_some()
    }

    /// Return a reference to the memory pointed to by this [`Internal`], as `Some(&T)`, unless the
    /// backing datum is null, then `None`.
    ///
    /// ## Safety
    ///
    /// We cannot guarantee that the contained datum points to memory that is really `T`.  This is
    /// your responsibility.
    #[inline(always)]
    pub unsafe fn get<T>(&self) -> Option<&T> {
        self.0.and_then(|datum| (datum.cast_mut_ptr::<T>() as *const T).as_ref())
    }

    /// Initializes the internal with `value`, then returns a mutable reference to it.
    ///
    /// If the Internal is already initialized with a value, the old value is dropped.
    ///
    /// See also [`Internal::get_or_insert`], which doesn’t update the value if already initialized.
    ///
    /// ## Safety
    ///
    /// We cannot guarantee that the contained datum points to memory that is really `T`.  This is
    /// your responsibility.
    #[inline(always)]
    pub unsafe fn insert<T>(&mut self, value: T) -> &mut T {
        let datum = pg_sys::Datum::from(
            PgMemoryContexts::CurrentMemoryContext.leak_and_drop_on_delete(value),
        );
        let ptr = self.0.insert(datum);
        &mut *(ptr.cast_mut_ptr::<T>())
    }

    /// Return a reference to the memory pointed to by this [`Internal`], as `Some(&mut T)`, unless the
    /// backing datum is null, then `None`.
    ///
    /// ## Safety
    ///
    /// We cannot guarantee that the contained datum points to memory that is really `T`.  This is
    /// your responsibility.
    #[inline(always)]
    pub unsafe fn get_mut<T>(&self) -> Option<&mut T> {
        self.0.and_then(|datum| (datum.cast_mut_ptr::<T>()).as_mut())
    }

    /// Initializes the internal with `value` if it is not initialized, then returns a mutable reference to
    /// the contained value.
    ///
    /// See also [`Internal::insert`], which updates the value even if the option already contains Some.
    ///
    /// ## Safety
    ///
    /// We cannot guarantee that the contained datum points to memory that is really `T`.  This is
    /// your responsibility.
    #[inline(always)]
    pub unsafe fn get_or_insert<T>(&mut self, value: T) -> &mut T {
        self.get_or_insert_with(|| value)
    }

    /// Initializes the internal with a default if it is not initialized, then returns a mutable reference
    /// to the contained value.
    ///
    /// See also [`Internal::insert`], which updates the value even if the option already contains Some.
    ///
    /// ## Safety
    ///
    /// We cannot guarantee that the contained datum points to memory that is really `T`.  This is
    /// your responsibility.
    #[inline(always)]
    pub unsafe fn get_or_insert_default<T>(&mut self) -> &mut T
    where
        T: Default,
    {
        self.get_or_insert_with(|| T::default())
    }

    /// Inserts a value computed from `f` into the internal if it is `None`, then returns a mutable reference
    /// to the contained value.
    ///
    /// ## Safety
    ///
    /// We cannot guarantee that the contained datum points to memory that is really `T`.  This is
    /// your responsibility.
    #[inline(always)]
    pub unsafe fn get_or_insert_with<F, T>(&mut self, f: F) -> &mut T
    where
        F: FnOnce() -> T,
    {
        let ptr = self.0.get_or_insert_with(|| {
            let result = f();

            PgMemoryContexts::CurrentMemoryContext.leak_and_drop_on_delete(result).into()
        });
        &mut *(ptr.cast_mut_ptr::<T>())
    }

    /// Returns the contained `Option<pg_sys::Datum>`
    #[inline(always)]
    pub fn unwrap(self) -> Option<pg_sys::Datum> {
        self.0
    }
}

impl From<Option<pg_sys::Datum>> for Internal {
    #[inline]
    fn from(datum: Option<pg_sys::Datum>) -> Self {
        Internal(datum)
    }
}

impl FromDatum for Internal {
    #[inline]
    unsafe fn from_polymorphic_datum(
        datum: pg_sys::Datum,
        is_null: bool,
        _: pg_sys::Oid,
    ) -> Option<Internal> {
        Some(Internal(if is_null { None } else { Some(datum) }))
    }
}

impl IntoDatum for Internal {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        self.0
    }

    #[inline]
    fn type_oid() -> pg_sys::Oid {
        pg_sys::INTERNALOID
    }
}

unsafe impl SqlTranslatable for crate::datum::Internal {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("internal"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("internal")))
    }
    // We don't want to strict upgrade if internal is present.
    fn optional() -> bool {
        true
    }
}
