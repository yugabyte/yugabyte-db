//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use core::ffi::CStr;
use pgrx::aggregate::*;
use pgrx::prelude::*;
use pgrx::StringInfo;
use serde::{Deserialize, Serialize};
use std::str::FromStr;

::pgrx::pg_module_magic!();

#[derive(Copy, Clone, PostgresType, Serialize, Deserialize)]
#[pgvarlena_inoutfuncs]
#[derive(Default)]
pub struct IntegerAvgState {
    sum: i32,
    n: i32,
}

impl IntegerAvgState {
    #[inline(always)]
    fn state(
        mut current: <Self as Aggregate>::State,
        arg: <Self as Aggregate>::Args,
    ) -> <Self as Aggregate>::State {
        if let Some(arg) = arg {
            current.sum += arg;
            current.n += 1;
        }
        current
    }

    #[inline(always)]
    fn finalize(current: <Self as Aggregate>::State) -> <Self as Aggregate>::Finalize {
        current.sum / current.n
    }
}

impl PgVarlenaInOutFuncs for IntegerAvgState {
    fn input(input: &CStr) -> PgVarlena<Self> {
        let mut result = PgVarlena::<Self>::new();

        let mut split = input.to_bytes().split(|b| *b == b',');
        let sum = split
            .next()
            .map(|value| {
                i32::from_str(unsafe { std::str::from_utf8_unchecked(value) }).expect("invalid i32")
            })
            .expect("expected sum");
        let n = split
            .next()
            .map(|value| {
                i32::from_str(unsafe { std::str::from_utf8_unchecked(value) }).expect("invalid i32")
            })
            .expect("expected n");

        result.sum = sum;
        result.n = n;

        result
    }
    fn output(&self, buffer: &mut StringInfo) {
        buffer.push_str(&format!("{},{}", self.sum, self.n));
    }
}

// In order to improve the testability of your code, it's encouraged to make this implementation
// call to your own functions which don't require a PostgreSQL made [`pgrx::pg_sys::FunctionCallInfo`].
#[pg_aggregate]
impl Aggregate for IntegerAvgState {
    type State = PgVarlena<Self>;
    type Args = pgrx::name!(value, Option<i32>);
    const NAME: &'static str = "DEMOAVG";

    const INITIAL_CONDITION: Option<&'static str> = Some("0,0");

    #[pgrx(parallel_safe, immutable)]
    fn state(
        current: Self::State,
        arg: Self::Args,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::State {
        Self::state(current, arg)
    }

    // You can skip all these:
    type Finalize = i32;
    // type OrderBy = i32;
    // type MovingState = i32;

    // const PARALLEL: Option<ParallelOption> = Some(ParallelOption::Safe);
    // const FINALIZE_MODIFY: Option<FinalizeModify> = Some(FinalizeModify::ReadWrite);
    // const MOVING_FINALIZE_MODIFY: Option<FinalizeModify> = Some(FinalizeModify::ReadWrite);

    // const SORT_OPERATOR: Option<&'static str> = Some("sortop");
    // const MOVING_INITIAL_CONDITION: Option<&'static str> = Some("1,1");
    // const HYPOTHETICAL: bool = true;

    // You can skip all these:
    fn finalize(
        current: Self::State,
        _direct_args: Self::OrderedSetArgs,
        _fcinfo: pgrx::pg_sys::FunctionCallInfo,
    ) -> Self::Finalize {
        Self::finalize(current)
    }

    // fn combine(current: Self::State, _other: Self::State, _fcinfo: pgrx::pg_sys::FunctionCallInfo) -> Self::State {
    //     unimplemented!()
    // }

    // fn serial(current: Self::State, _fcinfo: pgrx::pg_sys::FunctionCallInfo) -> Vec<u8> {
    //     unimplemented!()
    // }

    // fn deserial(current: Self::State, _buf: Vec<u8>, _internal: PgBox<Self::State>, _fcinfo: pgrx::pg_sys::FunctionCallInfo) -> PgBox<Self::State> {
    //     unimplemented!()
    // }

    // fn moving_state(_mstate: Self::MovingState, _v: Self::Args, _fcinfo: pgrx::pg_sys::FunctionCallInfo) -> Self::MovingState {
    //     unimplemented!()
    // }

    // fn moving_state_inverse(_mstate: Self::MovingState, _v: Self::Args, _fcinfo: pgrx::pg_sys::FunctionCallInfo) -> Self::MovingState {
    //     unimplemented!()
    // }

    // fn moving_finalize(_mstate: Self::MovingState, _fcinfo: pgrx::pg_sys::FunctionCallInfo) -> Self::Finalize {
    //     unimplemented!()
    // }
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use crate::IntegerAvgState;
    use pgrx::prelude::*;

    #[pg_test]
    fn test_integer_avg_state() {
        let avg_state = PgVarlena::<IntegerAvgState>::default();
        let avg_state = IntegerAvgState::state(avg_state, Some(1));
        let avg_state = IntegerAvgState::state(avg_state, Some(2));
        let avg_state = IntegerAvgState::state(avg_state, Some(3));
        assert_eq!(2, IntegerAvgState::finalize(avg_state));
    }

    #[pg_test]
    fn test_integer_avg_state_sql() -> Result<(), spi::Error> {
        Spi::run("CREATE TABLE demo_table (value INTEGER);")?;
        Spi::run("INSERT INTO demo_table (value) VALUES (1), (2), (3);")?;
        let retval = Spi::get_one::<i32>("SELECT DEMOAVG(value) FROM demo_table;");
        assert_eq!(retval, Ok(Some(2)));
        Ok(())
    }
    #[pg_test]
    fn test_integer_avg_with_null() -> Result<(), spi::Error> {
        Spi::run("CREATE TABLE demo_table (value INTEGER);")?;
        Spi::run("INSERT INTO demo_table (value) VALUES (1), (NULL), (3);")?;
        let retval = Spi::get_one::<i32>("SELECT DEMOAVG(value) FROM demo_table;");
        assert_eq!(retval, Ok(Some(2)));
        Ok(())
    }
}

#[cfg(test)]
pub mod pg_test {
    pub fn setup(_options: Vec<&str>) {
        // perform one-off initialization when the pg_test framework starts
    }

    pub fn postgresql_conf_options() -> Vec<&'static str> {
        // return any postgresql.conf settings that are required for your tests
        vec![]
    }
}
