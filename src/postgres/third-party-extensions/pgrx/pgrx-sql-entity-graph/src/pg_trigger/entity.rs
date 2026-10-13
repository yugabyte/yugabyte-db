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

`#[pg_trigger]` related entities for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
use crate::{PgrxSql, SqlGraphEntity, SqlGraphIdentifier, ToSql, ToSqlConfigEntity};

#[derive(Debug, Clone, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub struct PgTriggerEntity<'a> {
    pub function_name: &'a str,
    pub to_sql_config: ToSqlConfigEntity<'a>,
    pub file: &'a str,
    pub line: u32,
    pub module_path: &'a str,
    pub full_path: &'a str,
}

impl PgTriggerEntity<'_> {
    fn wrapper_function_name(&self) -> String {
        self.function_name.to_string() + "_wrapper"
    }
}

impl<'a> From<PgTriggerEntity<'a>> for SqlGraphEntity<'a> {
    fn from(val: PgTriggerEntity<'a>) -> Self {
        SqlGraphEntity::Trigger(val)
    }
}

impl ToSql for PgTriggerEntity<'_> {
    fn to_sql(&self, context: &PgrxSql) -> eyre::Result<String> {
        let self_index = context.triggers[self];
        let schema = context.schema_prefix_for(&self_index);

        let PgTriggerEntity { file, line, full_path, function_name, .. } = self;
        let sql = format!(
            "\n\
            -- {file}:{line}\n\
            -- {full_path}\n\
            CREATE FUNCTION {schema}\"{function_name}\"()\n\
                \tRETURNS TRIGGER\n\
                \tLANGUAGE c\n\
                \tAS 'MODULE_PATHNAME', '{wrapper_function_name}';",
            wrapper_function_name = self.wrapper_function_name(),
        );
        Ok(sql)
    }
}

impl SqlGraphIdentifier for PgTriggerEntity<'_> {
    fn dot_identifier(&self) -> String {
        format!("trigger fn {}", self.full_path)
    }
    fn rust_identifier(&self) -> String {
        self.full_path.to_string()
    }

    fn file(&self) -> Option<&str> {
        Some(self.file)
    }

    fn line(&self) -> Option<u32> {
        Some(self.line)
    }
}
