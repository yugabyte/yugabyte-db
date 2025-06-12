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
fn accept_date(d: Date) -> Date {
    d
}

#[pg_extern]
fn accept_date_round_trip(d: Date) -> Date {
    d
}

#[pg_extern]
fn accept_time(t: Time) -> Time {
    t
}

#[pg_extern]
fn accept_time_with_time_zone(t: TimeWithTimeZone) -> TimeWithTimeZone {
    t
}

#[pg_extern]
fn convert_timetz_to_time(t: TimeWithTimeZone) -> Time {
    t.to_utc().into()
}

#[pg_extern]
fn accept_timestamp(t: Timestamp) -> Timestamp {
    t
}

#[pg_extern]
fn accept_timestamp_with_time_zone(t: TimestampWithTimeZone) -> TimestampWithTimeZone {
    t
}

#[pg_extern]
fn accept_timestamp_with_time_zone_offset_round_trip(
    t: TimestampWithTimeZone,
) -> TimestampWithTimeZone {
    t
}

#[pg_extern]
fn accept_timestamp_with_time_zone_datetime_round_trip(
    t: TimestampWithTimeZone,
) -> TimestampWithTimeZone {
    t
}

#[pg_extern]
fn return_3pm_mountain_time() -> TimestampWithTimeZone {
    TimestampWithTimeZone::with_timezone(2020, 2, 19, 15, 0, 0.0, "MST").unwrap()
}

#[pg_extern(sql = r#"
CREATE FUNCTION "timestamptz_to_i64"(
	"tstz" timestamptz
) RETURNS bigint
STRICT
LANGUAGE c /* Rust */
AS 'MODULE_PATHNAME', '@FUNCTION_NAME@';
"#)]
fn timestamptz_to_i64(tstz: pg_sys::TimestampTz) -> i64 {
    tstz
}

#[pg_extern]
fn accept_interval(interval: Interval) -> Interval {
    interval
}

#[pg_extern]
fn accept_interval_round_trip(interval: Interval) -> Interval {
    interval
}

#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;

    use pgrx::datum::datetime_support::{IntervalConversionError, USECS_PER_DAY};
    use pgrx::datum::{get_timezone_offset, DateTimeConversionError};
    use pgrx::prelude::*;
    use serde_json::*;
    use std::result::Result;
    use std::str::FromStr;
    use std::time::Duration;

    #[pg_test]
    fn test_to_pg_epoch_days() {
        let date = Date::new(2000, 1, 2).unwrap();

        assert_eq!(date.to_pg_epoch_days(), 1);
    }

    #[pg_test]
    fn test_to_posix_time() {
        let date = Date::new(1970, 1, 2).unwrap();

        assert_eq!(date.to_posix_time(), 86400);
    }

    #[pg_test]
    fn test_to_julian_days() {
        let date = Date::new(2000, 1, 1).unwrap();

        assert_eq!(date.to_julian_days(), pg_sys::POSTGRES_EPOCH_JDATE as i32);
    }

    #[pg_test]
    #[allow(deprecated)]
    fn test_time_with_timezone_serialization() {
        let time_with_timezone = TimeWithTimeZone::with_timezone(12, 23, 34.0, "CEST").unwrap();
        let json = json!({ "time W/ Zone test": time_with_timezone });

        let (h, ..) = time_with_timezone.at_timezone("UTC").unwrap().to_hms_micro();
        assert_eq!(10, h);

        // however Postgres wants to format it is fine by us
        assert_eq!(json!({"time W/ Zone test":"12:23:34+02:00"}), json);
    }

    #[pg_test]
    fn test_timetz_from_time_and_zone() {
        let timeadt: pg_sys::TimeADT = USECS_PER_DAY / 2;
        let zone = 0;
        let timetz = TimeWithTimeZone::try_from((timeadt, zone)).unwrap();

        let expected_timetz = TimeWithTimeZone::with_timezone(12, 0, 0.0, "UTC").unwrap();

        assert_eq!(timetz.hour(), expected_timetz.hour());
        assert_eq!(timetz.minute(), expected_timetz.minute());
        assert_eq!(timetz.second(), expected_timetz.second());
        assert_eq!(timetz.timezone_offset(), expected_timetz.timezone_offset());
    }

    #[pg_test]
    fn test_date_serialization() {
        let date: Date = Date::new(2020, 4, 7).unwrap();
        let json = json!({ "date test": date });

        assert_eq!(json!({"date test":"2020-04-07"}), json);
    }

    #[pg_test]
    #[allow(deprecated)]
    fn test_time_serialization() {
        let time = Time::ALLBALLS;
        let json = json!({ "time test": time });

        assert_eq!(json!({"time test":"00:00:00"}), json);
    }

    #[pg_test]
    fn test_accept_date_now() {
        let result = Spi::get_one::<bool>("SELECT accept_date(now()::date) = now()::date;");
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_accept_date_yesterday() {
        let result =
            Spi::get_one::<bool>("SELECT accept_date('yesterday'::date) = 'yesterday'::date;");
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_accept_date_tomorrow() {
        let result =
            Spi::get_one::<bool>("SELECT accept_date('tomorrow'::date) = 'tomorrow'::date;");
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_accept_date_neg_infinity() {
        let result =
            Spi::get_one::<bool>("SELECT accept_date('-infinity'::date) = '-infinity'::date;");
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_accept_date_infinity() {
        let result =
            Spi::get_one::<bool>("SELECT accept_date('infinity'::date) = 'infinity'::date;");
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_accept_date_large_date() {
        let result =
            Spi::get_one::<bool>("SELECT accept_date('10001-01-01'::date) = '10001-01-01'::date;");
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_accept_date_random() {
        let result =
            Spi::get_one::<bool>("SELECT accept_date('1823-03-28'::date) = '1823-03-28'::date;");
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_accept_date_round_trip_large_date() {
        let result = Spi::get_one::<bool>(
            "SELECT accept_date_round_trip('10001-01-01'::date) = '10001-01-01'::date;",
        );
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_accept_date_round_trip_random() {
        let result = Spi::get_one::<bool>(
            "SELECT accept_date_round_trip('1823-03-28'::date) = '1823-03-28'::date;",
        );
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_accept_time_now() {
        let result = Spi::get_one::<bool>("SELECT accept_time(now()::time) = now()::time;");
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_convert_time_with_time_zone_now() {
        // This test used to simply compare for equality in Postgres, assert on the bool
        // however, failed `=` in Postgres doesn't say much if it fails.
        // Thus this esoteric formulation: it derives a delta if there is one.
        let result = Spi::get_one::<Time>(
            "SELECT (
                convert_timetz_to_time(now()::time with time zone at time zone 'America/Denver')
                - convert_timetz_to_time(now()::time with time zone at time zone 'utc')
                + 'allballs'::time
            );",
        );

        assert_eq!(result, Ok(Some(Time::ALLBALLS)));
    }

    #[pg_test]
    fn test_accept_time_yesterday() {
        let result = Spi::get_one::<bool>(
            "SELECT accept_time('yesterday'::timestamp::time) = 'yesterday'::timestamp::time;",
        );
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_accept_time_tomorrow() {
        let result = Spi::get_one::<bool>(
            "SELECT accept_time('tomorrow'::timestamp::time) = 'tomorrow'::timestamp::time;",
        );
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_accept_time_random() {
        let result = Spi::get_one::<bool>(
            "SELECT accept_time('1823-03-28 7:54:03 am'::time) = '1823-03-28 7:54:03 am'::time;",
        );
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_accept_timestamp() {
        let result =
            Spi::get_one::<bool>("SELECT accept_timestamp(now()::timestamp) = now()::timestamp;");
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_accept_timestamp_with_time_zone() {
        let result = Spi::get_one::<bool>("SELECT accept_timestamp_with_time_zone(now()) = now();");
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_accept_timestamp_with_time_zone_not_utc() {
        let result = Spi::get_one::<bool>("SELECT accept_timestamp_with_time_zone('1990-01-23 03:45:00-07') = '1990-01-23 03:45:00-07'::timestamp with time zone;");
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_return_3pm_mountain_time() -> Result<(), pgrx::spi::Error> {
        let result = Spi::get_one::<TimestampWithTimeZone>(
            "SET timezone TO 'UTC'; SELECT return_3pm_mountain_time();",
        )?
        .expect("datum was null");

        assert_eq!(22, result.hour());
        Ok(())
    }

    #[pg_test]
    fn test_is_timestamp_with_time_zone_utc() -> Result<(), pgrx::spi::Error> {
        let ts = Spi::get_one::<TimestampWithTimeZone>(
            "SELECT '2020-02-18 14:08 -07'::timestamp with time zone",
        )?
        .expect("datum was null");

        assert_eq!(ts.to_utc().hour(), 21);
        Ok(())
    }

    #[pg_test]
    fn test_is_timestamp_utc() -> Result<(), pgrx::spi::Error> {
        let ts = Spi::get_one::<Timestamp>("SELECT '2020-02-18 14:08'::timestamp")?
            .expect("datum was null");
        assert_eq!(ts.hour(), 14);
        Ok(())
    }

    #[pg_test]
    fn test_timestamptz() {
        let result = Spi::get_one::<i64>(
            "SELECT timestamptz_to_i64('2000-01-01 00:01:00.0000000+00'::timestamptz)",
        );
        assert_eq!(result, Ok(Some(Duration::from_secs(60).as_micros() as i64)));
    }

    #[pg_test]
    fn test_accept_timestamp_with_time_zone_offset_round_trip() {
        let result = Spi::get_one::<bool>(
            "SELECT accept_timestamp_with_time_zone_offset_round_trip(now()) = now()",
        );
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_accept_timestamp_with_time_zone_datetime_round_trip() {
        let result = Spi::get_one::<bool>(
            "SELECT accept_timestamp_with_time_zone_datetime_round_trip(now()) = now()",
        );
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_timestamp_with_timezone_serialization() {
        let time_stamp_with_timezone =
            TimestampWithTimeZone::with_timezone(2022, 2, 2, 16, 57, 11.0, "CEST").unwrap();

        // prevents PG's timestamp serialization from imposing the local servers time zone
        Spi::run("SET TIME ZONE 'UTC'").expect("SPI failed");
        let json = json!({ "time stamp with timezone test": time_stamp_with_timezone });

        // but we serialize timestamps at UTC
        assert_eq!(json!({"time stamp with timezone test":"2022-02-02T14:57:11+00:00"}), json);
    }

    #[pg_test]
    fn test_timestamp_serialization() {
        // prevents PG's timestamp serialization from imposing the local servers time zone
        Spi::run("SET TIME ZONE 'UTC'").expect("SPI failed");

        let ts = Timestamp::new(2020, 1, 1, 12, 34, 54.0).unwrap();
        let json = json!({ "time stamp test": ts });

        assert_eq!(json!({"time stamp test":"2020-01-01T12:34:54"}), json);
    }

    #[pg_test]
    fn test_timestamp_with_timezone_infinity() -> Result<(), pgrx::spi::Error> {
        let result =
            Spi::get_one::<bool>("SELECT accept_timestamp_with_time_zone('-infinity') = TIMESTAMP WITH TIME ZONE '-infinity';");
        assert_eq!(result, Ok(Some(true)));

        let result =
            Spi::get_one::<bool>("SELECT accept_timestamp_with_time_zone('infinity') = TIMESTAMP WITH TIME ZONE 'infinity';");
        assert_eq!(result, Ok(Some(true)));

        let tstz =
            Spi::get_one::<TimestampWithTimeZone>("SELECT TIMESTAMP WITH TIME ZONE'infinity'")?
                .expect("datum was null");
        assert!(tstz.is_infinity());

        let tstz =
            Spi::get_one::<TimestampWithTimeZone>("SELECT TIMESTAMP WITH TIME ZONE'-infinity'")?
                .expect("datum was null");
        assert!(tstz.is_neg_infinity());
        Ok(())
    }

    #[pg_test]
    fn test_timestamp_infinity() -> Result<(), pgrx::spi::Error> {
        let result =
            Spi::get_one::<bool>("SELECT accept_timestamp('-infinity') = '-infinity'::timestamp;")?
                .expect("datum was null");
        assert!(result);

        let result =
            Spi::get_one::<bool>("SELECT accept_timestamp('infinity') = 'infinity'::timestamp;")?
                .expect("datum was null");
        assert!(result);

        let ts =
            Spi::get_one::<Timestamp>("SELECT 'infinity'::timestamp")?.expect("datum was null");
        assert!(ts.is_infinity());

        let ts =
            Spi::get_one::<Timestamp>("SELECT '-infinity'::timestamp")?.expect("datum was null");
        assert!(ts.is_neg_infinity());
        Ok(())
    }

    #[rustfmt::skip]
    #[pg_test]
    fn test_from_str() -> Result<(), Box<dyn std::error::Error>> {
        assert_eq!(Time::new(12, 0, 0.0)?, Time::from_str("12:00:00")?);
        assert_eq!(TimeWithTimeZone::with_timezone(12, 0, 0.0, "UTC")?, TimeWithTimeZone::from_str("12:00:00 UTC")?);
        assert_eq!(Date::new(2023, 5, 13)?, Date::from_str("2023-5-13")?);
        assert_eq!(Timestamp::new(2023, 5, 13, 4, 56, 42.0)?, Timestamp::from_str("2023-5-13 04:56:42")?);
        assert_eq!(TimestampWithTimeZone::new(2023, 5, 13, 4, 56, 42.0)?, TimestampWithTimeZone::from_str("2023-5-13 04:56:42")?);
        assert_eq!(Interval::from_months(1), Interval::from_str("1 month")?);
        Ok(())
    }

    #[pg_test]
    fn test_accept_interval_random() {
        let result = Spi::get_one::<bool>("SELECT accept_interval(interval'1 year 2 months 3 days 4 hours 5 minutes 6 seconds') = interval'1 year 2 months 3 days 4 hours 5 minutes 6 seconds';")
            .expect("failed to get SPI result");
        assert_eq!(result, Some(true));
    }

    #[pg_test]
    fn test_accept_interval_neg_random() {
        let result = Spi::get_one::<bool>("SELECT accept_interval(interval'1 year 2 months 3 days 4 hours 5 minutes 6 seconds ago') = interval'1 year 2 months 3 days 4 hours 5 minutes 6 seconds ago';")
            .expect("failed to get SPI result");
        assert_eq!(result, Some(true));
    }

    #[pg_test]
    fn test_accept_interval_round_trip_random() {
        let result = Spi::get_one::<bool>("SELECT accept_interval_round_trip(interval'1 year 2 months 3 days 4 hours 5 minutes 6 seconds') = interval'1 year 2 months 3 days 4 hours 5 minutes 6 seconds';")
            .expect("failed to get SPI result");
        assert_eq!(result, Some(true));
    }

    #[pg_test]
    fn test_accept_interval_round_trip_neg_random() {
        let result = Spi::get_one::<bool>("SELECT accept_interval_round_trip(interval'1 year 2 months 3 days 4 hours 5 minutes 6 seconds ago') = interval'1 year 2 months 3 days 4 hours 5 minutes 6 seconds ago';")
            .expect("failed to get SPI result");
        assert_eq!(result, Some(true));
    }

    #[pg_test]
    fn test_interval_serialization() {
        let interval = Interval::new(3, 4, 5_000_000).unwrap();
        let json = json!({ "interval test": interval });

        assert_eq!(json!({"interval test":"3 mons 4 days 00:00:05"}), json);
    }

    #[pg_test]
    fn test_duration_to_interval_err() {
        use pgrx::datum::datetime_support::IntervalConversionError;
        // normal limit of i32::MAX months
        let duration = Duration::from_secs(
            pg_sys::DAYS_PER_MONTH as u64 * i32::MAX as u64 * pg_sys::SECS_PER_DAY as u64,
        );

        let result = TryInto::<Interval>::try_into(duration);
        match result {
            Ok(_) => (),
            _ => panic!("failed duration -> interval conversion"),
        };

        // one month too many, expect error
        let duration = Duration::from_secs(
            pg_sys::DAYS_PER_MONTH as u64 * (i32::MAX as u64 + 1u64) * pg_sys::SECS_PER_DAY as u64,
        );

        let result = TryInto::<Interval>::try_into(duration);
        match result {
            Err(IntervalConversionError::DurationMonthsOutOfBounds) => (),
            _ => panic!("invalid duration -> interval conversion succeeded"),
        };
    }

    #[pg_test]
    fn test_timezone_offset_cest() {
        assert_eq!(Ok(7200), get_timezone_offset("CEST"))
    }

    #[pg_test]
    fn test_timezone_offset_us_eastern() {
        let offset = get_timezone_offset("US/Eastern");
        // EDT vs EST
        assert!(offset == Ok(-14400) || offset == Ok(-18000), "offset was: {offset:?}");
    }

    #[pg_test]
    fn test_timezone_offset_unknown() {
        assert_eq!(
            Err(DateTimeConversionError::UnknownTimezone(String::from("UNKNOWN TIMEZONE"))),
            get_timezone_offset("UNKNOWN TIMEZONE")
        )
    }

    #[pg_test]
    fn test_interval_to_duration_conversion() {
        let i = Interval::new(42, 6, 3).unwrap();
        let i_micros = i.as_micros();
        let d: Duration = i.try_into().unwrap();

        assert_eq!(i_micros as u128, d.as_micros())
    }

    #[pg_test]
    fn test_negative_interval_to_duration_conversion() {
        let i = Interval::new(-42, -6, -3).unwrap();
        let d: Result<Duration, _> = i.try_into();

        assert_eq!(d, Err(IntervalConversionError::NegativeInterval))
    }

    #[pg_test]
    fn test_duration_to_interval_conversion() {
        let i: Interval =
            Spi::get_one("select '3 months 5 days 22 seconds'::interval").unwrap().unwrap();

        assert_eq!(i.months(), 3);
        assert_eq!(i.days(), 5);
        assert_eq!(i.micros(), 22_000_000); // 22 seconds

        let d = Duration::from_secs(
            pg_sys::DAYS_PER_MONTH as u64 * 3u64 * pg_sys::SECS_PER_DAY as u64 // 3 months
                + 5u64 * pg_sys::SECS_PER_DAY as u64 // 5 days
                + 22u64, // 22 seconds more
        );
        let i: Interval = d.try_into().unwrap();
        assert_eq!(i.months(), 3);
        assert_eq!(i.days(), 5);
        assert_eq!(i.micros(), 22_000_000); // 22 seconds
    }

    #[pg_test]
    fn test_interval_from_seconds() {
        let i = Interval::from_seconds(32768.0);
        assert_eq!("09:06:08", &i.to_string());

        let i = Interval::from_str("32768 seconds");
        assert_eq!(i, Ok(Interval::from_seconds(32768.0)))
    }

    #[pg_test]
    fn test_interval_from_mismatched_signs() {
        let i = Interval::from(Some(1), Some(-2), None, None, None, None, None);
        assert_eq!(i, Err(IntervalConversionError::MismatchedSigns))
    }

    #[pg_test]
    fn test_add_date_time() -> Result<(), Box<dyn std::error::Error>> {
        let date = Date::new(1978, 5, 13)?;
        let time = Time::new(13, 33, 42.0)?;
        let ts = date + time;
        assert_eq!(&ts.to_string(), "1978-05-13 13:33:42");
        assert_eq!(ts, Timestamp::new(1978, 5, 13, 13, 33, 42.0)?);
        Ok(())
    }

    #[pg_test]
    fn test_add_time_interval() -> Result<(), Box<dyn std::error::Error>> {
        let time = Time::new(13, 33, 42.0)?;
        let i = Interval::from_seconds(27.0);
        let time = time + i;
        assert_eq!(time, Time::new(13, 34, 9.0)?);
        Ok(())
    }

    #[pg_test]
    fn test_add_intervals() -> Result<(), Box<dyn std::error::Error>> {
        let b = Interval::from_months(6);
        let c = Interval::from_days(15);
        let a = Interval::from_micros(42);
        let result = a + b + c;
        assert_eq!(result, Interval::new(6, 15, 42)?);
        Ok(())
    }

    #[pg_test]
    fn test_old_date() -> Result<(), Box<dyn std::error::Error>> {
        let array = Spi::get_one::<Array<Date>>("SELECT ARRAY['1977-07-04']::date[]")?.unwrap();
        let first = array.into_iter().next();
        assert_eq!(first, Some(Some(Date::from_str("1977-07-04")?)));
        Ok(())
    }
}
