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

Function and type level metadata entities for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.


*/
use super::{ArgumentError, Returns, ReturnsError, SqlMapping};

/// Describes whether a SQL type reference should resolve to schema emitted by this
/// extension or be treated as an external SQL type.
#[derive(Clone, Copy, Debug, Hash, Eq, PartialEq, Ord, PartialOrd)]
pub enum TypeOrigin {
    /// The extension being built is responsible for emitting this type into the
    /// schema graph.
    ThisExtension,
    /// The type already exists outside this extension's schema graph.
    External,
}

#[derive(Clone, Debug, Hash, Eq, PartialEq, Ord, PartialOrd)]
pub struct FunctionMetadataEntity<'a> {
    pub arguments: Vec<FunctionMetadataTypeEntity<'a>>,
    pub retval: FunctionMetadataTypeEntity<'a>,
    pub path: &'a str,
}

#[derive(Clone, Debug, Hash, Eq, PartialEq, Ord, PartialOrd)]
pub struct FunctionMetadataTypeEntity<'a> {
    pub resolution: Option<FunctionMetadataTypeResolutionEntity<'a>>,
    pub argument_sql: Result<SqlMapping, ArgumentError>,
    pub return_sql: Result<Returns, ReturnsError>,
}

#[derive(Clone, Copy, Debug, Hash, Eq, PartialEq, Ord, PartialOrd)]
pub struct FunctionMetadataTypeResolutionEntity<'a> {
    pub type_ident: &'a str,
    pub type_origin: TypeOrigin,
}

impl<'a> FunctionMetadataTypeEntity<'a> {
    pub const fn resolved(
        type_ident: &'a str,
        type_origin: TypeOrigin,
        argument_sql: Result<SqlMapping, ArgumentError>,
        return_sql: Result<Returns, ReturnsError>,
    ) -> Self {
        Self {
            resolution: Some(FunctionMetadataTypeResolutionEntity { type_ident, type_origin }),
            argument_sql,
            return_sql,
        }
    }

    pub const fn sql_only(
        argument_sql: Result<SqlMapping, ArgumentError>,
        return_sql: Result<Returns, ReturnsError>,
    ) -> Self {
        Self { resolution: None, argument_sql, return_sql }
    }

    pub const fn type_ident(&self) -> Option<&'a str> {
        match self.resolution {
            Some(resolution) => Some(resolution.type_ident),
            None => None,
        }
    }

    pub const fn type_origin(&self) -> Option<TypeOrigin> {
        match self.resolution {
            Some(resolution) => Some(resolution.type_origin),
            None => None,
        }
    }

    pub const fn needs_type_resolution(&self) -> bool {
        self.resolution.is_some()
    }
}
