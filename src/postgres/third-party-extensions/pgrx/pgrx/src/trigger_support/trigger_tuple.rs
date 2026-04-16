//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
/// Indicates which trigger tuple to convert into a [`PgHeapTuple`].
///
/// [`PgHeapTuple`]: crate::heap_tuple::PgHeapTuple
#[derive(Debug, Copy, Clone)]
pub enum TriggerTuple {
    /// Represents the new database row for INSERT/UPDATE operations in row-level triggers.
    New,

    /// Represents the old database row for UPDATE/DELETE operations in row-level triggers.
    Old,
}
