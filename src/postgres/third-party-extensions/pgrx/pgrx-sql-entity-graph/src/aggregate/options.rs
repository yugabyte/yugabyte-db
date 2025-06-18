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

`#[pg_aggregate]` related options for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
use crate::{PgrxSql, ToSql};

/// Corresponds to the `PARALLEL` and `MFINALFUNC_MODIFY` in [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html).
#[derive(Debug, Clone, Copy, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub enum ParallelOption {
    Safe,
    Restricted,
    Unsafe,
}

impl ToSql for ParallelOption {
    fn to_sql(&self, _context: &PgrxSql) -> eyre::Result<String> {
        let value = match self {
            ParallelOption::Safe => String::from("SAFE"),
            ParallelOption::Restricted => String::from("RESTRICTED"),
            ParallelOption::Unsafe => String::from("UNSAFE"),
        };
        Ok(value)
    }
}

/// Corresponds to the `FINALFUNC_MODIFY` and `MFINALFUNC_MODIFY` in [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html).
#[derive(Debug, Clone, Copy, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub enum FinalizeModify {
    ReadOnly,
    Shareable,
    ReadWrite,
}

impl ToSql for FinalizeModify {
    fn to_sql(&self, _context: &PgrxSql) -> eyre::Result<String> {
        let value = match self {
            FinalizeModify::ReadOnly => String::from("READ_ONLY"),
            FinalizeModify::Shareable => String::from("SHAREABLE"),
            FinalizeModify::ReadWrite => String::from("READ_WRITE"),
        };
        Ok(value)
    }
}
