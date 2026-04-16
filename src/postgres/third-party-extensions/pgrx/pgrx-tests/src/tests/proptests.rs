use crate::proptest::PgTestRunner;
use core::ffi;
use paste::paste;
use pgrx::prelude::*;
use proptest::prelude::*;
use TimeWithTimeZone as TimeTz;

/// Generate the roundtrip property tests for a datetime type
///
/// This macro relies on the fact Postgres has a very regular naming format
/// for serialization/deserialization functions for most types, always using
/// - `type_in`
/// - `type_out`
macro_rules! pg_proptest_datetime_roundtrip_tests {
    ($datetime_ty:ty, $nop_fn:ident, $prop_strat:expr) => {

paste! {
// A property test consists of
// 1. Posing a hypothesis
#[doc = concat!("A value of ", stringify!($datetime_ty), "should be able to be passed to Postgres and back.")]
#[pg_test]
pub fn [<$datetime_ty:lower _spi_roundtrip>] () {
    // 2. Constructing the Postgres-adapted test runner
    let mut proptest = PgTestRunner::default();
    // 3. A strategy to create and refine values, which is a somewhat aggrandized function.
    //    In some cases it actually can be replaced directly by a closure, or, in this case,
    //    it involves using a closure to `prop_map` an existing Strategy for producing
    //    "any kind of i32" into "any kind of in-range value for a Date".
    let strat = $prop_strat;
    // 4. The runner invocation
    proptest
        .run(&strat, |datetime| {
            let query = concat!("SELECT ", stringify!($nop_fn), "($1)");
            let args = &[datetime.into()];
            let spi_ret: $datetime_ty = Spi::get_one_with_args(query, args).unwrap().unwrap();
            // 5. A condition on which the test is accepted or rejected:
            //    this is easily done via `prop_assert!` and its friends,
            //    which just early-returns a TestCaseError on failure
            prop_assert_eq!(datetime, spi_ret);
            Ok(())
        })
        .unwrap();
}


#[doc = concat!("A value of ", stringify!($datetime_ty), "should be able to be serialized to text, passed to Postgres, and recovered.")]
#[pg_test]
pub fn [<$datetime_ty:lower _literal_spi_roundtrip>] () {
    let mut proptest = PgTestRunner::default();
    let strat = $prop_strat;
    proptest
        .run(&strat, |datetime| {
            let datum = datetime.into_datum();
            let datetime_cstr: &ffi::CStr =
                unsafe { pgrx::direct_function_call(pg_sys:: [<$datetime_ty:lower _out>] , &[datum]).unwrap() };
            let datetime_text = datetime_cstr.to_str().unwrap().to_owned();
            let spi_select_command = format!(concat!("SELECT ", stringify!($nop_fn), "('{}')"), datetime_text);
            let spi_ret: Option<$datetime_ty> = Spi::get_one(&spi_select_command).unwrap();
            prop_assert_eq!(datetime, spi_ret.unwrap());
            Ok(())
        })
        .unwrap();
}

}

    }
}

macro_rules! pg_proptest_datetime_types {
    ($($datetime_ty:ty = $prop_strat:expr;)*) => {

        paste! {
    $(

#[pg_extern]
pub fn [<nop_ $datetime_ty:lower>](datetime: $datetime_ty) -> $datetime_ty {
    datetime
}

    )*

#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    use super::*;
    #[allow(unused)] // I can never tell when this is actually needed.
    use crate as pgrx_tests;

    $(
    pg_proptest_datetime_roundtrip_tests! {
            $datetime_ty, [<nop_ $datetime_ty:lower>], $prop_strat
    }
    )*
}

        }

    }
}

pg_proptest_datetime_types! {
    Date = prop::num::i32::ANY.prop_map(Date::saturating_from_raw);
    Time = prop::num::i64::ANY.prop_map(Time::modular_from_raw);
    Timestamp = prop::num::i64::ANY.prop_map(Timestamp::saturating_from_raw);
    // TimestampTz = prop::num::i64::ANY.prop_map(TimestampTz::from); // This doesn't exist, and that's a good thing.
    TimeTz = (prop::num::i64::ANY, prop::num::i32::ANY).prop_map(TimeTz::modular_from_raw);
}
