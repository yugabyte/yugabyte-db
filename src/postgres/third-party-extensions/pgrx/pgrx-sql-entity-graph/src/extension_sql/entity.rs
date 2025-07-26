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

`pgrx::extension_sql!()` related entities for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.


*/
use crate::extension_sql::SqlDeclared;
use crate::pgrx_sql::PgrxSql;
use crate::positioning_ref::PositioningRef;
use crate::to_sql::ToSql;
use crate::{SqlGraphEntity, SqlGraphIdentifier};

use std::fmt::Display;

/// The output of a [`ExtensionSql`](crate::ExtensionSql) from `quote::ToTokens::to_tokens`.
#[derive(Debug, Clone, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub struct ExtensionSqlEntity {
    pub module_path: &'static str,
    pub full_path: &'static str,
    pub sql: &'static str,
    pub file: &'static str,
    pub line: u32,
    pub name: &'static str,
    pub bootstrap: bool,
    pub finalize: bool,
    pub requires: Vec<PositioningRef>,
    pub creates: Vec<SqlDeclaredEntity>,
}

impl ExtensionSqlEntity {
    pub fn has_sql_declared_entity(&self, identifier: &SqlDeclared) -> Option<&SqlDeclaredEntity> {
        self.creates.iter().find(|created| created.has_sql_declared_entity(identifier))
    }
}

impl From<ExtensionSqlEntity> for SqlGraphEntity {
    fn from(val: ExtensionSqlEntity) -> Self {
        SqlGraphEntity::CustomSql(val)
    }
}

impl SqlGraphIdentifier for ExtensionSqlEntity {
    fn dot_identifier(&self) -> String {
        format!("sql {}", self.name)
    }
    fn rust_identifier(&self) -> String {
        self.name.to_string()
    }

    fn file(&self) -> Option<&'static str> {
        Some(self.file)
    }

    fn line(&self) -> Option<u32> {
        Some(self.line)
    }
}

impl ToSql for ExtensionSqlEntity {
    fn to_sql(&self, _context: &PgrxSql) -> eyre::Result<String> {
        let ExtensionSqlEntity { file, line, sql, creates, requires, .. } = self;
        let creates = if !creates.is_empty() {
            let joined = creates.iter().map(|i| format!("--   {i}")).collect::<Vec<_>>().join("\n");
            format!(
                "\
                -- creates:\n\
                {joined}\n\n"
            )
        } else {
            "".to_string()
        };
        let requires = if !requires.is_empty() {
            let joined =
                requires.iter().map(|i| format!("--   {i}")).collect::<Vec<_>>().join("\n");
            format!(
                "\
               -- requires:\n\
                {joined}\n\n"
            )
        } else {
            "".to_string()
        };
        let sql = format!(
            "\n\
                -- {file}:{line}\n\
                {bootstrap}\
                {creates}\
                {requires}\
                {finalize}\
                {sql}\
                ",
            bootstrap = if self.bootstrap { "-- bootstrap\n" } else { "" },
            finalize = if self.finalize { "-- finalize\n" } else { "" },
        );
        Ok(sql)
    }
}

#[derive(Debug, Clone, Hash, PartialEq, Eq, Ord, PartialOrd)]
pub struct SqlDeclaredEntityData {
    sql: String,
    name: String,
    option: String,
    vec: String,
    vec_option: String,
    option_vec: String,
    option_vec_option: String,
    array: String,
    option_array: String,
    varlena: String,
    pg_box: Vec<String>,
}
#[derive(Debug, Clone, Hash, PartialEq, Eq, Ord, PartialOrd)]
pub enum SqlDeclaredEntity {
    Type(SqlDeclaredEntityData),
    Enum(SqlDeclaredEntityData),
    Function(SqlDeclaredEntityData),
}

impl Display for SqlDeclaredEntity {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            SqlDeclaredEntity::Type(data) => {
                write!(f, "Type({})", data.name)
            }
            SqlDeclaredEntity::Enum(data) => {
                write!(f, "Enum({})", data.name)
            }
            SqlDeclaredEntity::Function(data) => {
                write!(f, "Function({})", data.name)
            }
        }
    }
}

impl SqlDeclaredEntity {
    pub fn build(variant: impl AsRef<str>, name: impl AsRef<str>) -> eyre::Result<Self> {
        let name = name.as_ref();
        let data = SqlDeclaredEntityData {
            sql: name
                .split("::")
                .last()
                .ok_or_else(|| eyre::eyre!("Did not get SQL for `{}`", name))?
                .to_string(),
            name: name.to_string(),
            option: format!("Option<{name}>"),
            vec: format!("Vec<{name}>"),
            vec_option: format!("Vec<Option<{name}>>"),
            option_vec: format!("Option<Vec<{name}>>"),
            option_vec_option: format!("Option<Vec<Option<{name}>>"),
            array: format!("Array<{name}>"),
            option_array: format!("Option<{name}>"),
            varlena: format!("Varlena<{name}>"),
            pg_box: vec![
                format!("pgrx::pgbox::PgBox<{}>", name),
                format!("pgrx::pgbox::PgBox<{}, pgrx::pgbox::AllocatedByRust>", name),
                format!("pgrx::pgbox::PgBox<{}, pgrx::pgbox::AllocatedByPostgres>", name),
            ],
        };
        let retval = match variant.as_ref() {
            "Type" => Self::Type(data),
            "Enum" => Self::Enum(data),
            "Function" => Self::Function(data),
            _ => {
                return Err(eyre::eyre!(
                    "Can only declare `Type(Ident)`, `Enum(Ident)` or `Function(Ident)`"
                ))
            }
        };
        Ok(retval)
    }
    pub fn sql(&self) -> String {
        match self {
            SqlDeclaredEntity::Type(data) => data.sql.clone(),
            SqlDeclaredEntity::Enum(data) => data.sql.clone(),
            SqlDeclaredEntity::Function(data) => data.sql.clone(),
        }
    }

    pub fn has_sql_declared_entity(&self, identifier: &SqlDeclared) -> bool {
        match (&identifier, &self) {
            (SqlDeclared::Type(ident_name), &SqlDeclaredEntity::Type(data))
            | (SqlDeclared::Enum(ident_name), &SqlDeclaredEntity::Enum(data))
            | (SqlDeclared::Function(ident_name), &SqlDeclaredEntity::Function(data)) => {
                let matches = |identifier_name: &str| {
                    identifier_name == data.name
                        || identifier_name == data.option
                        || identifier_name == data.vec
                        || identifier_name == data.vec_option
                        || identifier_name == data.option_vec
                        || identifier_name == data.option_vec_option
                        || identifier_name == data.array
                        || identifier_name == data.option_array
                        || identifier_name == data.varlena
                };
                if matches(ident_name) || data.pg_box.contains(ident_name) {
                    return true;
                }
                // there are cases where the identifier is
                // `core::option::Option<Foo>` while the data stores
                // `Option<Foo>` check again for this
                let Some(generics_start) = ident_name.find('<') else { return false };
                let Some(qual_end) = ident_name[..generics_start].rfind("::") else { return false };
                matches(&ident_name[qual_end + 2..])
            }
            _ => false,
        }
    }
}
