//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
#[derive(thiserror::Error, Debug, Clone, Copy)]
pub enum PgTriggerError {
    #[error("`PgTrigger`s can only be built from `FunctionCallInfo` instances which `pgrx::pg_sys::called_as_trigger(fcinfo)` returns `true`")]
    NotTrigger,
    #[error("`PgTrigger`s cannot be built from `NULL` `pgrx::pg_sys::FunctionCallInfo`s")]
    NullFunctionCallInfo,
    #[error(
        "`InvalidPgTriggerWhen` cannot be built from `event & TRIGGER_EVENT_TIMINGMASK` of `{0}"
    )]
    InvalidPgTriggerWhen(u32),
    #[error(
        "`InvalidPgTriggerOperation` cannot be built from `event & TRIGGER_EVENT_OPMASK` of `{0}"
    )]
    InvalidPgTriggerOperation(u32),
    #[error("core::str::Utf8Error: {0}")]
    CoreUtf8(#[from] core::str::Utf8Error),
    #[error("TryFromIntError: {0}")]
    TryFromInt(#[from] core::num::TryFromIntError),
    #[error("The `pgrx::pg_sys::TriggerData`'s `tg_trigger` field was a NULL pointer")]
    NullTrigger,
    #[error("The `pgrx::pg_sys::FunctionCallInfo`'s `context` field was a NULL pointer")]
    NullTriggerData,
    #[error("The `pgrx::pg_sys::TriggerData`'s `tg_relation` field was a NULL pointer")]
    NullRelation,
}
