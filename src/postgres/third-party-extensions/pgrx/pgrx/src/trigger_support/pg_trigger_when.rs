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
use crate::trigger_support::{PgTriggerError, TriggerEvent};

/// When a trigger happened
///
/// Maps from a `TEXT` of `BEFORE`, `AFTER`, or `INSTEAD OF`.
///
/// Can be calculated from a `pgrx_pg_sys::TriggerEvent`.
// Postgres constants: https://cs.github.com/postgres/postgres/blob/36d4efe779bfc7190ea1c1cf8deb0d945b726663/src/include/commands/trigger.h?q=TRIGGER_FIRED_BEFORE#L100-L102
// Postgres defines: https://cs.github.com/postgres/postgres/blob/36d4efe779bfc7190ea1c1cf8deb0d945b726663/src/include/commands/trigger.h?q=TRIGGER_FIRED_BEFORE#L128-L135
pub enum PgTriggerWhen {
    /// `BEFORE`
    Before,
    /// `AFTER`
    After,
    /// `INSTEAD OF`
    InsteadOf,
}

impl TryFrom<TriggerEvent> for PgTriggerWhen {
    type Error = PgTriggerError;
    fn try_from(event: TriggerEvent) -> Result<Self, Self::Error> {
        match event.0 & pg_sys::TRIGGER_EVENT_TIMINGMASK {
            pg_sys::TRIGGER_EVENT_BEFORE => Ok(Self::Before),
            pg_sys::TRIGGER_EVENT_AFTER => Ok(Self::After),
            pg_sys::TRIGGER_EVENT_INSTEAD => Ok(Self::InsteadOf),
            v => Err(PgTriggerError::InvalidPgTriggerWhen(v)),
        }
    }
}

impl Display for PgTriggerWhen {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            PgTriggerWhen::Before => "BEFORE",
            PgTriggerWhen::After => "AFTER",
            PgTriggerWhen::InsteadOf => "INSTEAD OF",
        })
    }
}
