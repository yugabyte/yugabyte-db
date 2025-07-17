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

`#[pg_extern]` related cast entities for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/

/// The output of a [`PgCast`](crate::PgCast) from `quote::ToTokens::to_tokens`.
#[derive(Debug, Clone, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub enum PgCastEntity {
    Default,
    Assignment,
    Implicit,
}
