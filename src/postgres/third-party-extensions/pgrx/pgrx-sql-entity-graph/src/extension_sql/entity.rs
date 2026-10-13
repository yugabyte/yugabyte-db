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
use crate::metadata::{SqlMapping, SqlTranslatable, TypeOrigin};
use crate::pgrx_sql::PgrxSql;
use crate::positioning_ref::PositioningRef;
use crate::to_sql::ToSql;
use crate::{SqlGraphEntity, SqlGraphIdentifier};

use std::fmt::Display;

/// The output of a [`ExtensionSql`](crate::ExtensionSql) from `quote::ToTokens::to_tokens`.
#[derive(Debug, Clone, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub struct ExtensionSqlEntity<'a> {
    pub module_path: &'a str,
    pub full_path: &'a str,
    pub sql: &'a str,
    pub file: &'a str,
    pub line: u32,
    pub name: &'a str,
    pub bootstrap: bool,
    pub finalize: bool,
    pub requires: Vec<PositioningRef>,
    pub creates: Vec<SqlDeclaredEntity>,
}

impl ExtensionSqlEntity<'_> {
    pub fn has_sql_declared_entity(&self, identifier: &SqlDeclared) -> Option<&SqlDeclaredEntity> {
        self.creates.iter().find(|created| created.has_sql_declared_entity(identifier))
    }
}

impl<'a> From<ExtensionSqlEntity<'a>> for SqlGraphEntity<'a> {
    fn from(val: ExtensionSqlEntity<'a>) -> Self {
        SqlGraphEntity::CustomSql(val)
    }
}

impl SqlGraphIdentifier for ExtensionSqlEntity<'_> {
    fn dot_identifier(&self) -> String {
        format!("sql {}", self.name)
    }
    fn rust_identifier(&self) -> String {
        self.name.to_string()
    }

    fn file(&self) -> Option<&str> {
        Some(self.file)
    }

    fn line(&self) -> Option<u32> {
        Some(self.line)
    }
}

impl ToSql for ExtensionSqlEntity<'_> {
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
pub struct SqlDeclaredTypeEntityData {
    pub(crate) sql: String,
    pub(crate) name: String,
    pub(crate) type_ident: String,
}

#[derive(Debug, Clone, Hash, PartialEq, Eq, Ord, PartialOrd)]
pub struct SqlDeclaredFunctionEntityData {
    pub(crate) sql: String,
    pub(crate) name: String,
}

#[derive(Debug, Clone, Hash, PartialEq, Eq, Ord, PartialOrd)]
pub enum SqlDeclaredEntity {
    Type(SqlDeclaredTypeEntityData),
    Enum(SqlDeclaredTypeEntityData),
    Function(SqlDeclaredFunctionEntityData),
}

impl Display for SqlDeclaredEntity {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Type(data) => {
                write!(f, "Type({})", data.name)
            }
            Self::Enum(data) => {
                write!(f, "Enum({})", data.name)
            }
            Self::Function(data) => {
                write!(f, "Function({})", data.name)
            }
        }
    }
}

impl SqlDeclaredEntity {
    pub fn build(variant: &str, name: &str) -> eyre::Result<Self> {
        let sql = name
            .split("::")
            .last()
            .ok_or_else(|| eyre::eyre!("Did not get SQL for `{}`", name))?
            .to_string();
        let retval = match variant {
            "Type" => Self::Type(SqlDeclaredTypeEntityData {
                sql,
                name: name.to_string(),
                type_ident: name.to_string(),
            }),
            "Enum" => Self::Enum(SqlDeclaredTypeEntityData {
                sql,
                name: name.to_string(),
                type_ident: name.to_string(),
            }),
            "Function" => {
                Self::Function(SqlDeclaredFunctionEntityData { sql, name: name.to_string() })
            }
            _ => {
                return Err(eyre::eyre!(
                    "Can only declare `Type(Ident)`, `Enum(Ident)` or `Function(Ident)`"
                ));
            }
        };
        Ok(retval)
    }

    pub fn build_type<T: SqlTranslatable>(variant: &str, name: &str) -> eyre::Result<Self> {
        let make_declared = match variant {
            "Type" => Self::Type,
            "Enum" => Self::Enum,
            _ => {
                return Err(eyre::eyre!(
                    "Can only declare `Type(Ident)` or `Enum(Ident)` with type metadata"
                ));
            }
        };

        if matches!(T::TYPE_ORIGIN, TypeOrigin::External) {
            return Err(eyre::eyre!(
                "`creates = [{variant}(...)]` is only valid for extension-owned SQL types"
            ));
        }

        let sql = match T::argument_sql() {
            Ok(SqlMapping::As(sql)) => sql,
            Ok(SqlMapping::Composite | SqlMapping::Array(_)) => {
                return Err(eyre::eyre!(
                    "`creates = [{variant}(...)]` requires a concrete SQL type name"
                ));
            }
            Ok(SqlMapping::Skip) => {
                return Err(eyre::eyre!(
                    "`creates = [{variant}(...)]` cannot use a skipped SQL type"
                ));
            }
            Err(err) => return Err(err.into()),
        };
        let data = SqlDeclaredTypeEntityData {
            sql,
            name: name.to_string(),
            type_ident: T::TYPE_IDENT.to_string(),
        };
        Ok(make_declared(data))
    }

    pub fn sql(&self) -> String {
        match self {
            Self::Type(data) => data.sql.clone(),
            Self::Enum(data) => data.sql.clone(),
            Self::Function(data) => data.sql.clone(),
        }
    }

    pub fn type_ident(&self) -> Option<&str> {
        match self {
            Self::Type(data) | Self::Enum(data) => Some(data.type_ident.as_str()),
            Self::Function(_) => None,
        }
    }

    pub fn matches_type_ident(&self, type_ident: &str) -> bool {
        matches!(self.type_ident(), Some(value) if value == type_ident)
    }

    pub fn has_sql_declared_entity(&self, identifier: &SqlDeclared) -> bool {
        match (&identifier, &self) {
            (SqlDeclared::Type(ident_name), &Self::Type(data))
            | (SqlDeclared::Enum(ident_name), &Self::Enum(data)) => {
                if ident_name == &data.name || ident_name == &data.type_ident {
                    return true;
                }
                false
            }
            (SqlDeclared::Function(ident_name), &Self::Function(data)) => ident_name == &data.name,
            _ => false,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::metadata::{ArgumentError, ReturnsError, ReturnsRef, SqlMappingRef, TypeOrigin};

    struct ExtensionOwnedType;
    struct ExternalType;

    unsafe impl SqlTranslatable for ExtensionOwnedType {
        const TYPE_IDENT: &'static str = "tests::ExtensionOwnedType";
        const TYPE_ORIGIN: TypeOrigin = TypeOrigin::ThisExtension;
        const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> =
            Ok(SqlMappingRef::literal("extension_owned"));
        const RETURN_SQL: Result<ReturnsRef, ReturnsError> =
            Ok(ReturnsRef::One(SqlMappingRef::literal("extension_owned")));
    }

    unsafe impl SqlTranslatable for ExternalType {
        const TYPE_IDENT: &'static str = "tests::ExternalType";
        const TYPE_ORIGIN: TypeOrigin = TypeOrigin::External;
        const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> =
            Ok(SqlMappingRef::literal("text"));
        const RETURN_SQL: Result<ReturnsRef, ReturnsError> =
            Ok(ReturnsRef::One(SqlMappingRef::literal("text")));
    }

    #[test]
    fn build_type_accepts_extension_owned_types() {
        let declared = SqlDeclaredEntity::build_type::<ExtensionOwnedType>(
            "Type",
            "tests::ExtensionOwnedType",
        )
        .unwrap();

        assert_eq!(declared.type_ident(), Some("tests::ExtensionOwnedType"));
        assert_eq!(declared.sql(), "extension_owned");
    }

    #[test]
    fn build_type_rejects_external_types() {
        let error = SqlDeclaredEntity::build_type::<ExternalType>("Type", "tests::ExternalType")
            .unwrap_err();
        assert!(error.to_string().contains("only valid for extension-owned SQL types"));

        let error = SqlDeclaredEntity::build_type::<ExternalType>("Enum", "tests::ExternalType")
            .unwrap_err();
        assert!(error.to_string().contains("only valid for extension-owned SQL types"));
    }

    #[test]
    fn function_declarations_do_not_carry_type_idents() {
        let declared = SqlDeclaredEntity::build("Function", "tests::helper_fn").unwrap();

        assert_eq!(declared.type_ident(), None);
        assert_eq!(declared.sql(), "helper_fn");
        assert!(
            declared.has_sql_declared_entity(&SqlDeclared::Function("tests::helper_fn".into()))
        );
    }
}
