use core::fmt::{Display, Formatter};
use std::fmt;
//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::pg_sys;
use crate::trigger_support::TriggerEvent;

/// The level of a trigger
///
/// Maps from a `TEXT` of `ROW` or `STATEMENT`.
///
/// Can be calculated from a `pgrx_pg_sys::TriggerEvent`.
// Postgres constants: https://cs.github.com/postgres/postgres/blob/36d4efe779bfc7190ea1c1cf8deb0d945b726663/src/include/commands/trigger.h?q=TRIGGER_FIRED_BEFORE#L98
// Postgres defines: https://cs.github.com/postgres/postgres/blob/36d4efe779bfc7190ea1c1cf8deb0d945b726663/src/include/commands/trigger.h?q=TRIGGER_FIRED_BEFORE#L122-L126
pub enum PgTriggerLevel {
    /// `ROW`
    Row,
    /// `STATEMENT`
    Statement,
}

impl From<TriggerEvent> for PgTriggerLevel {
    fn from(event: TriggerEvent) -> Self {
        match event.0 & pg_sys::TRIGGER_EVENT_ROW {
            0 => PgTriggerLevel::Statement,
            _ => PgTriggerLevel::Row,
        }
    }
}

impl Display for PgTriggerLevel {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            PgTriggerLevel::Row => "ROW",
            PgTriggerLevel::Statement => "STATEMENT",
        })
    }
}
