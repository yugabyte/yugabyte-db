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
Wire format support for the embedded pgrx schema linker section.

This module owns the bytes we embed into the extension shared object and later
decode from `cargo-pgrx schema`. That format is intentionally compact and
const-friendly because the producer side runs through macro expansion into
`static` linker-section entries, not through a normal runtime serializer.

Why this is a bespoke binary format instead of JSON:

- the producer must be able to size each entry at compile time
- the producer must emit raw bytes into a `static` item placed in a custom
  linker section
- the metadata we serialize is exposed to macro expansion through associated
  consts such as `SqlTranslatable::{ARGUMENT_SQL, RETURN_SQL}`

JSON would make the decode side simpler, but it would not remove the hard part.
We would still need a handwritten const-time encoder, plus escaping logic for
strings, SQL snippets, paths, and error payloads. That is not a clear win over
the current length-prefixed binary layout.

So the current design choice is:

- keep the section format binary while the producer is const-time linker-section
  emission
- keep the wire format logic centralized in this module instead of spreading it
  across macro emitters

If we ever move away from linker-section bytes and toward a normal build-time
manifest artifact, that would be the right time to re-evaluate a serde-based
format such as JSON.
*/

use crate::aggregate::entity::{AggregateTypeEntity, PgAggregateEntity};
use crate::aggregate::{FinalizeModify, ParallelOption};
use crate::extension_sql::entity::{
    ExtensionSqlEntity, SqlDeclaredEntity, SqlDeclaredFunctionEntityData, SqlDeclaredTypeEntityData,
};
use crate::extern_args::ExternArgs;
use crate::metadata::{
    ArgumentError, FunctionMetadataTypeEntity, Returns, ReturnsError, ReturnsRef, SqlArrayMapping,
    SqlArrayMappingRef, SqlMapping, SqlMappingRef, TypeOrigin,
};
use crate::pg_extern::entity::{
    PgCastEntity, PgExternArgumentEntity, PgExternEntity, PgExternReturnEntity,
    PgExternReturnEntityIteratedItem, PgOperatorEntity,
};
use crate::pg_trigger::entity::PgTriggerEntity;
use crate::positioning_ref::PositioningRef;
use crate::postgres_enum::entity::PostgresEnumEntity;
use crate::postgres_hash::entity::PostgresHashEntity;
use crate::postgres_ord::entity::PostgresOrdEntity;
use crate::postgres_type::entity::PostgresTypeEntity;
use crate::schema::entity::SchemaEntity;
use crate::to_sql::entity::ToSqlConfigEntity;
use crate::{SqlGraphEntity, UsedTypeEntity};
use eyre::{Result, bail, eyre};

pub const ELF_SECTION_NAME: &str = ".pgrxsc";
pub const MACHO_SEGMENT_NAME: &str = "__DATA";
pub const MACHO_SECTION_NAME: &str = "__pgrxsc";
pub const MACHO_SECTION_PATH: &str = "__DATA,__pgrxsc";

// PE/COFF section names are capped at 8 bytes, which is why the cross-platform
// names here are the shortened `pgrxsc` forms instead of `.pgrx_schema`.
const LEGACY_ELF_SECTION_NAME: &str = ".pgrx_schema";
const LEGACY_MACHO_SECTION_NAME: &str = "__pgrx_schema";
const LEGACY_MACHO_SECTION_PATH: &str = "__DATA,__pgrx_schema";

pub const ENTITY_SENTINEL: u8 = 0;
pub const ENTITY_SCHEMA: u8 = 1;
pub const ENTITY_CUSTOM_SQL: u8 = 2;
pub const ENTITY_FUNCTION: u8 = 3;
pub const ENTITY_TYPE: u8 = 4;
pub const ENTITY_ENUM: u8 = 5;
pub const ENTITY_ORD: u8 = 6;
pub const ENTITY_HASH: u8 = 7;
pub const ENTITY_AGGREGATE: u8 = 8;
pub const ENTITY_TRIGGER: u8 = 9;

pub const POSITIONING_REF_FULL_PATH: u8 = 1;
pub const POSITIONING_REF_NAME: u8 = 2;

pub const SQL_DECLARED_TYPE: u8 = 1;
pub const SQL_DECLARED_ENUM: u8 = 2;
pub const SQL_DECLARED_FUNCTION: u8 = 3;

pub const SQL_MAPPING_AS: u8 = 1;
pub const SQL_MAPPING_ARRAY: u8 = 2;
pub const SQL_MAPPING_NUMERIC: u8 = 3;
pub const SQL_MAPPING_COMPOSITE: u8 = 4;
pub const SQL_MAPPING_SKIP: u8 = 5;

pub const RETURNS_ONE: u8 = 1;
pub const RETURNS_SET_OF: u8 = 2;
pub const RETURNS_TABLE: u8 = 3;

pub const ARG_ERROR_SET_OF: u8 = 1;
pub const ARG_ERROR_TABLE: u8 = 2;
pub const ARG_ERROR_NESTED_ARRAY: u8 = 3;
pub const ARG_ERROR_BARE_U8: u8 = 4;
pub const ARG_ERROR_SKIP_IN_ARRAY: u8 = 5;
pub const ARG_ERROR_DATUM: u8 = 6;
pub const ARG_ERROR_NOT_VALID: u8 = 7;
pub const RESULT_OK: u8 = 1;
pub const RESULT_ERR: u8 = 2;

pub const TYPE_ORIGIN_THIS_EXTENSION: u8 = 1;
pub const TYPE_ORIGIN_EXTERNAL: u8 = 2;

pub const RETURNS_ERROR_NESTED_SET_OF: u8 = 1;
pub const RETURNS_ERROR_NESTED_TABLE: u8 = 2;
pub const RETURNS_ERROR_NESTED_ARRAY: u8 = 3;
pub const RETURNS_ERROR_SET_OF_CONTAINING_TABLE: u8 = 4;
pub const RETURNS_ERROR_TABLE_CONTAINING_SET_OF: u8 = 5;
pub const RETURNS_ERROR_SET_OF_IN_ARRAY: u8 = 6;
pub const RETURNS_ERROR_TABLE_IN_ARRAY: u8 = 7;
pub const RETURNS_ERROR_BARE_U8: u8 = 8;
pub const RETURNS_ERROR_SKIP_IN_ARRAY: u8 = 9;
pub const RETURNS_ERROR_DATUM: u8 = 10;

pub const EXTERN_ARG_CREATE_OR_REPLACE: u8 = 1;
pub const EXTERN_ARG_IMMUTABLE: u8 = 2;
pub const EXTERN_ARG_STRICT: u8 = 3;
pub const EXTERN_ARG_STABLE: u8 = 4;
pub const EXTERN_ARG_VOLATILE: u8 = 5;
pub const EXTERN_ARG_RAW: u8 = 6;
pub const EXTERN_ARG_NO_GUARD: u8 = 7;
pub const EXTERN_ARG_SECURITY_DEFINER: u8 = 8;
pub const EXTERN_ARG_SECURITY_INVOKER: u8 = 9;
pub const EXTERN_ARG_PARALLEL_SAFE: u8 = 10;
pub const EXTERN_ARG_PARALLEL_UNSAFE: u8 = 11;
pub const EXTERN_ARG_PARALLEL_RESTRICTED: u8 = 12;
pub const EXTERN_ARG_SHOULD_PANIC: u8 = 13;
pub const EXTERN_ARG_SCHEMA: u8 = 14;
pub const EXTERN_ARG_SUPPORT: u8 = 15;
pub const EXTERN_ARG_NAME: u8 = 16;
pub const EXTERN_ARG_COST: u8 = 17;
pub const EXTERN_ARG_REQUIRES: u8 = 18;

pub const OPERATOR_CAST_DEFAULT: u8 = 1;
pub const OPERATOR_CAST_ASSIGNMENT: u8 = 2;
pub const OPERATOR_CAST_IMPLICIT: u8 = 3;

pub const EXTERN_RET_NONE: u8 = 1;
pub const EXTERN_RET_TYPE: u8 = 2;
pub const EXTERN_RET_SET_OF: u8 = 3;
pub const EXTERN_RET_ITERATED: u8 = 4;
pub const EXTERN_RET_TRIGGER: u8 = 5;

pub const AGGREGATE_FINALIZE_READ_ONLY: u8 = 1;
pub const AGGREGATE_FINALIZE_SHAREABLE: u8 = 2;
pub const AGGREGATE_FINALIZE_READ_WRITE: u8 = 3;

pub const AGGREGATE_PARALLEL_SAFE: u8 = 1;
pub const AGGREGATE_PARALLEL_RESTRICTED: u8 = 2;
pub const AGGREGATE_PARALLEL_UNSAFE: u8 = 3;

pub const SECTION_SENTINEL_MAGIC: &str = "pgrx";
pub const SECTION_SENTINEL_PAYLOAD_LEN: usize = u8_len() + str_len(SECTION_SENTINEL_MAGIC);
pub const SECTION_SENTINEL_ENTRY_LEN: usize = u32_len() + SECTION_SENTINEL_PAYLOAD_LEN;
pub const SECTION_SENTINEL_PAYLOAD: [u8; SECTION_SENTINEL_PAYLOAD_LEN] =
    EntryWriter::<SECTION_SENTINEL_PAYLOAD_LEN>::new()
        .u8(ENTITY_SENTINEL)
        .str(SECTION_SENTINEL_MAGIC)
        .finish();

pub fn is_schema_section_name(name: &str) -> bool {
    name == ELF_SECTION_NAME
        || name == MACHO_SECTION_NAME
        || name == MACHO_SECTION_PATH
        || name == LEGACY_ELF_SECTION_NAME
        || name == LEGACY_MACHO_SECTION_NAME
        || name == LEGACY_MACHO_SECTION_PATH
}

#[macro_export]
macro_rules! __pgrx_schema_entry {
    ($name:ident, $len:expr, $payload:expr) => {
        #[doc(hidden)]
        #[used]
        #[allow(non_upper_case_globals)]
        #[cfg_attr(target_os = "macos", unsafe(link_section = "__DATA,__pgrxsc"))]
        #[cfg_attr(not(target_os = "macos"), unsafe(link_section = ".pgrxsc"))]
        static $name: [u8; $len] = $payload;
    };
}

pub const fn bytes_len(len: usize) -> usize {
    4 + len
}

pub const fn str_len(value: &str) -> usize {
    bytes_len(value.len())
}

pub const fn bool_len() -> usize {
    1
}

pub const fn u8_len() -> usize {
    1
}

pub const fn u32_len() -> usize {
    4
}

pub const fn opt_len(inner: Option<usize>) -> usize {
    1 + match inner {
        Some(inner) => inner,
        None => 0,
    }
}

pub const fn list_len(items: &[usize]) -> usize {
    let mut total = 4;
    let mut i = 0;
    while i < items.len() {
        total += items[i];
        i += 1;
    }
    total
}

pub const fn sql_mapping_len(value: SqlMappingRef) -> usize {
    match value {
        SqlMappingRef::As(sql) => u8_len() + str_len(sql),
        SqlMappingRef::Numeric { .. } => u8_len() + bool_len() + u32_len() + bool_len() + u32_len(),
        SqlMappingRef::Composite => u8_len(),
        SqlMappingRef::Array(value) => u8_len() + sql_array_mapping_len(value),
        SqlMappingRef::Skip => u8_len(),
    }
}

pub const fn sql_array_mapping_len(value: SqlArrayMappingRef) -> usize {
    match value {
        SqlArrayMappingRef::As(sql) => u8_len() + str_len(sql),
        SqlArrayMappingRef::Numeric { .. } => {
            u8_len() + bool_len() + u32_len() + bool_len() + u32_len()
        }
        SqlArrayMappingRef::Composite => u8_len(),
    }
}

pub const fn returns_len(value: ReturnsRef) -> usize {
    match value {
        ReturnsRef::One(mapping) | ReturnsRef::SetOf(mapping) => {
            u8_len() + sql_mapping_len(mapping)
        }
        ReturnsRef::Table(items) => {
            let mut total = u8_len() + u32_len();
            let mut i = 0;
            while i < items.len() {
                total += sql_mapping_len(items[i]);
                i += 1;
            }
            total
        }
    }
}

pub const fn argument_error_len(value: ArgumentError) -> usize {
    match value {
        ArgumentError::NotValidAsArgument(name) => u8_len() + str_len(name),
        _ => u8_len(),
    }
}

pub const fn argument_sql_len(value: Result<SqlMappingRef, ArgumentError>) -> usize {
    u8_len()
        + match value {
            Ok(mapping) => sql_mapping_len(mapping),
            Err(err) => argument_error_len(err),
        }
}

pub const fn returns_error_len(_value: ReturnsError) -> usize {
    u8_len()
}

pub const fn return_sql_len(value: Result<ReturnsRef, ReturnsError>) -> usize {
    u8_len()
        + match value {
            Ok(returns) => returns_len(returns),
            Err(err) => returns_error_len(err),
        }
}

pub const fn function_metadata_type_len(
    resolution: Option<&str>,
    argument_sql: Result<SqlMappingRef, ArgumentError>,
    return_sql: Result<ReturnsRef, ReturnsError>,
) -> usize {
    bool_len()
        + match resolution {
            Some(type_ident) => str_len(type_ident) + u8_len(),
            None => 0,
        }
        + argument_sql_len(argument_sql)
        + return_sql_len(return_sql)
}

#[derive(Clone, Copy)]
pub struct EntryWriter<const N: usize> {
    buf: [u8; N],
    pos: usize,
}

impl<const N: usize> EntryWriter<N> {
    pub const fn new() -> Self {
        Self { buf: [0; N], pos: 0 }
    }

    pub const fn u8(mut self, value: u8) -> Self {
        self.buf[self.pos] = value;
        self.pos += 1;
        self
    }

    pub const fn bool(self, value: bool) -> Self {
        self.u8(if value { 1 } else { 0 })
    }

    pub const fn u32(self, value: u32) -> Self {
        let [b0, b1, b2, b3] = value.to_le_bytes();
        self.u8(b0).u8(b1).u8(b2).u8(b3)
    }

    pub const fn type_origin(self, value: TypeOrigin) -> Self {
        self.u8(match value {
            TypeOrigin::ThisExtension => TYPE_ORIGIN_THIS_EXTENSION,
            TypeOrigin::External => TYPE_ORIGIN_EXTERNAL,
        })
    }

    pub const fn bytes(mut self, value: &[u8]) -> Self {
        let mut i = 0;
        while i < value.len() {
            self.buf[self.pos] = value[i];
            self.pos += 1;
            i += 1;
        }
        self
    }

    pub const fn str(self, value: &str) -> Self {
        self.u32(value.len() as u32).bytes(value.as_bytes())
    }

    pub const fn sql_mapping(self, value: SqlMappingRef) -> Self {
        match value {
            SqlMappingRef::As(sql) => self.u8(SQL_MAPPING_AS).str(sql),
            SqlMappingRef::Numeric { precision, scale } => self
                .u8(SQL_MAPPING_NUMERIC)
                .bool(precision.is_some())
                .u32(match precision {
                    Some(value) => value,
                    None => 0,
                })
                .bool(scale.is_some())
                .u32(match scale {
                    Some(value) => value,
                    None => 0,
                }),
            SqlMappingRef::Composite => self.u8(SQL_MAPPING_COMPOSITE),
            SqlMappingRef::Array(value) => self.u8(SQL_MAPPING_ARRAY).sql_array_mapping(value),
            SqlMappingRef::Skip => self.u8(SQL_MAPPING_SKIP),
        }
    }

    pub const fn sql_array_mapping(self, value: SqlArrayMappingRef) -> Self {
        match value {
            SqlArrayMappingRef::As(sql) => self.u8(SQL_MAPPING_AS).str(sql),
            SqlArrayMappingRef::Numeric { precision, scale } => self
                .u8(SQL_MAPPING_NUMERIC)
                .bool(precision.is_some())
                .u32(match precision {
                    Some(value) => value,
                    None => 0,
                })
                .bool(scale.is_some())
                .u32(match scale {
                    Some(value) => value,
                    None => 0,
                }),
            SqlArrayMappingRef::Composite => self.u8(SQL_MAPPING_COMPOSITE),
        }
    }

    pub const fn returns(self, value: ReturnsRef) -> Self {
        match value {
            ReturnsRef::One(mapping) => self.u8(RETURNS_ONE).sql_mapping(mapping),
            ReturnsRef::SetOf(mapping) => self.u8(RETURNS_SET_OF).sql_mapping(mapping),
            ReturnsRef::Table(items) => {
                let mut writer = self.u8(RETURNS_TABLE).u32(items.len() as u32);
                let mut i = 0;
                while i < items.len() {
                    writer = writer.sql_mapping(items[i]);
                    i += 1;
                }
                writer
            }
        }
    }

    pub const fn argument_error(self, value: ArgumentError) -> Self {
        match value {
            ArgumentError::SetOf => self.u8(ARG_ERROR_SET_OF),
            ArgumentError::Table => self.u8(ARG_ERROR_TABLE),
            ArgumentError::NestedArray => self.u8(ARG_ERROR_NESTED_ARRAY),
            ArgumentError::BareU8 => self.u8(ARG_ERROR_BARE_U8),
            ArgumentError::SkipInArray => self.u8(ARG_ERROR_SKIP_IN_ARRAY),
            ArgumentError::Datum => self.u8(ARG_ERROR_DATUM),
            ArgumentError::NotValidAsArgument(name) => self.u8(ARG_ERROR_NOT_VALID).str(name),
        }
    }

    pub const fn argument_sql(self, value: Result<SqlMappingRef, ArgumentError>) -> Self {
        match value {
            Ok(mapping) => self.u8(RESULT_OK).sql_mapping(mapping),
            Err(err) => self.u8(RESULT_ERR).argument_error(err),
        }
    }

    pub const fn returns_error(self, value: ReturnsError) -> Self {
        match value {
            ReturnsError::NestedSetOf => self.u8(RETURNS_ERROR_NESTED_SET_OF),
            ReturnsError::NestedTable => self.u8(RETURNS_ERROR_NESTED_TABLE),
            ReturnsError::NestedArray => self.u8(RETURNS_ERROR_NESTED_ARRAY),
            ReturnsError::SetOfContainingTable => self.u8(RETURNS_ERROR_SET_OF_CONTAINING_TABLE),
            ReturnsError::TableContainingSetOf => self.u8(RETURNS_ERROR_TABLE_CONTAINING_SET_OF),
            ReturnsError::SetOfInArray => self.u8(RETURNS_ERROR_SET_OF_IN_ARRAY),
            ReturnsError::TableInArray => self.u8(RETURNS_ERROR_TABLE_IN_ARRAY),
            ReturnsError::BareU8 => self.u8(RETURNS_ERROR_BARE_U8),
            ReturnsError::SkipInArray => self.u8(RETURNS_ERROR_SKIP_IN_ARRAY),
            ReturnsError::Datum => self.u8(RETURNS_ERROR_DATUM),
        }
    }

    pub const fn return_sql(self, value: Result<ReturnsRef, ReturnsError>) -> Self {
        match value {
            Ok(returns) => self.u8(RESULT_OK).returns(returns),
            Err(err) => self.u8(RESULT_ERR).returns_error(err),
        }
    }

    pub const fn function_metadata_type(
        self,
        resolution: Option<(&str, TypeOrigin)>,
        argument_sql: Result<SqlMappingRef, ArgumentError>,
        return_sql: Result<ReturnsRef, ReturnsError>,
    ) -> Self {
        let writer = match resolution {
            Some((type_ident, type_origin)) => {
                self.bool(true).str(type_ident).type_origin(type_origin)
            }
            None => self.bool(false),
        };
        writer.argument_sql(argument_sql).return_sql(return_sql)
    }

    pub const fn finish(self) -> [u8; N] {
        if self.pos != N {
            panic!("pgrx schema entry length mismatch");
        }
        self.buf
    }
}

impl<const N: usize> Default for EntryWriter<N> {
    fn default() -> Self {
        Self::new()
    }
}

pub const fn schema_section_sentinel_entry() -> [u8; SECTION_SENTINEL_ENTRY_LEN] {
    EntryWriter::<SECTION_SENTINEL_ENTRY_LEN>::new()
        .u32(SECTION_SENTINEL_PAYLOAD_LEN as u32)
        .bytes(&SECTION_SENTINEL_PAYLOAD)
        .finish()
}

pub struct EntryReader<'a> {
    bytes: &'a [u8],
    pos: usize,
}

impl<'a> EntryReader<'a> {
    pub fn new(bytes: &'a [u8]) -> Self {
        Self { bytes, pos: 0 }
    }

    pub fn is_empty(&self) -> bool {
        self.pos >= self.bytes.len()
    }

    pub fn remaining(&self) -> usize {
        self.bytes.len().saturating_sub(self.pos)
    }

    pub fn read_u8(&mut self) -> Result<u8> {
        if self.remaining() < 1 {
            bail!("unexpected end of schema entry");
        }
        let value = self.bytes[self.pos];
        self.pos += 1;
        Ok(value)
    }

    pub fn read_bool(&mut self) -> Result<bool> {
        match self.read_u8()? {
            0 => Ok(false),
            1 => Ok(true),
            other => Err(eyre!("invalid bool tag in schema entry: {other}")),
        }
    }

    pub fn read_u32(&mut self) -> Result<u32> {
        if self.remaining() < 4 {
            bail!("unexpected end of schema entry");
        }
        let start = self.pos;
        self.pos += 4;
        Ok(u32::from_le_bytes(
            self.bytes[start..self.pos].try_into().expect("checked slice length"),
        ))
    }

    pub fn read_bytes(&mut self) -> Result<&'a [u8]> {
        let len = self.read_u32()? as usize;
        if self.remaining() < len {
            bail!("unexpected end of schema entry");
        }
        let start = self.pos;
        self.pos += len;
        Ok(&self.bytes[start..self.pos])
    }

    pub fn read_string(&mut self) -> Result<String> {
        let bytes = self.read_bytes()?;
        Ok(std::str::from_utf8(bytes)
            .map_err(|err| eyre!("schema entry contained invalid utf8: {err}"))?
            .to_owned())
    }

    pub fn read_str(&mut self) -> Result<&'a str> {
        let bytes = self.read_bytes()?;
        std::str::from_utf8(bytes)
            .map_err(|err| eyre!("schema entry contained invalid utf8: {err}"))
    }

    pub fn read_option_str(&mut self) -> Result<Option<&'a str>> {
        if self.read_bool()? { Ok(Some(self.read_str()?)) } else { Ok(None) }
    }

    pub fn read_option_string(&mut self) -> Result<Option<String>> {
        if self.read_bool()? { Ok(Some(self.read_string()?)) } else { Ok(None) }
    }

    pub fn read_sql_mapping_owned(&mut self) -> Result<SqlMapping> {
        match self.read_u8()? {
            SQL_MAPPING_AS => Ok(SqlMapping::As(self.read_string()?)),
            SQL_MAPPING_ARRAY => Ok(SqlMapping::Array(self.read_sql_array_mapping_owned()?)),
            SQL_MAPPING_NUMERIC => {
                let has_precision = self.read_bool()?;
                let precision = self.read_u32()?;
                let has_scale = self.read_bool()?;
                let scale = self.read_u32()?;
                Ok(SqlMapping::As(crate::metadata::numeric_sql_string(
                    has_precision.then_some(precision),
                    has_scale.then_some(scale),
                )))
            }
            SQL_MAPPING_COMPOSITE => Ok(SqlMapping::Composite),
            SQL_MAPPING_SKIP => Ok(SqlMapping::Skip),
            other => Err(eyre!("invalid sql mapping tag in schema entry: {other}")),
        }
    }

    pub fn read_sql_array_mapping_owned(&mut self) -> Result<SqlArrayMapping> {
        match self.read_u8()? {
            SQL_MAPPING_AS => Ok(SqlArrayMapping::As(self.read_string()?)),
            SQL_MAPPING_NUMERIC => {
                let has_precision = self.read_bool()?;
                let precision = self.read_u32()?;
                let has_scale = self.read_bool()?;
                let scale = self.read_u32()?;
                Ok(SqlArrayMapping::As(crate::metadata::numeric_sql_string(
                    has_precision.then_some(precision),
                    has_scale.then_some(scale),
                )))
            }
            SQL_MAPPING_COMPOSITE => Ok(SqlArrayMapping::Composite),
            other => Err(eyre!("invalid sql array mapping tag in schema entry: {other}")),
        }
    }

    pub fn read_returns_owned(&mut self) -> Result<Returns> {
        match self.read_u8()? {
            RETURNS_ONE => Ok(Returns::One(self.read_sql_mapping_owned()?)),
            RETURNS_SET_OF => Ok(Returns::SetOf(self.read_sql_mapping_owned()?)),
            RETURNS_TABLE => {
                let count = self.read_u32()? as usize;
                let mut items = Vec::with_capacity(count);
                for _ in 0..count {
                    items.push(self.read_sql_mapping_owned()?);
                }
                Ok(Returns::Table(items))
            }
            other => Err(eyre!("invalid returns tag in schema entry: {other}")),
        }
    }

    pub fn read_argument_sql_owned(&mut self) -> Result<Result<SqlMapping, ArgumentError>> {
        match self.read_u8()? {
            RESULT_OK => Ok(Ok(self.read_sql_mapping_owned()?)),
            RESULT_ERR => Ok(Err(self.read_argument_error()?)),
            other => Err(eyre!("invalid argument sql tag in schema entry: {other}")),
        }
    }

    pub fn read_return_sql_owned(&mut self) -> Result<Result<Returns, ReturnsError>> {
        match self.read_u8()? {
            RESULT_OK => Ok(Ok(self.read_returns_owned()?)),
            RESULT_ERR => Ok(Err(self.read_returns_error()?)),
            other => Err(eyre!("invalid return sql tag in schema entry: {other}")),
        }
    }

    pub fn read_argument_error(&mut self) -> Result<ArgumentError> {
        match self.read_u8()? {
            ARG_ERROR_SET_OF => Ok(ArgumentError::SetOf),
            ARG_ERROR_TABLE => Ok(ArgumentError::Table),
            ARG_ERROR_NESTED_ARRAY => Ok(ArgumentError::NestedArray),
            ARG_ERROR_BARE_U8 => Ok(ArgumentError::BareU8),
            ARG_ERROR_SKIP_IN_ARRAY => Ok(ArgumentError::SkipInArray),
            ARG_ERROR_DATUM => Ok(ArgumentError::Datum),
            ARG_ERROR_NOT_VALID => {
                // ArgumentError::NotValidAsArgument requires &'static str for const compatibility.
                // This is the one remaining leak — a tiny string for a rare error variant.
                Ok(ArgumentError::NotValidAsArgument(Box::leak(
                    self.read_string()?.into_boxed_str(),
                )))
            }
            other => Err(eyre!("invalid argument error tag in schema entry: {other}")),
        }
    }

    pub fn read_returns_error(&mut self) -> Result<ReturnsError> {
        match self.read_u8()? {
            RETURNS_ERROR_NESTED_SET_OF => Ok(ReturnsError::NestedSetOf),
            RETURNS_ERROR_NESTED_TABLE => Ok(ReturnsError::NestedTable),
            RETURNS_ERROR_NESTED_ARRAY => Ok(ReturnsError::NestedArray),
            RETURNS_ERROR_SET_OF_CONTAINING_TABLE => Ok(ReturnsError::SetOfContainingTable),
            RETURNS_ERROR_TABLE_CONTAINING_SET_OF => Ok(ReturnsError::TableContainingSetOf),
            RETURNS_ERROR_SET_OF_IN_ARRAY => Ok(ReturnsError::SetOfInArray),
            RETURNS_ERROR_TABLE_IN_ARRAY => Ok(ReturnsError::TableInArray),
            RETURNS_ERROR_BARE_U8 => Ok(ReturnsError::BareU8),
            RETURNS_ERROR_SKIP_IN_ARRAY => Ok(ReturnsError::SkipInArray),
            RETURNS_ERROR_DATUM => Ok(ReturnsError::Datum),
            other => Err(eyre!("invalid returns error tag in schema entry: {other}")),
        }
    }

    pub fn read_positioning_ref(&mut self) -> Result<PositioningRef> {
        match self.read_u8()? {
            POSITIONING_REF_FULL_PATH => Ok(PositioningRef::FullPath(self.read_string()?)),
            POSITIONING_REF_NAME => Ok(PositioningRef::Name(self.read_string()?)),
            other => Err(eyre!("invalid positioning ref tag in schema entry: {other}")),
        }
    }

    pub fn read_to_sql_config(&mut self) -> Result<ToSqlConfigEntity<'a>> {
        let enabled = self.read_bool()?;
        let content = self.read_option_str()?;
        Ok(ToSqlConfigEntity { enabled, content })
    }

    pub fn read_function_metadata_type(&mut self) -> Result<FunctionMetadataTypeEntity<'a>> {
        let resolution = if self.read_bool()? {
            Some(crate::metadata::FunctionMetadataTypeResolutionEntity {
                type_ident: self.read_str()?,
                type_origin: self.read_type_origin()?,
            })
        } else {
            None
        };
        Ok(FunctionMetadataTypeEntity {
            resolution,
            argument_sql: self.read_argument_sql_owned()?,
            return_sql: self.read_return_sql_owned()?,
        })
    }

    pub fn read_type_origin(&mut self) -> Result<TypeOrigin> {
        match self.read_u8()? {
            TYPE_ORIGIN_THIS_EXTENSION => Ok(TypeOrigin::ThisExtension),
            TYPE_ORIGIN_EXTERNAL => Ok(TypeOrigin::External),
            value => Err(eyre!("Unknown type origin discriminator `{value}`")),
        }
    }

    pub fn read_used_type(&mut self) -> Result<UsedTypeEntity<'a>> {
        Ok(UsedTypeEntity {
            ty_source: self.read_str()?,
            full_path: self.read_str()?,
            composite_type: self.read_option_str()?,
            variadic: self.read_bool()?,
            default: self.read_option_str()?,
            optional: self.read_bool()?,
            metadata: self.read_function_metadata_type()?,
        })
    }

    pub fn read_pg_extern_argument(&mut self) -> Result<PgExternArgumentEntity<'a>> {
        Ok(PgExternArgumentEntity { pattern: self.read_str()?, used_ty: self.read_used_type()? })
    }

    pub fn read_pg_extern_return(&mut self) -> Result<PgExternReturnEntity<'a>> {
        match self.read_u8()? {
            EXTERN_RET_NONE => Ok(PgExternReturnEntity::None),
            EXTERN_RET_TYPE => Ok(PgExternReturnEntity::Type { ty: self.read_used_type()? }),
            EXTERN_RET_SET_OF => Ok(PgExternReturnEntity::SetOf { ty: self.read_used_type()? }),
            EXTERN_RET_ITERATED => {
                let count = self.read_u32()? as usize;
                let mut tys = Vec::with_capacity(count);
                for _ in 0..count {
                    tys.push(PgExternReturnEntityIteratedItem {
                        name: self.read_option_str()?,
                        ty: self.read_used_type()?,
                    });
                }
                Ok(PgExternReturnEntity::Iterated { tys })
            }
            EXTERN_RET_TRIGGER => Ok(PgExternReturnEntity::Trigger),
            other => Err(eyre!("invalid extern return tag in schema entry: {other}")),
        }
    }

    pub fn read_extern_arg(&mut self) -> Result<ExternArgs> {
        match self.read_u8()? {
            EXTERN_ARG_CREATE_OR_REPLACE => Ok(ExternArgs::CreateOrReplace),
            EXTERN_ARG_IMMUTABLE => Ok(ExternArgs::Immutable),
            EXTERN_ARG_STRICT => Ok(ExternArgs::Strict),
            EXTERN_ARG_STABLE => Ok(ExternArgs::Stable),
            EXTERN_ARG_VOLATILE => Ok(ExternArgs::Volatile),
            EXTERN_ARG_RAW => Ok(ExternArgs::Raw),
            EXTERN_ARG_NO_GUARD => Ok(ExternArgs::NoGuard),
            EXTERN_ARG_SECURITY_DEFINER => Ok(ExternArgs::SecurityDefiner),
            EXTERN_ARG_SECURITY_INVOKER => Ok(ExternArgs::SecurityInvoker),
            EXTERN_ARG_PARALLEL_SAFE => Ok(ExternArgs::ParallelSafe),
            EXTERN_ARG_PARALLEL_UNSAFE => Ok(ExternArgs::ParallelUnsafe),
            EXTERN_ARG_PARALLEL_RESTRICTED => Ok(ExternArgs::ParallelRestricted),
            EXTERN_ARG_SHOULD_PANIC => Ok(ExternArgs::ShouldPanic(self.read_string()?)),
            EXTERN_ARG_SCHEMA => Ok(ExternArgs::Schema(self.read_string()?)),
            EXTERN_ARG_SUPPORT => Ok(ExternArgs::Support(self.read_positioning_ref()?)),
            EXTERN_ARG_NAME => Ok(ExternArgs::Name(self.read_string()?)),
            EXTERN_ARG_COST => Ok(ExternArgs::Cost(self.read_string()?)),
            EXTERN_ARG_REQUIRES => {
                let count = self.read_u32()? as usize;
                let mut items = Vec::with_capacity(count);
                for _ in 0..count {
                    items.push(self.read_positioning_ref()?);
                }
                Ok(ExternArgs::Requires(items))
            }
            other => Err(eyre!("invalid extern arg tag in schema entry: {other}")),
        }
    }

    pub fn read_search_path(&mut self) -> Result<Option<Vec<&'a str>>> {
        if !self.read_bool()? {
            return Ok(None);
        }
        let count = self.read_u32()? as usize;
        let mut values = Vec::with_capacity(count);
        for _ in 0..count {
            values.push(self.read_str()?);
        }
        Ok(Some(values))
    }

    pub fn read_operator(&mut self) -> Result<PgOperatorEntity<'a>> {
        Ok(PgOperatorEntity {
            opname: self.read_option_str()?,
            commutator: self.read_option_str()?,
            negator: self.read_option_str()?,
            restrict: self.read_option_str()?,
            join: self.read_option_str()?,
            hashes: self.read_bool()?,
            merges: self.read_bool()?,
        })
    }

    pub fn read_cast(&mut self) -> Result<PgCastEntity> {
        match self.read_u8()? {
            OPERATOR_CAST_DEFAULT => Ok(PgCastEntity::Default),
            OPERATOR_CAST_ASSIGNMENT => Ok(PgCastEntity::Assignment),
            OPERATOR_CAST_IMPLICIT => Ok(PgCastEntity::Implicit),
            other => Err(eyre!("invalid cast tag in schema entry: {other}")),
        }
    }

    pub fn read_sql_declared(&mut self) -> Result<SqlDeclaredEntity> {
        let kind = self.read_u8()?;
        let name = self.read_string()?;

        match kind {
            SQL_DECLARED_TYPE | SQL_DECLARED_ENUM => {
                let type_ident = self.read_string()?;
                let sql = match self.read_argument_sql_owned()? {
                    Ok(crate::metadata::SqlMapping::As(sql)) => sql,
                    Ok(other) => {
                        bail!("invalid SQL declaration mapping in schema entry: {other:?}")
                    }
                    Err(err) => return Err(err.into()),
                };
                let data = SqlDeclaredTypeEntityData { sql, name, type_ident };
                Ok(match kind {
                    SQL_DECLARED_TYPE => SqlDeclaredEntity::Type(data),
                    SQL_DECLARED_ENUM => SqlDeclaredEntity::Enum(data),
                    _ => unreachable!(),
                })
            }
            SQL_DECLARED_FUNCTION => {
                let sql = name
                    .split("::")
                    .last()
                    .ok_or_else(|| eyre!("function declaration was missing a name"))?
                    .to_owned();
                Ok(SqlDeclaredEntity::Function(SqlDeclaredFunctionEntityData { sql, name }))
            }
            other => Err(eyre!("invalid SQL declared tag in schema entry: {other}")),
        }
    }

    pub fn read_aggregate_type(&mut self) -> Result<AggregateTypeEntity<'a>> {
        Ok(AggregateTypeEntity { name: self.read_option_str()?, used_ty: self.read_used_type()? })
    }

    pub fn read_aggregate_type_list(&mut self) -> Result<Vec<AggregateTypeEntity<'a>>> {
        let count = self.read_u32()? as usize;
        let mut items = Vec::with_capacity(count);
        for _ in 0..count {
            items.push(self.read_aggregate_type()?);
        }
        Ok(items)
    }

    pub fn read_finalize_modify(&mut self) -> Result<FinalizeModify> {
        match self.read_u8()? {
            AGGREGATE_FINALIZE_READ_ONLY => Ok(FinalizeModify::ReadOnly),
            AGGREGATE_FINALIZE_SHAREABLE => Ok(FinalizeModify::Shareable),
            AGGREGATE_FINALIZE_READ_WRITE => Ok(FinalizeModify::ReadWrite),
            other => Err(eyre!("invalid finalize modify tag in schema entry: {other}")),
        }
    }

    pub fn read_parallel_option(&mut self) -> Result<ParallelOption> {
        match self.read_u8()? {
            AGGREGATE_PARALLEL_SAFE => Ok(ParallelOption::Safe),
            AGGREGATE_PARALLEL_RESTRICTED => Ok(ParallelOption::Restricted),
            AGGREGATE_PARALLEL_UNSAFE => Ok(ParallelOption::Unsafe),
            other => Err(eyre!("invalid parallel option tag in schema entry: {other}")),
        }
    }

    pub fn finish(&self) -> Result<()> {
        if self.is_empty() {
            Ok(())
        } else {
            Err(eyre!("schema entry had {} trailing bytes", self.remaining()))
        }
    }
}

pub fn entry_payloads(section: &[u8]) -> Result<Vec<&[u8]>> {
    let mut out = Vec::new();
    let mut reader = EntryReader::new(section);
    while !reader.is_empty() {
        if reader.remaining() < 4 {
            if section[reader.pos..].iter().all(|byte| *byte == 0) {
                break;
            }
            bail!("invalid trailing bytes in pgrx schema section");
        }

        let len = reader.read_u32()? as usize;
        if len == 0 {
            if section[reader.pos..].iter().all(|byte| *byte == 0) {
                break;
            }
            bail!("invalid zero-length pgrx schema entry");
        }
        if reader.remaining() < len {
            bail!("invalid pgrx schema section payload length");
        }
        let start = reader.pos;
        reader.pos += len;
        out.push(&section[start..reader.pos]);
    }
    Ok(out)
}

pub fn decode_entity<'a>(payload: &'a [u8]) -> Result<SqlGraphEntity<'a>> {
    let mut reader = EntryReader::new(payload);
    let entity = match reader.read_u8()? {
        ENTITY_SCHEMA => SqlGraphEntity::Schema(SchemaEntity {
            module_path: reader.read_str()?,
            name: reader.read_str()?,
            file: reader.read_str()?,
            line: reader.read_u32()?,
        }),
        ENTITY_CUSTOM_SQL => {
            let sql = reader.read_str()?;
            let module_path = reader.read_str()?;
            let full_path = reader.read_str()?;
            let file = reader.read_str()?;
            let line = reader.read_u32()?;
            let name = reader.read_str()?;
            let bootstrap = reader.read_bool()?;
            let finalize = reader.read_bool()?;

            let require_count = reader.read_u32()? as usize;
            let mut requires = Vec::with_capacity(require_count);
            for _ in 0..require_count {
                requires.push(reader.read_positioning_ref()?);
            }

            let create_count = reader.read_u32()? as usize;
            let mut creates = Vec::with_capacity(create_count);
            for _ in 0..create_count {
                creates.push(reader.read_sql_declared()?);
            }

            SqlGraphEntity::CustomSql(ExtensionSqlEntity {
                module_path,
                full_path,
                sql,
                file,
                line,
                name,
                bootstrap,
                finalize,
                requires,
                creates,
            })
        }
        ENTITY_FUNCTION => {
            let name = reader.read_str()?;
            let unaliased_name = reader.read_str()?;
            let module_path = reader.read_str()?;
            let full_path = reader.read_str()?;

            let arg_count = reader.read_u32()? as usize;
            let mut fn_args = Vec::with_capacity(arg_count);
            for _ in 0..arg_count {
                fn_args.push(reader.read_pg_extern_argument()?);
            }

            let fn_return = reader.read_pg_extern_return()?;
            let schema = reader.read_option_str()?;
            let file = reader.read_str()?;
            let line = reader.read_u32()?;

            let extern_attr_count = reader.read_u32()? as usize;
            let mut extern_attrs = Vec::with_capacity(extern_attr_count);
            for _ in 0..extern_attr_count {
                extern_attrs.push(reader.read_extern_arg()?);
            }

            let search_path = reader.read_search_path()?;
            let operator = if reader.read_bool()? { Some(reader.read_operator()?) } else { None };
            let cast = if reader.read_bool()? { Some(reader.read_cast()?) } else { None };
            let to_sql_config = reader.read_to_sql_config()?;

            SqlGraphEntity::Function(PgExternEntity {
                name,
                unaliased_name,
                module_path,
                full_path,
                fn_args,
                fn_return,
                schema,
                file,
                line,
                extern_attrs,
                search_path,
                operator,
                cast,
                to_sql_config,
            })
        }
        ENTITY_TYPE => {
            let name = reader.read_str()?;
            let file = reader.read_str()?;
            let line = reader.read_u32()?;
            let module_path = reader.read_str()?;
            let full_path = reader.read_str()?;
            let type_ident = reader.read_str()?;
            let in_fn_path = reader.read_str()?;
            let out_fn_path = reader.read_str()?;
            let receive_fn_path = reader.read_option_str()?;
            let send_fn_path = reader.read_option_str()?;
            let to_sql_config = reader.read_to_sql_config()?;
            let alignment =
                if reader.read_bool()? { Some(reader.read_u32()? as usize) } else { None };

            SqlGraphEntity::Type(PostgresTypeEntity {
                name,
                file,
                line,
                full_path,
                module_path,
                type_ident,
                in_fn_path,
                out_fn_path,
                receive_fn_path,
                send_fn_path,
                to_sql_config,
                alignment,
            })
        }
        ENTITY_ENUM => {
            let name = reader.read_str()?;
            let file = reader.read_str()?;
            let line = reader.read_u32()?;
            let module_path = reader.read_str()?;
            let full_path = reader.read_str()?;
            let type_ident = reader.read_str()?;

            let variant_count = reader.read_u32()? as usize;
            let mut variants = Vec::with_capacity(variant_count);
            for _ in 0..variant_count {
                variants.push(reader.read_str()?);
            }

            let to_sql_config = reader.read_to_sql_config()?;

            SqlGraphEntity::Enum(PostgresEnumEntity {
                name,
                file,
                line,
                full_path,
                module_path,
                type_ident,
                variants,
                to_sql_config,
            })
        }
        ENTITY_ORD => SqlGraphEntity::Ord(PostgresOrdEntity {
            name: reader.read_str()?,
            file: reader.read_str()?,
            line: reader.read_u32()?,
            full_path: reader.read_str()?,
            module_path: reader.read_str()?,
            type_ident: reader.read_str()?,
            to_sql_config: reader.read_to_sql_config()?,
        }),
        ENTITY_HASH => SqlGraphEntity::Hash(PostgresHashEntity {
            name: reader.read_str()?,
            file: reader.read_str()?,
            line: reader.read_u32()?,
            full_path: reader.read_str()?,
            module_path: reader.read_str()?,
            type_ident: reader.read_str()?,
            to_sql_config: reader.read_to_sql_config()?,
        }),
        ENTITY_AGGREGATE => {
            let full_path = reader.read_str()?;
            let module_path = reader.read_str()?;
            let file = reader.read_str()?;
            let line = reader.read_u32()?;
            let name = reader.read_str()?;
            let ordered_set = reader.read_bool()?;
            let args = reader.read_aggregate_type_list()?;
            let direct_args =
                if reader.read_bool()? { Some(reader.read_aggregate_type_list()?) } else { None };
            let stype = reader.read_aggregate_type()?;
            let sfunc = reader.read_str()?;
            let finalfunc = reader.read_option_str()?;
            let finalfunc_modify =
                if reader.read_bool()? { Some(reader.read_finalize_modify()?) } else { None };
            let combinefunc = reader.read_option_str()?;
            let serialfunc = reader.read_option_str()?;
            let deserialfunc = reader.read_option_str()?;
            let initcond = reader.read_option_str()?;
            let msfunc = reader.read_option_str()?;
            let minvfunc = reader.read_option_str()?;
            let mstype = if reader.read_bool()? { Some(reader.read_used_type()?) } else { None };
            let mfinalfunc = reader.read_option_str()?;
            let mfinalfunc_modify =
                if reader.read_bool()? { Some(reader.read_finalize_modify()?) } else { None };
            let minitcond = reader.read_option_str()?;
            let sortop = reader.read_option_str()?;
            let parallel =
                if reader.read_bool()? { Some(reader.read_parallel_option()?) } else { None };
            let hypothetical = reader.read_bool()?;
            let to_sql_config = reader.read_to_sql_config()?;

            SqlGraphEntity::Aggregate(PgAggregateEntity {
                full_path,
                module_path,
                file,
                line,
                name,
                ordered_set,
                args,
                direct_args,
                stype,
                sfunc,
                finalfunc,
                finalfunc_modify,
                combinefunc,
                serialfunc,
                deserialfunc,
                initcond,
                msfunc,
                minvfunc,
                mstype,
                mfinalfunc,
                mfinalfunc_modify,
                minitcond,
                sortop,
                parallel,
                hypothetical,
                to_sql_config,
            })
        }
        ENTITY_TRIGGER => SqlGraphEntity::Trigger(PgTriggerEntity {
            function_name: reader.read_str()?,
            file: reader.read_str()?,
            line: reader.read_u32()?,
            module_path: reader.read_str()?,
            full_path: reader.read_str()?,
            to_sql_config: reader.read_to_sql_config()?,
        }),
        other => return Err(eyre!("invalid entity tag in schema entry: {other}")),
    };

    reader.finish()?;
    Ok(entity)
}

pub fn decode_entities<'a>(section: &'a [u8]) -> Result<Vec<SqlGraphEntity<'a>>> {
    entry_payloads(section)?
        .into_iter()
        .filter(|payload| *payload != SECTION_SENTINEL_PAYLOAD.as_slice())
        .map(decode_entity)
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip_basic_entry() {
        const PAYLOAD_LEN: usize = u8_len() + str_len("hello") + bool_len() + u32_len();
        const TOTAL_LEN: usize = u32_len() + PAYLOAD_LEN;
        const ENTRY: [u8; TOTAL_LEN] = EntryWriter::<TOTAL_LEN>::new()
            .u32(PAYLOAD_LEN as u32)
            .u8(7)
            .str("hello")
            .bool(true)
            .u32(42)
            .finish();

        let payloads = entry_payloads(&ENTRY).unwrap();
        assert_eq!(payloads.len(), 1);
        let mut reader = EntryReader::new(payloads[0]);
        assert_eq!(reader.read_u8().unwrap(), 7);
        assert_eq!(reader.read_string().unwrap(), "hello");
        assert!(reader.read_bool().unwrap());
        assert_eq!(reader.read_u32().unwrap(), 42);
        assert!(reader.is_empty());
    }

    #[test]
    fn ignores_trailing_zero_padding() {
        const PAYLOAD_LEN: usize = u8_len();
        const TOTAL_LEN: usize = u32_len() + PAYLOAD_LEN;
        const ENTRY: [u8; TOTAL_LEN] =
            EntryWriter::<TOTAL_LEN>::new().u32(PAYLOAD_LEN as u32).u8(ENTITY_SCHEMA).finish();
        let mut section = ENTRY.to_vec();
        section.extend_from_slice(&[0, 0, 0, 0]);

        let payloads = entry_payloads(&section).unwrap();
        assert_eq!(payloads.len(), 1);
        assert_eq!(payloads[0], &[ENTITY_SCHEMA]);
    }

    #[test]
    fn zero_filled_section_decodes_as_empty() {
        assert!(decode_entities(&[0]).unwrap().is_empty());
    }

    #[test]
    fn sentinel_entry_decodes_as_empty() {
        assert!(decode_entities(&schema_section_sentinel_entry()).unwrap().is_empty());
    }

    #[test]
    fn sentinel_entry_is_ignored_mid_section() {
        const PAYLOAD_LEN: usize =
            u8_len() + str_len("module") + str_len("tests") + str_len("file.rs") + u32_len();
        const TOTAL_LEN: usize = u32_len() + PAYLOAD_LEN;
        const ENTRY: [u8; TOTAL_LEN] = EntryWriter::<TOTAL_LEN>::new()
            .u32(PAYLOAD_LEN as u32)
            .u8(ENTITY_SCHEMA)
            .str("module")
            .str("tests")
            .str("file.rs")
            .u32(42)
            .finish();

        let mut section = Vec::new();
        section.extend_from_slice(&ENTRY);
        section.extend_from_slice(&schema_section_sentinel_entry());
        section.extend_from_slice(&ENTRY);
        let entities = decode_entities(&section).unwrap();
        assert_eq!(entities.len(), 2);
        assert!(entities.iter().all(|entity| matches!(entity, SqlGraphEntity::Schema(_))));
    }

    #[test]
    fn recognizes_macho_qualified_section_name() {
        assert!(is_schema_section_name(MACHO_SECTION_NAME));
        assert!(is_schema_section_name(MACHO_SECTION_PATH));
        assert!(is_schema_section_name(ELF_SECTION_NAME));
        assert!(is_schema_section_name(".pgrx_schema"));
        assert!(is_schema_section_name("__pgrx_schema"));
        assert!(is_schema_section_name("__DATA,__pgrx_schema"));
        assert!(!is_schema_section_name("__TEXT,__text"));
    }

    #[test]
    fn schema_section_names_fit_windows_image_limits() {
        assert!(ELF_SECTION_NAME.len() <= 8);
        assert!(MACHO_SECTION_NAME.len() <= 8);
    }

    #[test]
    fn round_trip_function_metadata_type_preserves_type_origin() {
        const TYPE_IDENT: &str = "tests::FancyText";
        const SQL: &str = "TEXT";
        const PAYLOAD_LEN: usize = function_metadata_type_len(
            Some(TYPE_IDENT),
            Ok(SqlMappingRef::literal(SQL)),
            Ok(ReturnsRef::One(SqlMappingRef::literal(SQL))),
        );
        const TOTAL_LEN: usize = u32_len() + PAYLOAD_LEN;
        const ENTRY: [u8; TOTAL_LEN] = EntryWriter::<TOTAL_LEN>::new()
            .u32(PAYLOAD_LEN as u32)
            .function_metadata_type(
                Some((TYPE_IDENT, TypeOrigin::External)),
                Ok(SqlMappingRef::literal(SQL)),
                Ok(ReturnsRef::One(SqlMappingRef::literal(SQL))),
            )
            .finish();

        let payloads = entry_payloads(&ENTRY).unwrap();
        let mut reader = EntryReader::new(payloads[0]);
        let metadata = reader.read_function_metadata_type().unwrap();

        assert_eq!(metadata.type_ident(), Some(TYPE_IDENT));
        assert_eq!(metadata.type_origin(), Some(TypeOrigin::External));
        assert_eq!(metadata.argument_sql, Ok(SqlMapping::literal(SQL)));
        assert_eq!(metadata.return_sql, Ok(Returns::One(SqlMapping::literal(SQL))));
        assert!(reader.is_empty());
    }

    #[test]
    fn round_trip_function_metadata_type_preserves_array_mappings() {
        const TYPE_IDENT: &str = "tests::FancyNumeric";
        const PAYLOAD_LEN: usize = function_metadata_type_len(
            Some(TYPE_IDENT),
            Ok(SqlMappingRef::Array(SqlArrayMappingRef::As("INT"))),
            Ok(ReturnsRef::One(SqlMappingRef::Array(SqlArrayMappingRef::Numeric {
                precision: Some(10),
                scale: Some(2),
            }))),
        );
        const TOTAL_LEN: usize = u32_len() + PAYLOAD_LEN;
        const ENTRY: [u8; TOTAL_LEN] = EntryWriter::<TOTAL_LEN>::new()
            .u32(PAYLOAD_LEN as u32)
            .function_metadata_type(
                Some((TYPE_IDENT, TypeOrigin::External)),
                Ok(SqlMappingRef::Array(SqlArrayMappingRef::As("INT"))),
                Ok(ReturnsRef::One(SqlMappingRef::Array(SqlArrayMappingRef::Numeric {
                    precision: Some(10),
                    scale: Some(2),
                }))),
            )
            .finish();

        let payloads = entry_payloads(&ENTRY).unwrap();
        let mut reader = EntryReader::new(payloads[0]);
        let metadata = reader.read_function_metadata_type().unwrap();

        assert_eq!(
            metadata.argument_sql,
            Ok(SqlMapping::Array(SqlArrayMapping::As("INT".to_string())))
        );
        assert_eq!(
            metadata.return_sql,
            Ok(Returns::One(SqlMapping::Array(SqlArrayMapping::As("NUMERIC(10, 2)".to_string(),))))
        );
        assert!(reader.is_empty());
    }

    #[test]
    fn round_trip_function_metadata_type_preserves_composite_array_mappings() {
        const PAYLOAD_LEN: usize = function_metadata_type_len(
            None,
            Ok(SqlMappingRef::Array(SqlArrayMappingRef::Composite)),
            Ok(ReturnsRef::One(SqlMappingRef::Array(SqlArrayMappingRef::Composite))),
        );
        const TOTAL_LEN: usize = u32_len() + PAYLOAD_LEN;
        const ENTRY: [u8; TOTAL_LEN] = EntryWriter::<TOTAL_LEN>::new()
            .u32(PAYLOAD_LEN as u32)
            .function_metadata_type(
                None,
                Ok(SqlMappingRef::Array(SqlArrayMappingRef::Composite)),
                Ok(ReturnsRef::One(SqlMappingRef::Array(SqlArrayMappingRef::Composite))),
            )
            .finish();

        let payloads = entry_payloads(&ENTRY).unwrap();
        let mut reader = EntryReader::new(payloads[0]);
        let metadata = reader.read_function_metadata_type().unwrap();

        assert_eq!(metadata.argument_sql, Ok(SqlMapping::Array(SqlArrayMapping::Composite)));
        assert_eq!(
            metadata.return_sql,
            Ok(Returns::One(SqlMapping::Array(SqlArrayMapping::Composite)))
        );
        assert_eq!(metadata.type_ident(), None);
        assert_eq!(metadata.type_origin(), None);
        assert!(reader.is_empty());
    }

    #[test]
    fn round_trip_function_metadata_type_preserves_nested_array_errors() {
        const TYPE_IDENT: &str = "tests::NestedArrayError";
        const PAYLOAD_LEN: usize = function_metadata_type_len(
            Some(TYPE_IDENT),
            Err(ArgumentError::NestedArray),
            Err(ReturnsError::NestedArray),
        );
        const TOTAL_LEN: usize = u32_len() + PAYLOAD_LEN;
        const ENTRY: [u8; TOTAL_LEN] = EntryWriter::<TOTAL_LEN>::new()
            .u32(PAYLOAD_LEN as u32)
            .function_metadata_type(
                Some((TYPE_IDENT, TypeOrigin::External)),
                Err(ArgumentError::NestedArray),
                Err(ReturnsError::NestedArray),
            )
            .finish();

        let payloads = entry_payloads(&ENTRY).unwrap();
        let mut reader = EntryReader::new(payloads[0]);
        let metadata = reader.read_function_metadata_type().unwrap();

        assert_eq!(metadata.argument_sql, Err(ArgumentError::NestedArray));
        assert_eq!(metadata.return_sql, Err(ReturnsError::NestedArray));
        assert!(reader.is_empty());
    }

    #[test]
    fn round_trip_sql_declared_type_preserves_type_ident_and_sql() {
        const NAME: &str = "tests::FancyText";
        const TYPE_IDENT: &str = "tests::FancyText";
        const SQL: &str = "fancy_text";
        const PAYLOAD_LEN: usize = u8_len()
            + str_len(NAME)
            + str_len(TYPE_IDENT)
            + argument_sql_len(Ok(SqlMappingRef::literal(SQL)));
        const TOTAL_LEN: usize = u32_len() + PAYLOAD_LEN;
        const ENTRY: [u8; TOTAL_LEN] = EntryWriter::<TOTAL_LEN>::new()
            .u32(PAYLOAD_LEN as u32)
            .u8(SQL_DECLARED_TYPE)
            .str(NAME)
            .str(TYPE_IDENT)
            .argument_sql(Ok(SqlMappingRef::literal(SQL)))
            .finish();

        let payloads = entry_payloads(&ENTRY).unwrap();
        let mut reader = EntryReader::new(payloads[0]);
        let declared = reader.read_sql_declared().unwrap();

        assert_eq!(declared.type_ident(), Some(TYPE_IDENT));
        assert_eq!(declared.sql(), SQL);
        assert!(reader.is_empty());
    }

    #[test]
    fn round_trip_sql_declared_function_skips_type_ident() {
        const NAME: &str = "tests::helper_fn";
        const PAYLOAD_LEN: usize = u8_len() + str_len(NAME);
        const TOTAL_LEN: usize = u32_len() + PAYLOAD_LEN;
        const ENTRY: [u8; TOTAL_LEN] = EntryWriter::<TOTAL_LEN>::new()
            .u32(PAYLOAD_LEN as u32)
            .u8(SQL_DECLARED_FUNCTION)
            .str(NAME)
            .finish();

        let payloads = entry_payloads(&ENTRY).unwrap();
        let mut reader = EntryReader::new(payloads[0]);
        let declared = reader.read_sql_declared().unwrap();

        assert_eq!(declared.type_ident(), None);
        assert_eq!(declared.sql(), "helper_fn");
        assert!(reader.is_empty());
    }
}
