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

Function and type level metadata for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.


*/

mod entity;
mod function_metadata;
mod return_variant;
mod sql_translatable;

pub use entity::{FunctionMetadataEntity, FunctionMetadataTypeEntity};
pub use function_metadata::FunctionMetadata;
pub use return_variant::{Returns, ReturnsError};
pub use sql_translatable::{ArgumentError, SqlMapping, SqlTranslatable};
