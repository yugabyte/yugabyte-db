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

Return value specific metadata for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
use thiserror::Error;

use super::sql_translatable::SqlMapping;

/// Describes the RETURNS of CREATE FUNCTION ... RETURNS ...
/// See the PostgreSQL documentation for [CREATE FUNCTION]
/// [CREATE FUNCTION]: <https://www.postgresql.org/docs/current/sql-createfunction.html>
#[derive(Clone, Debug, Hash, Eq, PartialEq, Ord, PartialOrd)]
pub enum Returns {
    One(SqlMapping),
    SetOf(SqlMapping),
    Table(Vec<SqlMapping>),
}

#[derive(Clone, Copy, Debug, Hash, Ord, PartialOrd, PartialEq, Eq, Error)]
pub enum ReturnsError {
    #[error("Nested SetOfIterator in return type")]
    NestedSetOf,
    #[error("Nested TableIterator in return type")]
    NestedTable,
    #[error("SetOfIterator containing TableIterator in return type")]
    SetOfContainingTable,
    #[error("TableIterator containing SetOfIterator in return type")]
    TableContainingSetOf,
    #[error("SetofIterator inside Array is not valid")]
    SetOfInArray,
    #[error("TableIterator inside Array is not valid")]
    TableInArray,
    #[error("Cannot use bare u8")]
    BareU8,
    #[error("SqlMapping::Skip inside Array is not valid")]
    SkipInArray,
    #[error("A Datum as a return means that `sql = \"...\"` must be set in the declaration")]
    Datum,
}
