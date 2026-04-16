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

`#[pg_schema]` related entities for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
use crate::pgrx_sql::PgrxSql;
use crate::to_sql::ToSql;
use crate::{SqlGraphEntity, SqlGraphIdentifier};

/// The output of a [`Schema`](crate::schema::Schema) from `quote::ToTokens::to_tokens`.
#[derive(Debug, Clone, Hash, PartialEq, Eq, Ord, PartialOrd)]
pub struct SchemaEntity {
    pub module_path: &'static str,
    pub name: &'static str,
    pub file: &'static str,
    pub line: u32,
}

impl From<SchemaEntity> for SqlGraphEntity {
    fn from(val: SchemaEntity) -> Self {
        SqlGraphEntity::Schema(val)
    }
}

impl SqlGraphIdentifier for SchemaEntity {
    fn dot_identifier(&self) -> String {
        format!("schema {}", self.module_path)
    }
    fn rust_identifier(&self) -> String {
        self.module_path.to_string()
    }

    fn file(&self) -> Option<&'static str> {
        Some(self.file)
    }

    fn line(&self) -> Option<u32> {
        Some(self.line)
    }
}

impl ToSql for SchemaEntity {
    fn to_sql(&self, _context: &PgrxSql) -> eyre::Result<String> {
        let SchemaEntity { name, file, line, module_path } = self;
        let sql = format!(
            "\n\
                -- {file}:{line}\n\
                CREATE SCHEMA IF NOT EXISTS {name}; /* {module_path} */\
            ",
        );
        Ok(sql)
    }
}
