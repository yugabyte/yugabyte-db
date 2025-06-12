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

[Aggregate](https://www.postgresql.org/docs/current/xaggr.html) support.

Most items of this trait map directly to a [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html)
functionality.

Aggregates are created by implementing [`Aggregate`] for a type and decorating the implementation with
[`#[pg_aggregate]`](pgrx_macros::pg_aggregate).

Definition of the aggregate is done via settings in the type's [`Aggregate`] implementation. While
the trait itself has several items, only a few are required, the macro will fill in the others with unused stubs.

# Minimal Example

```rust
use pgrx::prelude::*;
use serde::{Serialize, Deserialize};

// pg_module_magic!(); // Uncomment this outside of docs!

#[derive(Copy, Clone, Default, PostgresType, Serialize, Deserialize)]
pub struct DemoSum {
    count: i32,
}

#[pg_aggregate]
impl Aggregate for DemoSum {
    const INITIAL_CONDITION: Option<&'static str> = Some(r#"{ "count": 0 }"#);
    type Args = i32;
    fn state(
        mut current: Self::State,
        arg: Self::Args,
        _fcinfo: pg_sys::FunctionCallInfo
    ) -> Self::State {
        current.count += arg;
        current
    }
}
```

This creates SQL like so:

```sql
-- src/lib.rs:11
-- aggregate::DemoSum
CREATE AGGREGATE DemoSum (
    integer /* i32 */
)
(
    SFUNC = "demo_sum_state", /* aggregate::DemoSum::state */
    STYPE = DemoSum, /* aggregate::DemoSum */
    INITCOND = '{ "count": 0 }' /* aggregate::DemoSum::INITIAL_CONDITION */
);
```

Example of usage:

```sql
aggregate=# CREATE TABLE demo_table (value INTEGER);
CREATE TABLE
aggregate=# INSERT INTO demo_table (value) VALUES (1), (2), (3);
INSERT 0 3
aggregate=# SELECT DemoSum(value) FROM demo_table;
    demosum
-------------
 {"count":6}
(1 row)
```

## Multiple Arguments

Sometimes aggregates need to handle multiple arguments. The
[`Aggregate::Args`] associated type can be a tuple:

```rust
# use pgrx::prelude::*;
# use serde::{Serialize, Deserialize};
#
# #[derive(Copy, Clone, Default, PostgresType, Serialize, Deserialize)]
# pub struct DemoSum {
#     count: i32,
# }
#
#[pg_aggregate]
impl Aggregate for DemoSum {
    const INITIAL_CONDITION: Option<&'static str> = Some(r#"{ "count": 0 }"#);
    type Args = (i32, i32);
    fn state(
        mut current: Self::State,
        (arg1, arg2): Self::Args,
        _fcinfo: pg_sys::FunctionCallInfo
    ) -> Self::State {
        current.count += arg1;
        current.count += arg2;
        current
    }
}
```

Creates:

```sql
-- src/lib.rs:11
-- aggregate::DemoSum
CREATE AGGREGATE DemoSum (
    integer, /* i32 */
    integer /* i32 */
)
(
    SFUNC = "demo_sum_state", /* aggregate::DemoSum::state */
    STYPE = DemoSum, /* aggregate::DemoSum */
    INITCOND = '{ "count": 0 }' /* aggregate::DemoSum::INITIAL_CONDITION */
);
```

## Named Arguments

The [`name!(ident, Type)`][macro@crate::name] macro can be used to set the name of an argument:

```rust
# use pgrx::prelude::*;
# use serde::{Serialize, Deserialize};
#
# #[derive(Copy, Clone, Default, PostgresType, Serialize, Deserialize)]
# pub struct DemoSum {
#     count: i32,
# }
#
# #[pg_aggregate]
impl Aggregate for DemoSum {
    const INITIAL_CONDITION: Option<&'static str> = Some(r#"{ "count": 0 }"#);
    type Args = (
        i32,
        name!(extra, i32),
    );
    fn state(
        mut current: Self::State,
        (arg1, extra): Self::Args,
        _fcinfo: pg_sys::FunctionCallInfo
    ) -> Self::State {
        todo!()
    }
}
```

Creates:

```sql
-- src/lib.rs:11
-- aggregate::DemoSum
CREATE AGGREGATE DemoSum (
    integer, /* i32 */
    "extra" integer /* i32 */
)
(
    SFUNC = "demo_sum_state", /* aggregate::DemoSum::state */
    STYPE = DemoSum, /* aggregate::DemoSum */
    INITCOND = '{ "count": 0 }' /* aggregate::DemoSum::INITIAL_CONDITION */
);
```

## Function attributes

Functions inside the `impl` may use the [`#[pgrx]`](macro@crate::pgrx) attribute. It
accepts the same parameters as [`#[pg_extern]`][macro@pgrx-macros::pg_extern].

```rust
# use pgrx::prelude::*;
# use serde::{Serialize, Deserialize};
#
# #[derive(Copy, Clone, Default, PostgresType, Serialize, Deserialize)]
# pub struct DemoSum {
#     count: i32,
# }
#
#[pg_aggregate]
impl Aggregate for DemoSum {
    const INITIAL_CONDITION: Option<&'static str> = Some(r#"{ "count": 0 }"#);
    type Args = i32;
    #[pgrx(parallel_safe, immutable)]
    fn state(
        mut current: Self::State,
        arg: Self::Args,
        _fcinfo: pg_sys::FunctionCallInfo
    ) -> Self::State {
        todo!()
    }
}
```

Generates:

```sql
-- src/lib.rs:11
-- aggregate::demo_sum_state
CREATE FUNCTION "demo_sum_state"(
    "this" DemoSum, /* aggregate::DemoSum */
    "arg_one" integer /* i32 */
) RETURNS DemoSum /* aggregate::DemoSum */
PARALLEL SAFE IMMUTABLE STRICT
LANGUAGE c /* Rust */
AS 'MODULE_PATHNAME', 'demo_sum_state_wrapper';
```

## Non-`Self` State

Sometimes it's useful to have aggregates share state, or use some other type for state.

```rust
# use pgrx::prelude::*;
# use serde::{Serialize, Deserialize};
#
#[derive(Copy, Clone, Default, PostgresType, Serialize, Deserialize)]
pub struct DemoSumState {
    count: i32,
}

pub struct DemoSum;

#[pg_aggregate]
impl Aggregate for DemoSum {
    const INITIAL_CONDITION: Option<&'static str> = Some(r#"{ "count": 0 }"#);
    type Args = i32;
    type State = DemoSumState;
    fn state(
        mut current: Self::State,
        arg: Self::Args,
        _fcinfo: pg_sys::FunctionCallInfo
    ) -> Self::State {
        todo!()
    }
}
```

Creates:

```sql
-- src/lib.rs:13
-- aggregate::demo_sum_state
CREATE FUNCTION "demo_sum_state"(
    "this" DemoSumState, /* aggregate::DemoSumState */
    "arg_one" integer /* i32 */
) RETURNS DemoSumState /* aggregate::DemoSumState */
STRICT
LANGUAGE c /* Rust */
AS 'MODULE_PATHNAME', 'demo_sum_state_wrapper';

-- src/lib.rs:13
-- aggregate::DemoSum
CREATE AGGREGATE DemoSum (
    integer /* i32 */
)
(
    SFUNC = "demo_sum_state", /* aggregate::DemoSum::state */
    STYPE = DemoSumState, /* aggregate::DemoSumState */
    INITCOND = '{ "count": 0 }' /* aggregate::DemoSum::INITIAL_CONDITION */
);
```

*/

use crate::error;
use crate::memcxt::PgMemoryContexts;
use crate::pg_sys::{AggCheckCallContext, CurrentMemoryContext, FunctionCallInfo, MemoryContext};
use crate::pgbox::PgBox;

pub use pgrx_sql_entity_graph::{FinalizeModify, ParallelOption};

/// Aggregate implementation trait.
///
/// When decorated with [`#[pgrx_macros::pg_aggregate]`](pgrx_macros::pg_aggregate), enables the
/// generation of [`CREATE AGGREGATE`](https://www.postgresql.org/docs/current/sql-createaggregate.html)
/// SQL.
///
/// The [`#[pgrx_macros::pg_aggregate]`](pgrx_macros::pg_aggregate) will automatically fill fields
/// marked optional with stubs.
pub trait Aggregate
where
    Self: Sized,
{
    /// The type of the return value on `state` and `combine` functions.
    ///
    /// For an aggregate type which does not have a `PgVarlenaInOutFuncs` implementation,
    /// this can be left out, or set to it's default, `Self`.
    ///
    /// For an aggregate type which **does** have a `PgVarlenaInOutFuncs` implementation,
    /// this should be set to `PgVarlena<Self>`.
    ///
    /// Other types are supported as well, this can be useful if multiple aggregates share a state.
    type State;

    /// The type of the argument(s).
    ///
    /// For a single argument, provide the type directly.
    ///
    /// For multiple arguments, provide a tuple.
    ///
    /// Use [`pgrx::name!()`](crate::name) to set the SQL name of the argument.
    ///
    /// If the final argument is to be variadic, use [`pgrx::variadic`](crate::variadic). When used
    /// with [`pgrx::name!()`](crate::name), it must be used **inside** the [`pgrx::name!()`](crate::name) macro.
    type Args;

    /// The types of the direct argument(s) to an ordered-set aggregate's `finalize`.
    ///
    /// **Only effective if `ORDERED_SET` is `true`.**
    ///
    /// For a single argument, provide the type directly.
    ///
    /// For multiple arguments, provide a tuple.
    ///
    /// For no arguments, don't set this, or set it to `()` (the default).
    ///
    /// `pgrx` does not support `argname` as it is only used for documentation purposes.
    ///
    /// If the final argument is to be variadic, use `pgrx::Variadic`.
    ///
    /// **Optional:** This function can be skipped, `#[pg_aggregate]` will create a stub.
    type OrderedSetArgs;

    /// **Optional:** This function can be skipped, `#[pg_aggregate]` will create a stub.
    type Finalize;

    /// **Optional:** This function can be skipped, `#[pg_aggregate]` will create a stub.
    type MovingState;

    /// The name of the aggregate. (eg. What you'd pass to `SELECT agg(col) FROM tab`.)
    const NAME: &'static str;

    /// Set to true if this is an ordered set aggregate.
    ///
    /// If set, the `OrderedSetArgs` associated type becomes effective, this allows for
    /// direct arguments to the `finalize` function.
    ///
    /// See <https://www.postgresql.org/docs/current/xaggr.html#XAGGR-ORDERED-SET-AGGREGATES>
    /// for more information.
    ///
    /// **Optional:** This const can be skipped, `#[pg_aggregate]` will create a stub.
    const ORDERED_SET: bool = false;

    /// **Optional:** This const can be skipped, `#[pg_aggregate]` will create a stub.
    const PARALLEL: Option<ParallelOption> = None;

    /// **Optional:** This const can be skipped, `#[pg_aggregate]` will create a stub.
    const FINALIZE_MODIFY: Option<FinalizeModify> = None;

    /// **Optional:** This const can be skipped, `#[pg_aggregate]` will create a stub.
    const MOVING_FINALIZE_MODIFY: Option<FinalizeModify> = None;

    /// **Optional:** This const can be skipped, `#[pg_aggregate]` will create a stub.
    const INITIAL_CONDITION: Option<&'static str> = None;

    /// **Optional:** This const can be skipped, `#[pg_aggregate]` will create a stub.
    const SORT_OPERATOR: Option<&'static str> = None;

    /// **Optional:** This const can be skipped, `#[pg_aggregate]` will create a stub.
    const MOVING_INITIAL_CONDITION: Option<&'static str> = None;

    /// **Optional:** This const can be skipped, `#[pg_aggregate]` will create a stub.
    const HYPOTHETICAL: bool = false;

    fn state(current: Self::State, v: Self::Args, fcinfo: FunctionCallInfo) -> Self::State;

    /// The `OrderedSetArgs` is `()` unless `ORDERED_SET` is `true` and `OrderedSetArgs` is configured.
    ///
    /// **Optional:** This function can be skipped, `#[pg_aggregate]` will create a stub.
    fn finalize(
        current: Self::State,
        direct_args: Self::OrderedSetArgs,
        fcinfo: FunctionCallInfo,
    ) -> Self::Finalize;

    /// **Optional:** This function can be skipped, `#[pg_aggregate]` will create a stub.
    fn combine(current: Self::State, _other: Self::State, fcinfo: FunctionCallInfo) -> Self::State;

    /// **Optional:** This function can be skipped, `#[pg_aggregate]` will create a stub.
    fn serial(current: Self::State, fcinfo: FunctionCallInfo) -> Vec<u8>;

    /// **Optional:** This function can be skipped, `#[pg_aggregate]` will create a stub.
    fn deserial(
        current: Self::State,
        _buf: Vec<u8>,
        _internal: PgBox<Self::State>,
        fcinfo: FunctionCallInfo,
    ) -> PgBox<Self::State>;

    /// **Optional:** This function can be skipped, `#[pg_aggregate]` will create a stub.
    fn moving_state(
        _mstate: Self::MovingState,
        _v: Self::Args,
        fcinfo: FunctionCallInfo,
    ) -> Self::MovingState;

    /// **Optional:** This function can be skipped, `#[pg_aggregate]` will create a stub.
    fn moving_state_inverse(
        _mstate: Self::MovingState,
        _v: Self::Args,
        fcinfo: FunctionCallInfo,
    ) -> Self::MovingState;

    /// The `OrderedSetArgs` is `()` unless `ORDERED_SET` is `true` and `OrderedSetArgs` is configured.
    ///
    /// **Optional:** This function can be skipped, `#[pg_aggregate]` will create a stub.
    fn moving_finalize(
        _mstate: Self::MovingState,
        direct_args: Self::OrderedSetArgs,
        fcinfo: FunctionCallInfo,
    ) -> Self::Finalize;

    #[inline(always)]
    unsafe fn memory_context(fcinfo: FunctionCallInfo) -> Option<MemoryContext> {
        if fcinfo.is_null() {
            return Some(CurrentMemoryContext);
        }
        let mut memory_context = std::ptr::null_mut();
        let is_aggregate = AggCheckCallContext(fcinfo, &mut memory_context);
        if is_aggregate == 0 {
            None
        } else {
            debug_assert!(!memory_context.is_null());
            Some(memory_context)
        }
    }

    #[inline(always)]
    unsafe fn in_memory_context<
        R,
        F: FnOnce(&mut PgMemoryContexts) -> R + std::panic::UnwindSafe + std::panic::RefUnwindSafe,
    >(
        fcinfo: FunctionCallInfo,
        f: F,
    ) -> R {
        unsafe {
            let aggregate_memory_context = Self::memory_context(fcinfo).unwrap_or_else(|| {
                error!("Cannot access Aggregate memory contexts when not an aggregate.")
            });

            // SAFETY: Self::memory_context() will always give us a valid MemoryContext in which
            // to operate
            PgMemoryContexts::For(aggregate_memory_context).switch_to(f)
        }
    }
}
