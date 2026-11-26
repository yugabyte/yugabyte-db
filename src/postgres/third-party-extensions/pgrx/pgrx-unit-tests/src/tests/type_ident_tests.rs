use std::collections::BTreeMap;
use std::error::Error as StdError;

use pgrx::array::FlatArray;
use pgrx::memcx::MemCx;
use pgrx::nullable::Nullable;
use pgrx::pg_sys::FunctionCallInfoBaseData;
use pgrx::pgrx_sql_entity_graph::metadata::{
    ArgumentError, ReturnsError, ReturnsRef, SqlMappingRef, SqlTranslatable, TypeOrigin,
};
use pgrx::prelude::*;
use pgrx::{AnyArray, AnyElement, AnyNumeric, Inet, Internal, Json, JsonB, PgRelation, Uuid};
use serde::{Deserialize, Serialize};

#[derive(Debug)]
struct ManualTypeIdentType;

type ManualTypeIdentAlias = ManualTypeIdentType;

const MANUAL_TYPE_IDENT: &str = pgrx::pgrx_resolved_type!(ManualTypeIdentType);

unsafe impl SqlTranslatable for ManualTypeIdentType {
    const TYPE_IDENT: &'static str = MANUAL_TYPE_IDENT;
    const TYPE_ORIGIN: TypeOrigin = TypeOrigin::ThisExtension;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> =
        Ok(SqlMappingRef::literal("manual_type_ident_type"));
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> =
        Ok(ReturnsRef::One(SqlMappingRef::literal("manual_type_ident_type")));
}

mod nested_manual {
    use super::*;

    #[derive(Debug)]
    pub struct DefinitionSiteType;

    pub const DEFINITION_SITE_TYPE_IDENT: &str = pgrx::pgrx_resolved_type!(DefinitionSiteType);

    unsafe impl SqlTranslatable for DefinitionSiteType {
        const TYPE_IDENT: &'static str = DEFINITION_SITE_TYPE_IDENT;
        const TYPE_ORIGIN: TypeOrigin = TypeOrigin::ThisExtension;
        const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> =
            Ok(SqlMappingRef::literal("definition_site_type"));
        const RETURN_SQL: Result<ReturnsRef, ReturnsError> =
            Ok(ReturnsRef::One(SqlMappingRef::literal("definition_site_type")));
    }
}

use nested_manual::DefinitionSiteType;

type ReexportedDefinitionSiteType = DefinitionSiteType;

#[derive(PostgresType, Serialize, Deserialize, Debug, PartialEq)]
pub struct DerivedTypeIdentType {
    value: i32,
}

#[derive(PostgresEnum, Debug, PartialEq, Eq)]
pub enum DerivedTypeIdentEnum {
    Alpha,
    Beta,
}

fn assert_same_type_ident<Left, Right>()
where
    Left: ?Sized + SqlTranslatable,
    Right: ?Sized + SqlTranslatable,
{
    assert_eq!(Left::TYPE_IDENT, Right::TYPE_IDENT);
}

fn assert_distinct_type_ident<Left, Right>()
where
    Left: ?Sized + SqlTranslatable,
    Right: ?Sized + SqlTranslatable,
{
    assert_ne!(Left::TYPE_IDENT, Right::TYPE_IDENT);
}

#[test]
fn representative_leaf_types_have_non_empty_distinct_type_idents() {
    let leaf_keys = [
        ("bool", <bool as SqlTranslatable>::TYPE_IDENT),
        ("i8", <i8 as SqlTranslatable>::TYPE_IDENT),
        ("i16", <i16 as SqlTranslatable>::TYPE_IDENT),
        ("i32", <i32 as SqlTranslatable>::TYPE_IDENT),
        ("i64", <i64 as SqlTranslatable>::TYPE_IDENT),
        ("u8", <u8 as SqlTranslatable>::TYPE_IDENT),
        ("String", <String as SqlTranslatable>::TYPE_IDENT),
        ("str", <str as SqlTranslatable>::TYPE_IDENT),
        ("Date", <Date as SqlTranslatable>::TYPE_IDENT),
        ("Time", <Time as SqlTranslatable>::TYPE_IDENT),
        ("Timestamp", <Timestamp as SqlTranslatable>::TYPE_IDENT),
        ("TimestampWithTimeZone", <TimestampWithTimeZone as SqlTranslatable>::TYPE_IDENT),
        ("TimeWithTimeZone", <TimeWithTimeZone as SqlTranslatable>::TYPE_IDENT),
        ("Interval", <Interval as SqlTranslatable>::TYPE_IDENT),
        ("AnyArray", <AnyArray as SqlTranslatable>::TYPE_IDENT),
        ("AnyElement", <AnyElement as SqlTranslatable>::TYPE_IDENT),
        ("AnyNumeric", <AnyNumeric as SqlTranslatable>::TYPE_IDENT),
        ("Json", <Json as SqlTranslatable>::TYPE_IDENT),
        ("JsonB", <JsonB as SqlTranslatable>::TYPE_IDENT),
        ("Uuid", <Uuid as SqlTranslatable>::TYPE_IDENT),
        ("Inet", <Inet as SqlTranslatable>::TYPE_IDENT),
        ("Internal", <Internal as SqlTranslatable>::TYPE_IDENT),
        ("Oid", <pg_sys::Oid as SqlTranslatable>::TYPE_IDENT),
        ("PgRelation", <PgRelation as SqlTranslatable>::TYPE_IDENT),
        ("Range<i32>", <Range<i32> as SqlTranslatable>::TYPE_IDENT),
        ("Range<Date>", <Range<Date> as SqlTranslatable>::TYPE_IDENT),
        ("Range<Timestamp>", <Range<Timestamp> as SqlTranslatable>::TYPE_IDENT),
        (
            "Range<TimestampWithTimeZone>",
            <Range<TimestampWithTimeZone> as SqlTranslatable>::TYPE_IDENT,
        ),
        ("ManualTypeIdentType", <ManualTypeIdentType as SqlTranslatable>::TYPE_IDENT),
        ("DefinitionSiteType", <DefinitionSiteType as SqlTranslatable>::TYPE_IDENT),
        ("DerivedTypeIdentType", <DerivedTypeIdentType as SqlTranslatable>::TYPE_IDENT),
        ("DerivedTypeIdentEnum", <DerivedTypeIdentEnum as SqlTranslatable>::TYPE_IDENT),
    ];

    let mut seen = BTreeMap::new();
    for (name, key) in leaf_keys {
        assert!(!key.is_empty(), "{name} should not have an empty type ident");
        if let Some(previous) = seen.insert(key, name) {
            panic!("{name} and {previous} unexpectedly share type ident {key}");
        }
    }
}

#[test]
fn wrapper_types_forward_to_their_inner_type_ident() {
    assert_same_type_ident::<&str, str>();

    assert_same_type_ident::<Option<i32>, i32>();
    assert_same_type_ident::<Result<i32, std::io::Error>, i32>();
    assert_same_type_ident::<Vec<i32>, i32>();
    assert_same_type_ident::<Vec<u8>, u8>();
    assert_same_type_ident::<&i32, i32>();
    assert_same_type_ident::<*mut i32, i32>();
    assert_same_type_ident::<Nullable<i32>, i32>();
    assert_same_type_ident::<Array<'static, i32>, i32>();
    assert_same_type_ident::<VariadicArray<'static, i32>, i32>();
    assert_same_type_ident::<FlatArray<'static, i32>, i32>();
    assert_same_type_ident::<PgBox<i32, AllocatedByRust>, i32>();
    assert_same_type_ident::<PgBox<i32, AllocatedByPostgres>, i32>();
    assert_same_type_ident::<SetOfIterator<'static, i32>, i32>();

    assert_same_type_ident::<Option<ManualTypeIdentType>, ManualTypeIdentType>();
    assert_same_type_ident::<Result<ManualTypeIdentType, Box<dyn StdError>>, ManualTypeIdentType>();
}

#[test]
fn wrapper_types_forward_type_origin() {
    assert_eq!(<ManualTypeIdentType as SqlTranslatable>::TYPE_ORIGIN, TypeOrigin::ThisExtension);
    assert_eq!(
        <Option<ManualTypeIdentType> as SqlTranslatable>::TYPE_ORIGIN,
        TypeOrigin::ThisExtension
    );
    assert_eq!(
        <Result<ManualTypeIdentType, Box<dyn StdError>> as SqlTranslatable>::TYPE_ORIGIN,
        TypeOrigin::ThisExtension
    );

    assert_eq!(<Uuid as SqlTranslatable>::TYPE_ORIGIN, TypeOrigin::External);
    assert_eq!(<pg_sys::Oid as SqlTranslatable>::TYPE_ORIGIN, TypeOrigin::External);
    assert_eq!(<Nullable<Uuid> as SqlTranslatable>::TYPE_ORIGIN, TypeOrigin::External);
    assert_eq!(<Array<'static, Uuid> as SqlTranslatable>::TYPE_ORIGIN, TypeOrigin::External);
    assert_eq!(
        <VariadicArray<'static, Uuid> as SqlTranslatable>::TYPE_ORIGIN,
        TypeOrigin::External
    );
}

#[test]
fn table_iterators_keep_their_own_type_identity() {
    type OneColumnTable = TableIterator<'static, (name!(id, i32),)>;
    type TwoColumnTable = TableIterator<'static, (name!(id, i32), name!(label, String))>;

    assert_distinct_type_ident::<OneColumnTable, i32>();
    assert_distinct_type_ident::<TwoColumnTable, i32>();
    assert_distinct_type_ident::<OneColumnTable, TwoColumnTable>();
    assert_same_type_ident::<Result<OneColumnTable, std::io::Error>, OneColumnTable>();
}

#[test]
fn custom_types_use_definition_site_type_idents() {
    assert_eq!(<ManualTypeIdentType as SqlTranslatable>::TYPE_IDENT, MANUAL_TYPE_IDENT);
    assert_eq!(<ManualTypeIdentAlias as SqlTranslatable>::TYPE_IDENT, MANUAL_TYPE_IDENT);

    assert_eq!(
        <DefinitionSiteType as SqlTranslatable>::TYPE_IDENT,
        nested_manual::DEFINITION_SITE_TYPE_IDENT
    );
    assert_eq!(
        <ReexportedDefinitionSiteType as SqlTranslatable>::TYPE_IDENT,
        nested_manual::DEFINITION_SITE_TYPE_IDENT
    );

    assert_eq!(
        <DerivedTypeIdentType as SqlTranslatable>::TYPE_IDENT,
        pgrx::pgrx_resolved_type!(DerivedTypeIdentType)
    );
    assert_eq!(
        <DerivedTypeIdentEnum as SqlTranslatable>::TYPE_IDENT,
        pgrx::pgrx_resolved_type!(DerivedTypeIdentEnum)
    );
}

#[test]
fn skipped_virtual_types_stay_skipped() {
    assert_eq!(<&'static MemCx<'static> as SqlTranslatable>::ARGUMENT_SQL, Ok(SqlMappingRef::Skip));
    assert_eq!(
        <FunctionCallInfoBaseData as SqlTranslatable>::ARGUMENT_SQL,
        Ok(SqlMappingRef::Skip)
    );
    assert_eq!(
        <*mut FunctionCallInfoBaseData as SqlTranslatable>::ARGUMENT_SQL,
        Ok(SqlMappingRef::Skip)
    );
}
