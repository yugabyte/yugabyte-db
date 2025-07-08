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

`pgrx_module_magic!()` related macro expansion for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
use super::{SqlGraphEntity, SqlGraphIdentifier, ToSql};
use std::collections::HashMap;
use std::path::PathBuf;
use thiserror::Error;

/// The parsed contents of a `.control` file.
///
/// ```rust
/// use pgrx_sql_entity_graph::ControlFile;
/// use std::convert::TryFrom;
/// # fn main() -> eyre::Result<()> {
/// # // arrays.control chosen because it does **NOT** use the @CARGO_VERSION@ variable
/// let context = include_str!("../../pgrx-examples/arrays/arrays.control");
/// let _control_file = ControlFile::try_from(context)?;
/// # Ok(())
/// # }
/// ```
#[derive(Debug, Clone, Hash, PartialOrd, Ord, PartialEq, Eq)]
pub struct ControlFile {
    pub comment: String,
    pub default_version: String,
    pub module_pathname: Option<String>,
    pub relocatable: bool,
    pub superuser: bool,
    pub schema: Option<String>,
    pub trusted: bool,
}

impl ControlFile {
    /// Parse a `.control` file, performing all known pgrx dynamic variable substitutions.
    ///
    /// # Supported Dynamic Variable Substitutions
    ///
    /// `@CARGO_VERSION@`:  Replaced with the value of the environment variable `CARGO_PKG_VERSION`,
    ///                     which is set by cargo, or failing that, `cargo-pgrx` using the package
    ///                     version from the extension's `Cargo.toml` file
    ///
    /// # Errors
    ///
    /// Returns a `ControlFileError` if any of the required fields are missing from the input string
    /// or if any required environment variables (for dynamic variable substitution) are missing
    ///
    /// ```rust
    /// use pgrx_sql_entity_graph::ControlFile;
    /// # fn main() -> eyre::Result<()> {
    /// # // arrays.control chosen because it does **NOT** use the @CARGO_VERSION@ variable
    /// let context = include_str!("../../pgrx-examples/arrays/arrays.control");
    /// let _control_file = ControlFile::from_str(context)?;
    /// # Ok(())
    /// # }
    /// ```
    #[allow(clippy::should_implement_trait)]
    pub fn from_str(input: &str) -> Result<Self, ControlFileError> {
        fn do_var_replacements(mut input: String) -> Result<String, ControlFileError> {
            const CARGO_VERSION: &str = "@CARGO_VERSION@";

            // endeavor to not require external values if they're not used by the input
            if input.contains(CARGO_VERSION) {
                input = input.replace(
                    CARGO_VERSION,
                    &std::env::var("CARGO_PKG_VERSION").map_err(|_| {
                        ControlFileError::MissingEnvvar("CARGO_PKG_VERSION".to_string())
                    })?,
                );
            }

            Ok(input)
        }

        let mut temp = HashMap::new();
        for line in input.lines() {
            let parts: Vec<&str> = line.split('=').collect();

            if parts.len() != 2 {
                continue;
            }

            let (k, v) = (parts.first().unwrap().trim(), parts.get(1).unwrap().trim());

            let v = v.trim_start_matches('\'');
            let v = v.trim_end_matches('\'');

            temp.insert(k, do_var_replacements(v.to_string())?);
        }
        let control_file = ControlFile {
            comment: temp
                .get("comment")
                .ok_or(ControlFileError::MissingField { field: "comment" })?
                .to_string(),
            default_version: temp
                .get("default_version")
                .ok_or(ControlFileError::MissingField { field: "default_version" })?
                .to_string(),
            module_pathname: temp.get("module_pathname").map(|v| v.to_string()),
            relocatable: temp
                .get("relocatable")
                .ok_or(ControlFileError::MissingField { field: "relocatable" })?
                == "true",
            superuser: temp
                .get("superuser")
                .ok_or(ControlFileError::MissingField { field: "superuser" })?
                == "true",
            schema: temp.get("schema").map(|v| v.to_string()),
            trusted: if let Some(v) = temp.get("trusted") { v == "true" } else { false },
        };

        if !control_file.superuser && control_file.trusted {
            // `trusted` is irrelevant if `superuser` is false.
            return Err(ControlFileError::RedundantField { field: "trusted" });
        }

        Ok(control_file)
    }
}

impl From<ControlFile> for SqlGraphEntity {
    fn from(val: ControlFile) -> Self {
        SqlGraphEntity::ExtensionRoot(val)
    }
}

/// An error met while parsing a `.control` file.
#[derive(Debug, Error)]
pub enum ControlFileError {
    #[error("Filesystem error reading control file")]
    IOError {
        #[from]
        error: std::io::Error,
    },
    #[error("Missing field in control file! Please add `{field}`.")]
    MissingField { field: &'static str },
    #[error("Redundant field in control file! Please remove `{field}`.")]
    RedundantField { field: &'static str },
    #[error("Missing environment variable: {0}")]
    MissingEnvvar(String),
}

impl TryFrom<PathBuf> for ControlFile {
    type Error = ControlFileError;

    fn try_from(value: PathBuf) -> Result<Self, Self::Error> {
        let contents = std::fs::read_to_string(value)?;
        ControlFile::try_from(contents.as_str())
    }
}

impl TryFrom<&str> for ControlFile {
    type Error = ControlFileError;

    fn try_from(input: &str) -> Result<Self, Self::Error> {
        Self::from_str(input)
    }
}

impl ToSql for ControlFile {
    fn to_sql(&self, _context: &super::PgrxSql) -> eyre::Result<String> {
        let comment = r#"
/*
This file is auto generated by pgrx.

The ordering of items is not stable, it is driven by a dependency graph.
*/
"#;
        Ok(comment.into())
    }
}

impl SqlGraphIdentifier for ControlFile {
    fn dot_identifier(&self) -> String {
        "extension root".into()
    }
    fn rust_identifier(&self) -> String {
        "root".into()
    }

    fn file(&self) -> Option<&'static str> {
        None
    }

    fn line(&self) -> Option<u32> {
        None
    }
}
