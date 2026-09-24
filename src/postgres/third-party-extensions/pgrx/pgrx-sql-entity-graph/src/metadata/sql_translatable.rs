//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
/*!

A trait denoting a type can possibly be mapped to an SQL type

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
use std::any::Any;
use std::ffi::{CStr, CString};
use std::fmt::Display;
use thiserror::Error;

use super::return_variant::ReturnsError;
use super::{FunctionMetadataTypeEntity, Returns, TypeOrigin};

#[derive(Clone, Copy, Debug, Hash, Ord, PartialOrd, PartialEq, Eq, Error)]
pub enum ArgumentError {
    #[error("Cannot use SetOfIterator as an argument")]
    SetOf,
    #[error("Cannot use TableIterator as an argument")]
    Table,
    #[error("Nested arrays are not supported in arguments")]
    NestedArray,
    #[error("Cannot use bare u8")]
    BareU8,
    #[error("SqlMapping::Skip inside Array is not valid")]
    SkipInArray,
    #[error("A Datum as an argument means that `sql = \"...\"` must be set in the declaration")]
    Datum,
    #[error("`{0}` is not able to be used as a function argument")]
    NotValidAsArgument(&'static str),
}

/// Describes ways that Rust types are mapped into SQL
#[derive(Clone, Debug, Hash, Eq, PartialEq, Ord, PartialOrd)]
pub enum SqlMapping {
    /// Explicit mappings provided by PGRX
    As(String),
    Composite,
    Array(SqlArrayMapping),
    /// A type which does not actually appear in SQL
    Skip,
}

impl SqlMapping {
    pub fn literal(s: &'static str) -> Self {
        Self::As(String::from(s))
    }
}

#[derive(Clone, Debug, Hash, Eq, PartialEq, Ord, PartialOrd)]
pub enum SqlArrayMapping {
    /// Explicit mappings provided by PGRX
    As(String),
    Composite,
}

/// Const-friendly SQL mapping metadata.
#[derive(Clone, Copy, Debug, Hash, Eq, PartialEq, Ord, PartialOrd)]
pub enum SqlMappingRef {
    /// Explicit mappings provided by PGRX
    As(&'static str),
    Numeric {
        precision: Option<u32>,
        scale: Option<u32>,
    },
    Composite,
    Array(SqlArrayMappingRef),
    /// A type which does not actually appear in SQL
    Skip,
}

impl SqlMappingRef {
    pub const fn literal(s: &'static str) -> Self {
        Self::As(s)
    }
}

#[derive(Clone, Copy, Debug, Hash, Eq, PartialEq, Ord, PartialOrd)]
pub enum SqlArrayMappingRef {
    /// Explicit mappings provided by PGRX
    As(&'static str),
    Numeric {
        precision: Option<u32>,
        scale: Option<u32>,
    },
    Composite,
}

pub(crate) fn numeric_sql_string(precision: Option<u32>, scale: Option<u32>) -> String {
    match (precision, scale) {
        (None, _) => "NUMERIC".to_string(),
        (Some(precision), None) => format!("NUMERIC({precision})"),
        (Some(precision), Some(scale)) => format!("NUMERIC({precision}, {scale})"),
    }
}

impl From<SqlArrayMappingRef> for SqlArrayMapping {
    fn from(value: SqlArrayMappingRef) -> Self {
        match value {
            SqlArrayMappingRef::As(value) => Self::As(String::from(value)),
            SqlArrayMappingRef::Numeric { precision, scale } => {
                Self::As(numeric_sql_string(precision, scale))
            }
            SqlArrayMappingRef::Composite => Self::Composite,
        }
    }
}

impl From<SqlMappingRef> for SqlMapping {
    fn from(value: SqlMappingRef) -> Self {
        match value {
            SqlMappingRef::As(value) => Self::literal(value),
            SqlMappingRef::Numeric { precision, scale } => {
                Self::As(numeric_sql_string(precision, scale))
            }
            SqlMappingRef::Composite => Self::Composite,
            SqlMappingRef::Array(value) => Self::Array(value.into()),
            SqlMappingRef::Skip => Self::Skip,
        }
    }
}

/// Const-friendly return metadata.
#[derive(Clone, Copy, Debug, Hash, Eq, PartialEq, Ord, PartialOrd)]
pub enum ReturnsRef {
    One(SqlMappingRef),
    SetOf(SqlMappingRef),
    Table(&'static [SqlMappingRef]),
}

impl From<ReturnsRef> for Returns {
    fn from(value: ReturnsRef) -> Self {
        match value {
            ReturnsRef::One(value) => Self::One(value.into()),
            ReturnsRef::SetOf(value) => Self::SetOf(value.into()),
            ReturnsRef::Table(values) => {
                Self::Table(values.iter().copied().map(Into::into).collect())
            }
        }
    }
}

pub const fn array_argument_sql(
    mapping: Result<SqlMappingRef, ArgumentError>,
) -> Result<SqlMappingRef, ArgumentError> {
    match mapping {
        Ok(SqlMappingRef::As(sql)) => Ok(SqlMappingRef::Array(SqlArrayMappingRef::As(sql))),
        Ok(SqlMappingRef::Numeric { precision, scale }) => {
            Ok(SqlMappingRef::Array(SqlArrayMappingRef::Numeric { precision, scale }))
        }
        Ok(SqlMappingRef::Composite) => Ok(SqlMappingRef::Array(SqlArrayMappingRef::Composite)),
        Ok(SqlMappingRef::Skip) => Err(ArgumentError::SkipInArray),
        Ok(SqlMappingRef::Array(_)) => Err(ArgumentError::NestedArray),
        Err(err) => Err(err),
    }
}

pub const fn array_return_sql(
    returns: Result<ReturnsRef, ReturnsError>,
) -> Result<ReturnsRef, ReturnsError> {
    match returns {
        Ok(ReturnsRef::One(SqlMappingRef::As(sql))) => {
            Ok(ReturnsRef::One(SqlMappingRef::Array(SqlArrayMappingRef::As(sql))))
        }
        Ok(ReturnsRef::One(SqlMappingRef::Numeric { precision, scale })) => {
            Ok(ReturnsRef::One(SqlMappingRef::Array(SqlArrayMappingRef::Numeric {
                precision,
                scale,
            })))
        }
        Ok(ReturnsRef::One(SqlMappingRef::Composite)) => {
            Ok(ReturnsRef::One(SqlMappingRef::Array(SqlArrayMappingRef::Composite)))
        }
        Ok(ReturnsRef::One(SqlMappingRef::Skip)) => Err(ReturnsError::SkipInArray),
        Ok(ReturnsRef::One(SqlMappingRef::Array(_))) => Err(ReturnsError::NestedArray),
        Ok(ReturnsRef::SetOf(_)) => Err(ReturnsError::SetOfInArray),
        Ok(ReturnsRef::Table(_)) => Err(ReturnsError::TableInArray),
        Err(err) => Err(err),
    }
}

pub const fn setof_return_sql(
    returns: Result<ReturnsRef, ReturnsError>,
) -> Result<ReturnsRef, ReturnsError> {
    match returns {
        Ok(ReturnsRef::One(sql)) => Ok(ReturnsRef::SetOf(sql)),
        Ok(ReturnsRef::SetOf(_)) => Err(ReturnsError::NestedSetOf),
        Ok(ReturnsRef::Table(_)) => Err(ReturnsError::SetOfContainingTable),
        Err(err) => Err(err),
    }
}

pub const fn table_item_sql(
    returns: Result<ReturnsRef, ReturnsError>,
) -> Result<SqlMappingRef, ReturnsError> {
    match returns {
        Ok(ReturnsRef::One(sql)) => Ok(sql),
        Ok(ReturnsRef::SetOf(_)) => Err(ReturnsError::TableContainingSetOf),
        Ok(ReturnsRef::Table(_)) => Err(ReturnsError::NestedTable),
        Err(err) => Err(err),
    }
}

/// Implements `SqlTranslatable` for a type with a fixed external SQL mapping.
///
/// This macro uses `pgrx_resolved_type!(T)` for `TYPE_IDENT`, sets
/// `TYPE_ORIGIN` to `TypeOrigin::External`, and fills in the const SQL metadata
/// for the common "map this Rust wrapper to an existing SQL type" case.
///
/// Spell out the `unsafe impl SqlTranslatable` instead when (1) the type is owned by
/// this extension or (2) when its argument and return SQL need different mappings.
///
/// This macro is re-exported by `pgrx` and is also available through
/// `pgrx::prelude::*`.
///
/// # Examples
///
/// A wrapper that maps to the existing `uuid` type:
///
/// ```ignore
/// use pgrx::prelude::*;
///
/// pub struct UuidWrapper(uuid::Uuid);
///
/// impl_sql_translatable!(UuidWrapper, "uuid");
/// ```
///
/// An argument-only wrapper for a pseudo-type:
///
/// ```ignore
/// use pgrx::prelude::*;
///
/// pub struct InternalArg(*mut core::ffi::c_void);
///
/// impl_sql_translatable!(InternalArg, arg_only = "internal");
/// ```
#[macro_export]
macro_rules! impl_sql_translatable {
    ($ty:ty, $sql:literal) => {
        unsafe impl $crate::metadata::SqlTranslatable for $ty {
            const TYPE_IDENT: &'static str = $crate::pgrx_resolved_type!($ty);
            const TYPE_ORIGIN: $crate::metadata::TypeOrigin =
                $crate::metadata::TypeOrigin::External;
            const ARGUMENT_SQL: Result<
                $crate::metadata::SqlMappingRef,
                $crate::metadata::ArgumentError,
            > = Ok($crate::metadata::SqlMappingRef::literal($sql));
            const RETURN_SQL: Result<$crate::metadata::ReturnsRef, $crate::metadata::ReturnsError> =
                Ok($crate::metadata::ReturnsRef::One($crate::metadata::SqlMappingRef::literal(
                    $sql,
                )));
        }
    };
    ($ty:ty, arg_only = $sql:literal) => {
        unsafe impl $crate::metadata::SqlTranslatable for $ty {
            const TYPE_IDENT: &'static str = $crate::pgrx_resolved_type!($ty);
            const TYPE_ORIGIN: $crate::metadata::TypeOrigin =
                $crate::metadata::TypeOrigin::External;
            const ARGUMENT_SQL: Result<
                $crate::metadata::SqlMappingRef,
                $crate::metadata::ArgumentError,
            > = Ok($crate::metadata::SqlMappingRef::literal($sql));
            const RETURN_SQL: Result<$crate::metadata::ReturnsRef, $crate::metadata::ReturnsError> =
                Err($crate::metadata::ReturnsError::Datum);
        }
    };
}

/**
A value which can be represented in SQL

If you need the common "fixed external SQL type" case, prefer
`impl_sql_translatable!`. Spell out this trait impl when (1) the type is owned
by this extension or (2) when the argument or return SQL is unusual.

# Safety

By implementing this, you assert you are not lying to either Postgres or Rust in doing so.
This trait asserts a safe translation exists between values of this type from Rust to SQL,
or from SQL into Rust. If you are mistaken about how this works, either the Postgres C API
or the Rust handling in PGRX may emit undefined behavior.

It cannot be made private or sealed due to details of the structure of the PGRX framework.
Nonetheless, if you are not confident the translation is valid: do not implement this trait.
*/
#[diagnostic::on_unimplemented(
    message = "`{Self}` has no representation in SQL",
    label = "non-SQL type"
)]
pub unsafe trait SqlTranslatable {
    const TYPE_IDENT: &'static str;
    const TYPE_ORIGIN: TypeOrigin;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError>;
    const RETURN_SQL: Result<ReturnsRef, ReturnsError>;

    fn type_name() -> &'static str {
        core::any::type_name::<Self>()
    }
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Self::ARGUMENT_SQL.map(Into::into)
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Self::RETURN_SQL.map(Into::into)
    }
    fn entity() -> FunctionMetadataTypeEntity<'static> {
        FunctionMetadataTypeEntity::resolved(
            Self::TYPE_IDENT,
            Self::TYPE_ORIGIN,
            Self::argument_sql(),
            Self::return_sql(),
        )
    }
}

unsafe impl SqlTranslatable for () {
    const TYPE_IDENT: &'static str = crate::pgrx_resolved_type!(());
    const TYPE_ORIGIN: TypeOrigin = TypeOrigin::External;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> =
        Err(ArgumentError::NotValidAsArgument("()"));
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> =
        Ok(ReturnsRef::One(SqlMappingRef::literal("VOID")));
}

unsafe impl<T> SqlTranslatable for Option<T>
where
    T: SqlTranslatable,
{
    const TYPE_IDENT: &'static str = T::TYPE_IDENT;
    const TYPE_ORIGIN: TypeOrigin = T::TYPE_ORIGIN;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> = T::ARGUMENT_SQL;
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> = T::RETURN_SQL;
}

unsafe impl<T> SqlTranslatable for *mut T
where
    T: SqlTranslatable,
{
    const TYPE_IDENT: &'static str = T::TYPE_IDENT;
    const TYPE_ORIGIN: TypeOrigin = T::TYPE_ORIGIN;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> = T::ARGUMENT_SQL;
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> = T::RETURN_SQL;
}

unsafe impl<T, E> SqlTranslatable for Result<T, E>
where
    T: SqlTranslatable,
    E: Any + Display,
{
    const TYPE_IDENT: &'static str = T::TYPE_IDENT;
    const TYPE_ORIGIN: TypeOrigin = T::TYPE_ORIGIN;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> = T::ARGUMENT_SQL;
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> = T::RETURN_SQL;
}

unsafe impl<T> SqlTranslatable for Vec<T>
where
    T: SqlTranslatable,
{
    const TYPE_IDENT: &'static str = T::TYPE_IDENT;
    const TYPE_ORIGIN: TypeOrigin = T::TYPE_ORIGIN;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> = match T::ARGUMENT_SQL {
        Err(ArgumentError::BareU8) => Ok(SqlMappingRef::As("bytea")),
        other => array_argument_sql(other),
    };
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> = match T::RETURN_SQL {
        Err(ReturnsError::BareU8) => Ok(ReturnsRef::One(SqlMappingRef::As("bytea"))),
        other => array_return_sql(other),
    };
}

unsafe impl SqlTranslatable for u8 {
    const TYPE_IDENT: &'static str = crate::pgrx_resolved_type!(u8);
    const TYPE_ORIGIN: TypeOrigin = TypeOrigin::External;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> = Err(ArgumentError::BareU8);
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> = Err(ReturnsError::BareU8);
}

macro_rules! simple_sql_type {
    ($ty:ty, $sql:literal) => {
        unsafe impl SqlTranslatable for $ty {
            const TYPE_IDENT: &'static str = $crate::pgrx_resolved_type!($ty);
            const TYPE_ORIGIN: TypeOrigin = TypeOrigin::External;
            const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> =
                Ok(SqlMappingRef::literal($sql));
            const RETURN_SQL: Result<ReturnsRef, ReturnsError> =
                Ok(ReturnsRef::One(SqlMappingRef::literal($sql)));
        }
    };
}

simple_sql_type!(i32, "INT");
simple_sql_type!(String, "TEXT");
simple_sql_type!(str, "TEXT");
simple_sql_type!([u8], "bytea");
simple_sql_type!(i8, "\"char\"");
simple_sql_type!(i16, "smallint");
simple_sql_type!(i64, "bigint");
simple_sql_type!(bool, "bool");
simple_sql_type!(char, "varchar");
simple_sql_type!(f32, "real");
simple_sql_type!(f64, "double precision");
simple_sql_type!(CString, "cstring");
simple_sql_type!(CStr, "cstring");

unsafe impl<T> SqlTranslatable for &T
where
    T: ?Sized + SqlTranslatable,
{
    const TYPE_IDENT: &'static str = T::TYPE_IDENT;
    const TYPE_ORIGIN: TypeOrigin = T::TYPE_ORIGIN;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> = T::ARGUMENT_SQL;
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> = T::RETURN_SQL;
}

#[cfg(test)]
mod tests {
    use super::*;

    struct MacroExternalType;
    impl_sql_translatable!(MacroExternalType, "uuid");

    struct MacroArgOnlyType;
    impl_sql_translatable!(MacroArgOnlyType, arg_only = "internal");

    #[test]
    fn impl_sql_translatable_sets_external_defaults() {
        assert_eq!(
            <MacroExternalType as SqlTranslatable>::TYPE_IDENT,
            concat!(module_path!(), "::", "MacroExternalType")
        );
        assert_eq!(<MacroExternalType as SqlTranslatable>::TYPE_ORIGIN, TypeOrigin::External);
        assert_eq!(
            <MacroExternalType as SqlTranslatable>::ARGUMENT_SQL,
            Ok(SqlMappingRef::literal("uuid"))
        );
        assert_eq!(
            <MacroExternalType as SqlTranslatable>::RETURN_SQL,
            Ok(ReturnsRef::One(SqlMappingRef::literal("uuid")))
        );
    }

    #[test]
    fn impl_sql_translatable_supports_arg_only_types() {
        assert_eq!(
            <MacroArgOnlyType as SqlTranslatable>::TYPE_IDENT,
            concat!(module_path!(), "::", "MacroArgOnlyType")
        );
        assert_eq!(<MacroArgOnlyType as SqlTranslatable>::TYPE_ORIGIN, TypeOrigin::External);
        assert_eq!(
            <MacroArgOnlyType as SqlTranslatable>::ARGUMENT_SQL,
            Ok(SqlMappingRef::literal("internal"))
        );
        assert_eq!(<MacroArgOnlyType as SqlTranslatable>::RETURN_SQL, Err(ReturnsError::Datum));
    }

    #[test]
    fn array_argument_sql_wraps_scalar_kinds() {
        assert_eq!(
            array_argument_sql(Ok(SqlMappingRef::literal("INT"))),
            Ok(SqlMappingRef::Array(SqlArrayMappingRef::As("INT")))
        );
        assert_eq!(
            array_argument_sql(Ok(SqlMappingRef::Numeric { precision: Some(10), scale: Some(2) })),
            Ok(SqlMappingRef::Array(SqlArrayMappingRef::Numeric {
                precision: Some(10),
                scale: Some(2),
            }))
        );
        assert_eq!(
            array_argument_sql(Ok(SqlMappingRef::Composite)),
            Ok(SqlMappingRef::Array(SqlArrayMappingRef::Composite))
        );
    }

    #[test]
    fn array_return_sql_wraps_scalar_kinds() {
        assert_eq!(
            array_return_sql(Ok(ReturnsRef::One(SqlMappingRef::literal("INT")))),
            Ok(ReturnsRef::One(SqlMappingRef::Array(SqlArrayMappingRef::As("INT"))))
        );
        assert_eq!(
            array_return_sql(Ok(ReturnsRef::One(SqlMappingRef::Numeric {
                precision: Some(10),
                scale: Some(2),
            }))),
            Ok(ReturnsRef::One(SqlMappingRef::Array(SqlArrayMappingRef::Numeric {
                precision: Some(10),
                scale: Some(2),
            })))
        );
        assert_eq!(
            array_return_sql(Ok(ReturnsRef::One(SqlMappingRef::Composite))),
            Ok(ReturnsRef::One(SqlMappingRef::Array(SqlArrayMappingRef::Composite)))
        );
    }

    #[test]
    fn nested_vec_arrays_fail_fast() {
        assert_eq!(
            <Vec<Vec<i32>> as SqlTranslatable>::ARGUMENT_SQL,
            Err(ArgumentError::NestedArray)
        );
        assert_eq!(<Vec<Vec<i32>> as SqlTranslatable>::RETURN_SQL, Err(ReturnsError::NestedArray));
    }

    #[test]
    fn nested_numeric_arrays_fail_fast() {
        let numeric = SqlMappingRef::Array(SqlArrayMappingRef::Numeric {
            precision: Some(10),
            scale: Some(2),
        });
        assert_eq!(array_argument_sql(Ok(numeric)), Err(ArgumentError::NestedArray));
    }

    #[test]
    fn nested_composite_arrays_fail_fast() {
        let composite = SqlMappingRef::Array(SqlArrayMappingRef::Composite);
        assert_eq!(
            array_return_sql(Ok(ReturnsRef::One(composite))),
            Err(ReturnsError::NestedArray)
        );
    }
}
