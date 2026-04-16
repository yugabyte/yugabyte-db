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

`#[derive(PostgresEnum)]` related entities for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
use crate::mapping::RustSqlMapping;
use crate::pgrx_sql::PgrxSql;
use crate::to_sql::entity::ToSqlConfigEntity;
use crate::to_sql::ToSql;
use crate::{SqlGraphEntity, SqlGraphIdentifier, TypeMatch};
use std::collections::BTreeSet;

/// The output of a [`PostgresEnum`](crate::postgres_enum::PostgresEnum) from `quote::ToTokens::to_tokens`.
#[derive(Debug, Clone, PartialEq, Eq, Ord, PartialOrd, Hash)]
pub struct PostgresEnumEntity {
    pub name: &'static str,
    pub file: &'static str,
    pub line: u32,
    pub full_path: &'static str,
    pub module_path: &'static str,
    pub mappings: BTreeSet<RustSqlMapping>,
    pub variants: Vec<&'static str>,
    pub to_sql_config: ToSqlConfigEntity,
}

impl TypeMatch for PostgresEnumEntity {
    fn id_matches(&self, candidate: &core::any::TypeId) -> bool {
        self.mappings.iter().any(|tester| *candidate == tester.id)
    }
}

impl From<PostgresEnumEntity> for SqlGraphEntity {
    fn from(val: PostgresEnumEntity) -> Self {
        SqlGraphEntity::Enum(val)
    }
}

impl SqlGraphIdentifier for PostgresEnumEntity {
    fn dot_identifier(&self) -> String {
        format!("enum {}", self.full_path)
    }
    fn rust_identifier(&self) -> String {
        self.full_path.to_string()
    }

    fn file(&self) -> Option<&'static str> {
        Some(self.file)
    }

    fn line(&self) -> Option<u32> {
        Some(self.line)
    }
}

impl ToSql for PostgresEnumEntity {
    fn to_sql(&self, context: &PgrxSql) -> eyre::Result<String> {
        let self_index = context.enums[self];
        let sql = format!(
            "\n\
                -- {file}:{line}\n\
                -- {full_path}\n\
                CREATE TYPE {schema}{name} AS ENUM (\n\
                    {variants}\
                );\
            ",
            schema = context.schema_prefix_for(&self_index),
            full_path = self.full_path,
            file = self.file,
            line = self.line,
            name = self.name,
            variants = self
                .variants
                .iter()
                .map(|variant| format!("\t'{variant}'"))
                .collect::<Vec<_>>()
                .join(",\n")
                + "\n",
        );
        Ok(sql)
    }
}
