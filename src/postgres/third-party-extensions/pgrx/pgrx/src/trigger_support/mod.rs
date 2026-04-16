//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
/*! Support for writing Rust trigger functions

A "no-op" trigger that gets the current [`PgHeapTuple`],
panicking (into a PostgreSQL error) if it doesn't exist:

```rust,no_run
use pgrx::prelude::*;

#[pg_trigger]
fn trigger_example<'a>(trigger: &'a PgTrigger<'a>) -> Result<
    Option<PgHeapTuple<'a, impl WhoAllocated>>,
    PgHeapTupleError,
> {
    Ok(Some(trigger.old().expect("No current HeapTuple")))
}
```

Trigger functions only accept one argument, a [`PgTrigger`], and they return a [`Result`] containing
either a [`PgHeapTuple`] or any error that implements [`impl std::error::Error`][std::error::Error].

# Use from SQL

The `trigger_example` example above would generate something like the following SQL:

```sql
-- pgrx-examples/triggers/src/lib.rs:25
-- triggers::trigger_example
CREATE FUNCTION "trigger_example"()
    RETURNS TRIGGER
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'trigger_example_wrapper';
```

Users could then use it like so:

```sql
CREATE TABLE test (
    id serial8 NOT NULL PRIMARY KEY,
    title varchar(50),
    description text,
    payload jsonb
);

CREATE TRIGGER test_trigger
    BEFORE INSERT ON test
    FOR EACH ROW
    EXECUTE PROCEDURE trigger_example();

INSERT INTO test (title, description, payload)
    VALUES ('Fox', 'a description', '{"key": "value"}');
```

This can also be done via the [`extension_sql`][crate::extension_sql] attribute:

```rust,no_run
# use pgrx::prelude::*;
#
# #[pg_trigger]
# fn trigger_example<'a>(trigger: &'a PgTrigger<'a>) -> Result<
#    Option<PgHeapTuple<'a, impl WhoAllocated>>,
#    PgHeapTupleError,
# > {
#    Ok(Some(trigger.old().expect("No current HeapTuple")))
# }
#
pgrx::extension_sql!(
    r#"
CREATE TABLE test (
    id serial8 NOT NULL PRIMARY KEY,
    title varchar(50),
    description text,
    payload jsonb
);
*
CREATE TRIGGER test_trigger BEFORE INSERT ON test FOR EACH ROW EXECUTE PROCEDURE trigger_example();
INSERT INTO test (title, description, payload) VALUES ('Fox', 'a description', '{"key": "value"}');
"#,
    name = "create_trigger",
    requires = [ trigger_example ]
);
```

# Working with [`WhoAllocated`][crate::WhoAllocated]

Trigger functions can return [`PgHeapTuple`]s which are [`AllocatedByRust`][crate::AllocatedByRust]
or [`AllocatedByPostgres`][crate::AllocatedByPostgres]. In most cases, it can be inferred by the compiler using
[`impl WhoAllocated<pg_sys::HeapTupleData>>`][crate::WhoAllocated].

When it can't, the function definition permits for it to be specified:

```rust,no_run
use pgrx::prelude::*;

#[pg_trigger]
fn example_allocated_by_rust<'a>(trigger: &'a PgTrigger<'a>) -> Result<
    Option<PgHeapTuple<'a, AllocatedByRust>>,
    PgHeapTupleError,
> {
    let current = trigger.old().expect("No current HeapTuple");
    Ok(Some(current.into_owned()))
}

#[pg_trigger]
fn example_allocated_by_postgres<'a>(trigger: &'a PgTrigger<'a>) -> Result<
    Option<PgHeapTuple<'a, AllocatedByPostgres>>,
    PgHeapTupleError,
> {
    let current = trigger.old().expect("No current HeapTuple");
    Ok(Some(current))
}
```

# Error Handling

Trigger functions can return any [`impl std::error::Error`][std::error::Error]. Returned errors
become PostgreSQL errors.


```rust,no_run
use pgrx::prelude::*;

#[derive(thiserror::Error, Debug)]
enum CustomTriggerError {
    #[error("No current HeapTuple")]
    NoCurrentHeapTuple,
    #[error("pgrx::PgHeapTupleError: {0}")]
    PgHeapTuple(PgHeapTupleError),
}

#[pg_trigger]
fn example_custom_error<'a>(trigger: &'a PgTrigger<'a>) -> Result<
    Option<PgHeapTuple<'a, impl WhoAllocated>>,
    CustomTriggerError,
> {
    trigger.old().map(|t| Some(t)).ok_or(CustomTriggerError::NoCurrentHeapTuple)
}
```

# Lifetimes

Triggers are free to use lifetimes to hone their code, the generated wrapper is as generous as possible.

```rust,no_run
use pgrx::prelude::*;

#[derive(thiserror::Error, Debug)]
enum CustomTriggerError<'a> {
    #[error("No current HeapTuple")]
    NoCurrentHeapTuple,
    #[error("pgrx::PgHeapTupleError: {0}")]
    PgHeapTuple(PgHeapTupleError),
    #[error("A borrowed error variant: {0}")]
    SomeStr(&'a str),
}

#[pg_trigger]
fn example_lifetimes<'a, 'b>(trigger: &'a PgTrigger<'a>) -> Result<
    Option<PgHeapTuple<'a, AllocatedByRust>>,
    CustomTriggerError<'b>,
> {
    return Err(CustomTriggerError::SomeStr("Oopsie"))
}
```

# Escape hatches

Unsafe [`pgrx::pg_sys::FunctionCallInfo`][crate::pg_sys::FunctionCallInfo] and
[`pgrx::pg_sys::TriggerData`][crate::pg_sys::TriggerData] (include its contained
[`pgrx::pg_sys::Trigger`][crate::pg_sys::Trigger]) accessors are available..

[`PgHeapTuple`]: crate::heap_tuple::PgHeapTuple
*/

mod pg_trigger;
mod pg_trigger_error;
mod pg_trigger_level;
mod pg_trigger_option;
mod pg_trigger_when;
mod trigger_tuple;

pub use pg_trigger::PgTrigger;
pub use pg_trigger_error::PgTriggerError;
pub use pg_trigger_level::PgTriggerLevel;
pub use pg_trigger_option::PgTriggerOperation;
pub use pg_trigger_when::PgTriggerWhen;
pub use trigger_tuple::TriggerTuple;

use crate::{is_a, pg_sys};

/// Represents the event that fired a trigger.
///
/// It is a newtype wrapper around a `pg_sys::TriggerData.tg_event` to prevent accidental misuse and
/// provides helper methods for determining how the event was raised.
#[derive(Debug, Copy, Clone)]
#[repr(transparent)]
pub struct TriggerEvent(u32);

impl TriggerEvent {
    #[inline]
    pub fn fired_by_insert(&self) -> bool {
        trigger_fired_by_insert(self.0)
    }

    #[inline]
    pub fn fired_by_delete(&self) -> bool {
        trigger_fired_by_delete(self.0)
    }

    #[inline]
    pub fn fired_by_update(&self) -> bool {
        trigger_fired_by_update(self.0)
    }

    #[inline]
    pub fn fired_by_truncate(&self) -> bool {
        trigger_fired_by_truncate(self.0)
    }

    #[inline]
    pub fn fired_for_row(&self) -> bool {
        trigger_fired_for_row(self.0)
    }

    #[inline]
    pub fn fired_for_statement(&self) -> bool {
        trigger_fired_for_statement(self.0)
    }

    #[inline]
    pub fn fired_before(&self) -> bool {
        trigger_fired_before(self.0)
    }

    #[inline]
    pub fn fired_after(&self) -> bool {
        trigger_fired_after(self.0)
    }

    #[inline]
    pub fn fired_instead(&self) -> bool {
        trigger_fired_instead(self.0)
    }
}

#[inline]
pub unsafe fn called_as_trigger(fcinfo: pg_sys::FunctionCallInfo) -> bool {
    let fcinfo = fcinfo.as_ref().expect("fcinfo was null");
    !fcinfo.context.is_null() && is_a(fcinfo.context, pg_sys::NodeTag::T_TriggerData)
}

#[inline]
pub fn trigger_fired_by_insert(event: u32) -> bool {
    event & pg_sys::TRIGGER_EVENT_OPMASK == pg_sys::TRIGGER_EVENT_INSERT
}

#[inline]
pub fn trigger_fired_by_delete(event: u32) -> bool {
    event & pg_sys::TRIGGER_EVENT_OPMASK == pg_sys::TRIGGER_EVENT_DELETE
}

#[inline]
pub fn trigger_fired_by_update(event: u32) -> bool {
    event & pg_sys::TRIGGER_EVENT_OPMASK == pg_sys::TRIGGER_EVENT_UPDATE
}

#[inline]
pub fn trigger_fired_by_truncate(event: u32) -> bool {
    event & pg_sys::TRIGGER_EVENT_OPMASK == pg_sys::TRIGGER_EVENT_TRUNCATE
}

#[inline]
pub fn trigger_fired_for_row(event: u32) -> bool {
    event & pg_sys::TRIGGER_EVENT_ROW != 0
}

#[inline]
pub fn trigger_fired_for_statement(event: u32) -> bool {
    !trigger_fired_for_row(event)
}

#[inline]
pub fn trigger_fired_before(event: u32) -> bool {
    event & pg_sys::TRIGGER_EVENT_TIMINGMASK == pg_sys::TRIGGER_EVENT_BEFORE
}

#[inline]
pub fn trigger_fired_after(event: u32) -> bool {
    event & pg_sys::TRIGGER_EVENT_TIMINGMASK == pg_sys::TRIGGER_EVENT_AFTER
}

#[inline]
pub fn trigger_fired_instead(event: u32) -> bool {
    event & pg_sys::TRIGGER_EVENT_TIMINGMASK == pg_sys::TRIGGER_EVENT_INSTEAD
}
