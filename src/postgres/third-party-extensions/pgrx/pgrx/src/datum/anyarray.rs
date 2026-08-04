//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use super::ArrayIntoIterator;
use crate::{pg_sys, AnyElement, Array, FromDatum, IntoDatum};
use pgrx_sql_entity_graph::metadata::{
    ArgumentError, Returns, ReturnsError, SqlMapping, SqlTranslatable,
};
use std::iter::FusedIterator;

/// The [`anyarray` polymorphic pseudo-type][anyarray].
///
// rustdoc doesn't directly support a warning block: https://github.com/rust-lang/rust/issues/73935
/// **Warning**: Calling [`FromDatum::from_datum`] with this type will unconditionally panic. Call
/// [`FromDatum::from_polymorphic_datum`] with a type ID instead.
///
/// [anyarray]: https://www.postgresql.org/docs/current/extend-type-system.html#EXTEND-TYPES-POLYMORPHIC
#[derive(Debug, Clone, Copy)]
pub struct AnyArray {
    datum: pg_sys::Datum,
    typoid: pg_sys::Oid,
}

impl AnyArray {
    pub fn datum(&self) -> pg_sys::Datum {
        self.datum
    }

    pub fn oid(&self) -> pg_sys::Oid {
        self.typoid
    }

    #[inline]
    pub fn into<T: FromDatum>(&self) -> Option<T> {
        unsafe { T::from_polymorphic_datum(self.datum(), false, self.oid()) }
    }
}

impl FromDatum for AnyArray {
    const GET_TYPOID: bool = true;

    /// You should **never** call this function to make this type; it will unconditionally panic.
    /// For polymorphic types such as this one, you must use [`FromDatum::from_polymorphic_datum`]
    /// and pass a type ID.
    #[inline]
    unsafe fn from_datum(_datum: pg_sys::Datum, _is_null: bool) -> Option<AnyArray> {
        panic!("Can't create a polymorphic type using from_datum, call FromDatum::from_polymorphic_datum instead")
    }

    #[inline]
    unsafe fn from_polymorphic_datum(
        datum: pg_sys::Datum,
        is_null: bool,
        typoid: pg_sys::Oid,
    ) -> Option<AnyArray> {
        if is_null {
            None
        } else {
            Some(AnyArray { datum, typoid })
        }
    }
}

impl IntoDatum for AnyArray {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        Some(self.datum)
    }

    fn type_oid() -> pg_sys::Oid {
        pg_sys::ANYARRAYOID
    }
}

unsafe impl SqlTranslatable for AnyArray {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("anyarray"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("anyarray")))
    }
}

impl<'a> IntoIterator for &'a AnyArray
where
    Self: 'a,
{
    type Item = Option<AnyElement>;
    type IntoIter = AnyArrayIterator<'a>;

    fn into_iter(self) -> Self::IntoIter {
        unsafe {
            AnyArrayIterator {
                inner: Array::<pg_sys::Datum>::from_polymorphic_datum(
                    self.datum(),
                    false,
                    self.oid(),
                )
                .map(|a| a.into_iter()),
                typelem: pg_sys::get_element_type(self.oid()),
            }
        }
    }
}

pub struct AnyArrayIterator<'a> {
    inner: Option<ArrayIntoIterator<'a, pg_sys::Datum>>,
    typelem: pg_sys::Oid,
}

impl<'a> Iterator for AnyArrayIterator<'a> {
    type Item = Option<AnyElement>;

    fn next(&mut self) -> Option<Self::Item> {
        self.inner.as_mut().and_then(|i| {
            i.next().map(|d| match d {
                Some(d) => unsafe { AnyElement::from_polymorphic_datum(d, false, self.typelem) },
                None => None,
            })
        })
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        match &self.inner {
            Some(inner) => inner.size_hint(),
            None => (0, Some(0)),
        }
    }
}

impl<'a> ExactSizeIterator for AnyArrayIterator<'a> {}
impl<'a> FusedIterator for AnyArrayIterator<'a> {}
