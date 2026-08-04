use pgrx::aggregate::*;
use pgrx::prelude::*;
use pgrx::Internal;
use std::collections::HashSet;

const DOG_COMPOSITE_TYPE: &str = "Dog";

struct SumScritches {}

#[pg_aggregate]
impl Aggregate for SumScritches {
    type State = i32;
    const INITIAL_CONDITION: Option<&'static str> = Some("0");
    type Args = pgrx::name!(value, pgrx::composite_type!('static, "Dog"));

    fn state(
        current: Self::State,
        arg: Self::Args,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::State {
        let arg_scritches: i32 = arg
            .get_by_name("scritches")
            .unwrap() // Unwrap the result of the conversion
            .unwrap_or_default(); // The number of scritches, or 0 if there was none set
        current + arg_scritches
    }
}

/*
Create sum the scritches received by dogs, roughly the equivalent of:

```sql
CREATE FUNCTION scritch_collector_state(state Dog, new integer)
    RETURNS Dog
    LANGUAGE SQL
    STRICT
    RETURN ROW(state.name, state.scritches + new)::Dog;

CREATE AGGREGATE scritch_collector ("value" integer) (
    SFUNC = "sum_scritches_state",
    STYPE = Dog,
)
```
*/
struct ScritchCollector;

#[pg_aggregate]
impl Aggregate for ScritchCollector {
    type State = Option<pgrx::composite_type!('static, "Dog")>;
    type Args = i32;

    fn state(
        current: Self::State,
        arg: Self::Args,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::State {
        let mut current = match current {
            Some(v) => v,
            None => PgHeapTuple::new_composite_type(DOG_COMPOSITE_TYPE).unwrap(),
        };
        let current_scritches: i32 = current.get_by_name("scritches").unwrap().unwrap_or_default();
        current.set_by_name("scritches", current_scritches + arg).unwrap();
        Some(current)
    }
}

#[derive(Copy, Clone, Default, Debug)]
pub struct DemoUnique;

#[pg_aggregate]
impl Aggregate for DemoUnique {
    type Args = &'static str;
    type State = Internal;
    type Finalize = i32;

    fn state(
        mut current: Self::State,
        arg: Self::Args,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::State {
        let inner = unsafe { current.get_or_insert_default::<HashSet<String>>() };

        inner.insert(arg.to_string());
        current
    }

    fn combine(
        mut first: Self::State,
        mut second: Self::State,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::State {
        let first_inner = unsafe { first.get_or_insert_default::<HashSet<String>>() };
        let second_inner = unsafe { second.get_or_insert_default::<HashSet<String>>() };

        let unioned: HashSet<_> = first_inner.union(second_inner).collect();
        Internal::new(unioned)
    }

    fn finalize(
        mut current: Self::State,
        _direct_args: Self::OrderedSetArgs,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::Finalize {
        let inner = unsafe { current.get_or_insert_default::<HashSet<String>>() };

        inner.len() as i32
    }
}

#[derive(Copy, Clone, Default, Debug)]
pub struct AggregateWithOrderedSetArgs;

#[pg_aggregate]
impl Aggregate for AggregateWithOrderedSetArgs {
    type Args = name!(input, pgrx::composite_type!('static, "Dog"));
    type State = pgrx::composite_type!('static, "Dog");
    type Finalize = pgrx::composite_type!('static, "Dog");
    const ORDERED_SET: bool = true;
    type OrderedSetArgs = name!(percentile, pgrx::composite_type!('static, "Dog"));

    fn state(
        _current: Self::State,
        _arg: Self::Args,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::State {
        unimplemented!("Just a SQL generation test")
    }

    fn finalize(
        _current: Self::State,
        _direct_arg: Self::OrderedSetArgs,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::Finalize {
        unimplemented!("Just a SQL generation test")
    }
}

#[derive(Copy, Clone, Default, Debug)]
pub struct AggregateWithMovingState;

#[pg_aggregate]
impl Aggregate for AggregateWithMovingState {
    type Args = pgrx::composite_type!('static, "Dog");
    type State = pgrx::composite_type!('static, "Dog");
    type MovingState = pgrx::composite_type!('static, "Dog");

    fn state(
        _current: Self::State,
        _arg: Self::Args,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::State {
        unimplemented!("Just a SQL generation test")
    }

    fn moving_state(
        _current: Self::State,
        _arg: Self::Args,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::MovingState {
        unimplemented!("Just a SQL generation test")
    }

    fn moving_state_inverse(
        _current: Self::State,
        _arg: Self::Args,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::MovingState {
        unimplemented!("Just a SQL generation test")
    }

    fn combine(
        _first: Self::State,
        _second: Self::State,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::State {
        unimplemented!("Just a SQL generation test")
    }
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    #[allow(unused_imports)]
    use pgrx::prelude::*;

    #[pg_test]
    fn test_scritch_collector() {
        let retval = Spi::get_one::<i32>(
            "SELECT (scritch_collector(value)).scritches FROM UNNEST(ARRAY [1,2,3]) as value;",
        );
        assert_eq!(retval, Ok(Some(6)));
    }

    #[pg_test]
    fn aggregate_demo_sum() {
        let retval =
            Spi::get_one::<i32>("SELECT demo_sum(value) FROM UNNEST(ARRAY [1, 1, 2]) as value;");
        assert_eq!(retval, Ok(Some(4)));

        // Moving-aggregate mode
        let retval = Spi::get_one::<Vec<i32>>(
            "
            SELECT array_agg(calculated) FROM (
                SELECT demo_sum(value) OVER (
                    ROWS BETWEEN 1 PRECEDING AND CURRENT ROW
                ) as calculated FROM UNNEST(ARRAY [1, 20, 300, 4000]) as value
            ) as results;
        ",
        );
        assert_eq!(retval, Ok(Some(vec![1, 21, 320, 4300])));
    }

    #[pg_test]
    fn aggregate_demo_unique() {
        let retval = Spi::get_one::<i32>(
            "SELECT DemoUnique(value) FROM UNNEST(ARRAY ['a', 'a', 'b']) as value;",
        );
        assert_eq!(retval, Ok(Some(2)));
    }
}
fn main() {}
