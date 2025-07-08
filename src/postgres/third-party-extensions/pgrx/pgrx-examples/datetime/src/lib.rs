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
use rand::Rng;

::pgrx::pg_module_magic!();

#[pg_extern(name = "to_iso_string", immutable, parallel_safe)]
fn to_iso_string(tsz: TimestampWithTimeZone) -> String {
    tsz.to_iso_string()
}

/// ```sql
/// datetime=# select
///        to_iso_string(now())        as my_timezone,
///        to_iso_string(now(), 'PDT') as "PDT",
///        to_iso_string(now(), 'MDT') as "MDT",
///        to_iso_string(now(), 'EDT') as "EDT",
///        to_iso_string(now(), 'UTC') as "UTC";
/// -[ RECORD 1 ]---------------------------------
/// my_timezone | 2023-05-19T12:38:06.343021-04:00
/// PDT         | 2023-05-19T09:38:06.343021-07:00
/// MDT         | 2023-05-19T10:38:06.343021-06:00
/// EDT         | 2023-05-19T12:38:06.343021-04:00
/// UTC         | 2023-05-19T16:38:06.343021+00:00
/// ```
#[pg_extern(name = "to_iso_string", immutable, parallel_safe)]
fn to_iso_string_at_timezone(
    tsz: TimestampWithTimeZone,
    tz: String,
) -> Result<String, DateTimeConversionError> {
    tsz.to_iso_string_with_timezone(tz)
}

#[pg_extern(name = "subtract_interval", immutable, parallel_safe)]
fn subtract_interval_time(v: Time, i: Interval) -> Time {
    v - i
}

#[pg_extern(name = "subtract_interval", immutable, parallel_safe)]
fn subtract_interval_tz(v: TimeWithTimeZone, i: Interval) -> TimeWithTimeZone {
    v - i
}

#[pg_extern(name = "subtract_interval", immutable, parallel_safe)]
fn subtract_interval_tstz(v: TimestampWithTimeZone, i: Interval) -> TimestampWithTimeZone {
    v - i
}

#[pg_extern(name = "subtract_interval", immutable, parallel_safe)]
fn subtract_interval_ts(v: Timestamp, i: Interval) -> Timestamp {
    v - i
}

#[pg_extern(name = "subtract_interval", immutable, parallel_safe)]
fn subtract_interval_date(v: Date, i: Interval) -> Timestamp {
    v - i
}

#[pg_extern(name = "subtract_interval", immutable, parallel_safe)]
fn subtract_interval_interval(v: Interval, i: Interval) -> Interval {
    v - i
}

#[pg_extern(name = "add_interval", immutable, parallel_safe)]
fn add_interval_date(v: Date, i: Interval) -> Timestamp {
    v + i
}

#[pg_extern(name = "add_interval", immutable, parallel_safe)]
fn add_interval_time(v: Time, i: Interval) -> Time {
    v + i
}

#[pg_extern(name = "add_interval", immutable, parallel_safe)]
fn add_interval_tz(v: TimeWithTimeZone, i: Interval) -> TimeWithTimeZone {
    v + i
}

#[pg_extern(name = "add_interval", immutable, parallel_safe)]
fn add_interval_tstz(v: TimestampWithTimeZone, i: Interval) -> TimestampWithTimeZone {
    v + i
}

#[pg_extern(name = "add_interval", immutable, parallel_safe)]
fn add_interval_ts(v: Timestamp, i: Interval) -> Timestamp {
    v + i
}

#[pg_extern(name = "add_interval", immutable, parallel_safe)]
fn add_interval_interval(v: Interval, i: Interval) -> Interval {
    v + i
}

#[pg_extern(immutable, parallel_safe)]
fn mul_interval(i: Interval, v: f64) -> Interval {
    i * v
}

#[pg_extern(immutable, parallel_safe)]
fn div_interval(i: Interval, v: f64) -> Interval {
    i / v
}

#[pg_extern(parallel_safe)]
fn random_time() -> Result<Time, DateTimeConversionError> {
    Time::new(
        rand::thread_rng().gen_range(0..23),
        rand::thread_rng().gen_range(0..59),
        rand::thread_rng().gen_range(0..59) as f64,
    )
}

#[pg_extern(parallel_safe)]
fn random_date() -> Date {
    let year = rand::thread_rng().gen_range(1978..2023);
    let month = rand::thread_rng().gen_range(1..12);

    loop {
        let day = rand::thread_rng().gen_range(0..31);
        match Date::new(year, month, day) {
            // everything is good
            Ok(date) => return date,

            // the random day could be greater than the random month has, so we'd get a FieldOverflow error
            Err(DateTimeConversionError::FieldOverflow) => continue,

            // Something really unexpected happened
            Err(e) => panic!("Error creating random date: {}", e),
        }
    }
}

/// ```sql
/// datetime=# select compose_timestamp(random_date(), random_time()) from generate_series(1, 10);
///   compose_timestamp  
/// ---------------------
///  2001-06-21 19:27:04
///  1994-11-03 06:11:01
///  2012-08-08 14:39:04
///  2020-02-18 08:29:03
///  2012-04-06 06:41:02
///  2022-02-03 22:29:00
///  2003-04-08 16:27:03
///  1986-01-11 03:25:02
///  2003-02-01 03:06:02
///  2012-08-09 00:03:03
/// (10 rows)
/// ```
#[pg_extern(immutable, parallel_safe)]
fn compose_timestamp(d: Date, t: Time) -> Timestamp {
    (d, t).into()
}

#[pg_extern(immutable, parallel_safe)]
fn set_timezone(ts: Timestamp, timezone: String) -> Result<Timestamp, DateTimeConversionError> {
    let tsz: TimestampWithTimeZone = ts.into();
    tsz.at_timezone(timezone)
}

/// ```sql
/// datetime=# begin; select pg_sleep(10); select * from all_times();
/// BEGIN
/// Time: 0.261 ms
/// -[ RECORD 1 ]
/// pg_sleep |
///
/// Time: 10010.623 ms (00:10.011)
/// -[ RECORD 1 ]---------+------------------------------
/// now                   | 2023-05-19 12:45:51.032427-04
/// transaction_timestamp | 2023-05-19 12:45:51.032427-04
/// statement_timestamp   | 2023-05-19 12:46:01.047298-04
/// clock_timestamp       | 2023-05-19 12:46:01.047415-04
/// ```
#[pg_extern]
fn all_times() -> TableIterator<
    'static,
    (
        name!(now, TimestampWithTimeZone),
        name!(transaction_timestamp, TimestampWithTimeZone),
        name!(statement_timestamp, TimestampWithTimeZone),
        name!(clock_timestamp, TimestampWithTimeZone),
    ),
> {
    TableIterator::once((now(), transaction_timestamp(), statement_timestamp(), clock_timestamp()))
}
