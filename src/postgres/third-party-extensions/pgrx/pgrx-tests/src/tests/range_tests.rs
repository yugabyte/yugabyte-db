//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use pgrx::prelude::*;

#[pg_extern]
fn accept_range_i32(range: Range<i32>) -> Range<i32> {
    range
}

#[pg_extern]
fn accept_range_i64(range: Range<i64>) -> Range<i64> {
    range
}

#[pg_extern]
fn accept_range_numeric(range: Range<AnyNumeric>) -> Range<AnyNumeric> {
    range
}

#[pg_extern]
fn accept_range_date(range: Range<Date>) -> Range<Date> {
    range
}

#[pg_extern]
fn accept_range_date_array(arr: Array<Range<Date>>) -> Vec<Range<Date>> {
    arr.iter_deny_null().collect()
}

#[pg_extern]
fn accept_range_ts(range: Range<Timestamp>) -> Range<Timestamp> {
    range
}

#[pg_extern]
fn accept_range_tstz(range: Range<TimestampWithTimeZone>) -> Range<TimestampWithTimeZone> {
    range
}

fn range_round_trip_values<T>(range: Range<T>) -> Range<T>
where
    T: FromDatum + IntoDatum + RangeSubType,
{
    range
}

fn range_round_trip_bounds<T>(range: Range<T>) -> Range<T>
where
    T: FromDatum + IntoDatum + RangeSubType,
{
    match range.as_ref() {
        None => Range::empty(),
        Some((l, u)) => Range::new(l.clone(), u.clone()),
    }
}

#[pg_extern]
fn range_i32_rt_values(range: Range<i32>) -> Range<i32> {
    range_round_trip_values(range)
}

#[pg_extern]
fn range_i32_rt_bounds(range: Range<i32>) -> Range<i32> {
    range_round_trip_bounds(range)
}

#[pg_extern]
fn range_i64_rt_values(range: Range<i64>) -> Range<i64> {
    range_round_trip_values(range)
}

#[pg_extern]
fn range_i64_rt_bounds(range: Range<i64>) -> Range<i64> {
    range_round_trip_bounds(range)
}

#[pg_extern]
fn range_num_rt_values(range: Range<AnyNumeric>) -> Range<AnyNumeric> {
    range_round_trip_values(range)
}

#[pg_extern]
fn range_num_rt_bounds(range: Range<AnyNumeric>) -> Range<AnyNumeric> {
    range_round_trip_bounds(range)
}

#[pg_extern]
fn range_date_rt_values(range: Range<Date>) -> Range<Date> {
    range_round_trip_values(range)
}

#[pg_extern]
fn range_date_rt_bounds(range: Range<Date>) -> Range<Date> {
    range_round_trip_bounds(range)
}

#[pg_extern]
fn range_ts_rt_values(range: Range<Timestamp>) -> Range<Timestamp> {
    range_round_trip_values(range)
}

#[pg_extern]
fn range_ts_rt_bounds(range: Range<Timestamp>) -> Range<Timestamp> {
    range_round_trip_bounds(range)
}

#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;

    use pgrx::prelude::*;

    #[pg_test]
    fn test_accept_range_i32() {
        let matched =
            Spi::get_one::<bool>("SELECT accept_range_i32(int4range'[1,10)') = int4range'[1,10)'");
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_accept_range_i64() {
        let matched =
            Spi::get_one::<bool>("SELECT accept_range_i64(int8range'[1,10)') = int8range'[1,10)'");
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_accept_range_numeric() {
        let matched = Spi::get_one::<bool>(
            "SELECT accept_range_numeric(numrange'[1.0,10.0)') = numrange'[1.0,10.0)'",
        );
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_empty_anynumeric_range() {
        let matched = Spi::get_one::<Range<AnyNumeric>>(
            "SELECT accept_range_numeric('[10.5, 10.5)'::numrange)",
        );
        assert_eq!(matched, Ok(Some(Range::empty())));
    }

    #[pg_test]
    fn test_accept_range_date() {
        let matched =
            Spi::get_one::<bool>("SELECT accept_range_date(daterange'[2000-01-01,2022-01-01)') = daterange'[2000-01-01,2022-01-01)'");
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_accept_range_date_array() {
        let matched =
            Spi::get_one::<bool>("SELECT accept_range_date_array(ARRAY[daterange'[2000-01-01,2022-01-01)']::daterange[]) = ARRAY[daterange'[2000-01-01,2022-01-01)']::daterange[]");
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_accept_range_ts() {
        let matched =
            Spi::get_one::<bool>("SELECT accept_range_ts(tsrange'[2000-01-01T:12:34:56,2022-01-01T:12:34:56)') = tsrange'[2000-01-01T:12:34:56,2022-01-01T:12:34:56)'");
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_accept_range_tstz() {
        let matched =
            Spi::get_one::<bool>("SELECT accept_range_tstz(tstzrange'[2000-01-01T:12:34:56+00,2022-01-01T:12:34:56+00)') = tstzrange'[2000-01-01T:12:34:56+00,2022-01-01T:12:34:56+00)'");
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_i32_rt_values() {
        let matched = Spi::get_one::<bool>(
            "SELECT range_i32_rt_values(int4range'[1,10)') = int4range'[1,10)'",
        );
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_i32_rt_bounds() {
        let matched = Spi::get_one::<bool>(
            "SELECT range_i32_rt_bounds(int4range'[1,10)') = int4range'[1,10)'",
        );
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_i64_rt_values() {
        let matched = Spi::get_one::<bool>(
            "SELECT range_i64_rt_values(int8range'[1,10)') = int8range'[1,10)'",
        );
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_i64_rt_bounds() {
        let matched = Spi::get_one::<bool>(
            "SELECT range_i64_rt_bounds(int8range'[1,10)') = int8range'[1,10)'",
        );
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_num_rt_values() {
        let matched = Spi::get_one::<bool>(
            "SELECT range_num_rt_values(numrange'[1.0,10.0)') = numrange'[1.0,10.0)'",
        );
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_num_rt_bounds() {
        let matched = Spi::get_one::<bool>(
            "SELECT range_num_rt_bounds(numrange'[1.0,10.0)') = numrange'[1.0,10.0)'",
        );
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_date_rt_values() {
        let matched = Spi::get_one::<bool>(
            "SELECT range_date_rt_values(daterange'[2000-01-01,2022-01-01)') = daterange'[2000-01-01,2022-01-01)'",
        );
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_date_rt_bounds() {
        let matched =
            Spi::get_one::<bool>("SELECT range_date_rt_bounds(daterange'[2000-01-01,2022-01-01)') = daterange'[2000-01-01,2022-01-01)'");
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_ts_rt_values() {
        let matched = Spi::get_one::<bool>(
            "SELECT range_ts_rt_values(tsrange'[2000-01-01T12:34:56,2022-01-01T12:34:56)') = tsrange'[2000-01-01T12:34:56,2022-01-01T12:34:56)'",
        );
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_ts_rt_bounds() {
        let matched =
            Spi::get_one::<bool>("SELECT range_ts_rt_bounds(tsrange'[2000-01-01T12:34:56,2022-01-01T12:34:56)') = tsrange'[2000-01-01T12:34:56,2022-01-01T12:34:56)'");
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_date_rt_values_empty() {
        let matched = Spi::get_one::<bool>(
            "SELECT range_date_rt_values(daterange'[2000-01-01,2000-01-01)') = daterange'empty'",
        );
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_date_rt_bounds_empty() {
        let matched = Spi::get_one::<bool>(
            "SELECT range_date_rt_bounds(daterange'[2000-01-01,2000-01-01)') = daterange'empty'",
        );
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_date_rt_values_neg_inf() {
        let matched = Spi::get_one::<bool>(
            "SELECT range_date_rt_values(daterange'(-infinity,2000-01-01)') = daterange'(-infinity,2000-01-01)'",
        );
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_date_rt_bounds_neg_inf() {
        let matched = Spi::get_one::<bool>(
            "SELECT range_date_rt_bounds(daterange'(-infinity,2000-01-01)') = daterange'(-infinity,2000-01-01)'",
        );
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_date_rt_values_inf() {
        let matched = Spi::get_one::<bool>(
            "SELECT range_date_rt_values(daterange'(2000-01-01,infinity)') = daterange'(2000-01-01,infinity)'",
        );
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_date_rt_bounds_inf() {
        let matched = Spi::get_one::<bool>(
            "SELECT range_date_rt_bounds(daterange'(2000-01-01,infinity)') = daterange'(2000-01-01,infinity)'",
        );
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_date_rt_values_neg_inf_inf() {
        let matched = Spi::get_one::<bool>(
            "SELECT range_date_rt_values(daterange'(-infinity,infinity)') = daterange'(-infinity,infinity)'",
        );
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_date_rt_bounds_neg_inf_inf() {
        let matched = Spi::get_one::<bool>(
            "SELECT range_date_rt_bounds(daterange'(-infinity,infinity)') = daterange'(-infinity,infinity)'",
        );
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_date_rt_values_neg_inf_val() {
        let matched = Spi::get_one::<bool>(
            "SELECT range_date_rt_values(daterange'(,2000-01-01)') = daterange'(,2000-01-01)'",
        );
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_date_rt_bounds_neg_inf_val() {
        let matched = Spi::get_one::<bool>(
            "SELECT range_date_rt_bounds(daterange'(,2000-01-01)') = daterange'(,2000-01-01)'",
        );
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_date_rt_values_val_inf() {
        let matched = Spi::get_one::<bool>(
            "SELECT range_date_rt_values(daterange'(2000-01-01,)') = daterange'(2000-01-01,)'",
        );
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_date_rt_bounds_val_inf() {
        let matched = Spi::get_one::<bool>(
            "SELECT range_date_rt_bounds(daterange'(2000-01-01,)') = daterange'(2000-01-01,)'",
        );
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_date_rt_values_full() {
        let matched =
            Spi::get_one::<bool>("SELECT range_date_rt_values(daterange'(,)') = daterange'(,)'");
        assert_eq!(matched, Ok(Some(true)));
    }

    #[pg_test]
    fn test_range_date_rt_bounds_full() {
        let matched =
            Spi::get_one::<bool>("SELECT range_date_rt_bounds(daterange'(,)') = daterange'(,)'");
        assert_eq!(matched, Ok(Some(true)));
    }
}
