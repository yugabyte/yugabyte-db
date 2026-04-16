//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::{pg_sys, FromDatum, IntoDatum};
use pgrx_sql_entity_graph::metadata::{
    ArgumentError, Returns, ReturnsError, SqlMapping, SqlTranslatable,
};

/// The [`anyelement` polymorphic pseudo-type][anyelement].
///
// rustdoc doesn't directly support a warning block: https://github.com/rust-lang/rust/issues/73935
/// **Warning**: Calling [`FromDatum::from_datum`] with this type will unconditionally panic. Call
/// [`FromDatum::from_polymorphic_datum`] with a type ID instead.
///
/// [anyelement]: https://www.postgresql.org/docs/current/extend-type-system.html#EXTEND-TYPES-POLYMORPHIC
#[derive(Debug, Clone, Copy)]
pub struct AnyElement {
    datum: pg_sys::Datum,
    typoid: pg_sys::Oid,
}

impl AnyElement {
    pub fn datum(&self) -> pg_sys::Datum {
        self.datum
    }

    pub fn oid(&self) -> pg_sys::Oid {
        self.typoid
    }

    /// Convert this element into a specific type.
    ///
    /// # Safety
    ///
    /// This function is unsafe as it cannot guarantee that the underlying datum can be represented
    /// as `T`.  This is your responsibility
    #[inline]
    pub unsafe fn into<T: FromDatum>(&self) -> Option<T> {
        T::from_polymorphic_datum(self.datum(), false, self.oid())
    }
}

impl FromDatum for AnyElement {
    const GET_TYPOID: bool = true;

    /// You should **never** call this function to make this type; it will unconditionally panic.
    /// For polymorphic types such as this one, you must use [`FromDatum::from_polymorphic_datum`]
    /// and pass a type ID.
    #[inline]
    unsafe fn from_datum(_datum: pg_sys::Datum, _is_null: bool) -> Option<AnyElement> {
        panic!("Can't create a polymorphic type using from_datum, call FromDatum::from_polymorphic_datum instead")
    }

    #[inline]
    unsafe fn from_polymorphic_datum(
        datum: pg_sys::Datum,
        is_null: bool,
        typoid: pg_sys::Oid,
    ) -> Option<AnyElement> {
        if is_null {
            None
        } else {
            Some(AnyElement { datum, typoid })
        }
    }
}

impl IntoDatum for AnyElement {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        Some(self.datum)
    }

    fn type_oid() -> pg_sys::Oid {
        pg_sys::ANYELEMENTOID
    }
}

unsafe impl SqlTranslatable for AnyElement {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("anyelement"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("anyelement")))
    }
}
