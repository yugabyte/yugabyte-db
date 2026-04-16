//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::datum::{
    Date, Interval, IntoDatum, Time, TimeWithTimeZone, Timestamp, TimestampWithTimeZone,
};
use crate::{direct_function_call, pg_sys};
use std::ops::{Add, AddAssign, Div, DivAssign, Mul, MulAssign, Neg, Sub, SubAssign};

impl Sub<i32> for Date {
    type Output = Date;

    fn sub(self, rhs: i32) -> Self::Output {
        unsafe {
            direct_function_call(pg_sys::date_mii, &[self.into_datum(), rhs.into_datum()]).unwrap()
        }
    }
}

impl Add<i32> for Date {
    type Output = Date;

    fn add(self, rhs: i32) -> Self::Output {
        unsafe {
            direct_function_call(pg_sys::date_pli, &[self.into_datum(), rhs.into_datum()]).unwrap()
        }
    }
}

impl Add<Date> for i32 {
    type Output = Date;

    fn add(self, rhs: Date) -> Self::Output {
        rhs + self
    }
}

impl Div<f64> for Interval {
    type Output = Interval;

    fn div(self, rhs: f64) -> Self::Output {
        unsafe {
            direct_function_call(pg_sys::interval_div, &[self.as_datum(), rhs.into_datum()])
                .unwrap()
        }
    }
}

impl DivAssign<f64> for Interval {
    fn div_assign(&mut self, rhs: f64) {
        *self = *self / rhs
    }
}

impl Sub for Interval {
    type Output = Interval;

    fn sub(self, rhs: Self) -> Self::Output {
        unsafe {
            direct_function_call(pg_sys::interval_mi, &[self.as_datum(), rhs.as_datum()]).unwrap()
        }
    }
}

impl SubAssign for Interval {
    fn sub_assign(&mut self, rhs: Self) {
        *self = *self - rhs
    }
}

impl Mul<f64> for Interval {
    type Output = Interval;

    fn mul(self, rhs: f64) -> Self::Output {
        unsafe {
            direct_function_call(pg_sys::interval_mul, &[self.as_datum(), rhs.into_datum()])
                .unwrap()
        }
    }
}

impl Mul<Interval> for f64 {
    type Output = Interval;

    fn mul(self, rhs: Interval) -> Self::Output {
        rhs * self
    }
}

impl MulAssign<f64> for Interval {
    fn mul_assign(&mut self, rhs: f64) {
        *self = *self * rhs
    }
}

impl Add for Interval {
    type Output = Interval;

    fn add(self, rhs: Self) -> Self::Output {
        unsafe {
            direct_function_call(pg_sys::interval_pl, &[self.as_datum(), rhs.as_datum()]).unwrap()
        }
    }
}

impl AddAssign for Interval {
    fn add_assign(&mut self, rhs: Self) {
        *self = *self + rhs
    }
}

impl Neg for Interval {
    type Output = Interval;

    fn neg(self) -> Self::Output {
        unsafe { direct_function_call(pg_sys::interval_um, &[self.into_datum()]).unwrap() }
    }
}

impl Sub for Time {
    type Output = Interval;

    fn sub(self, rhs: Self) -> Self::Output {
        unsafe {
            direct_function_call(pg_sys::time_mi_time, &[self.into_datum(), rhs.into_datum()])
                .unwrap()
        }
    }
}

impl Sub<Interval> for Time {
    type Output = Time;

    fn sub(self, rhs: Interval) -> Self::Output {
        unsafe {
            direct_function_call(pg_sys::time_mi_interval, &[self.into_datum(), rhs.as_datum()])
                .unwrap()
        }
    }
}

impl Add<Interval> for Time {
    type Output = Time;

    fn add(self, rhs: Interval) -> Self::Output {
        unsafe {
            direct_function_call(pg_sys::time_pl_interval, &[self.into_datum(), rhs.as_datum()])
                .unwrap()
        }
    }
}

impl Add<Time> for Interval {
    type Output = Time;

    fn add(self, rhs: Time) -> Self::Output {
        rhs + self
    }
}

impl Sub for Timestamp {
    type Output = Interval;

    fn sub(self, rhs: Self) -> Self::Output {
        unsafe {
            direct_function_call(pg_sys::timestamp_mi, &[self.into_datum(), rhs.into_datum()])
                .unwrap()
        }
    }
}

impl Sub<Interval> for Date {
    type Output = Timestamp;

    fn sub(self, rhs: Interval) -> Self::Output {
        unsafe {
            direct_function_call(pg_sys::date_mi_interval, &[self.into_datum(), rhs.as_datum()])
                .unwrap()
        }
    }
}

impl Add<Interval> for Date {
    type Output = Timestamp;

    fn add(self, rhs: Interval) -> Self::Output {
        unsafe {
            direct_function_call(pg_sys::date_pl_interval, &[self.into_datum(), rhs.as_datum()])
                .unwrap()
        }
    }
}

impl Add<Time> for Date {
    type Output = Timestamp;

    fn add(self, rhs: Time) -> Self::Output {
        unsafe {
            direct_function_call(pg_sys::datetime_timestamp, &[self.into_datum(), rhs.into_datum()])
                .unwrap()
        }
    }
}

impl Add<Date> for Interval {
    type Output = Timestamp;

    fn add(self, rhs: Date) -> Self::Output {
        rhs + self
    }
}

impl Add<Timestamp> for Interval {
    type Output = Timestamp;

    fn add(self, rhs: Timestamp) -> Self::Output {
        rhs + self
    }
}

impl Add<Date> for Time {
    type Output = Timestamp;

    fn add(self, rhs: Date) -> Self::Output {
        rhs + self
    }
}

impl Sub<Interval> for Timestamp {
    type Output = Timestamp;

    fn sub(self, rhs: Interval) -> Self::Output {
        unsafe {
            direct_function_call(
                pg_sys::timestamp_mi_interval,
                &[self.into_datum(), rhs.as_datum()],
            )
            .unwrap()
        }
    }
}

impl Add<Interval> for Timestamp {
    type Output = Timestamp;

    fn add(self, rhs: Interval) -> Self::Output {
        unsafe {
            direct_function_call(
                pg_sys::timestamp_pl_interval,
                &[self.into_datum(), rhs.as_datum()],
            )
            .unwrap()
        }
    }
}

impl Sub for TimestampWithTimeZone {
    type Output = Interval;

    fn sub(self, rhs: Self) -> Self::Output {
        unsafe {
            // yes, `timestamp_mi` is correct for TimestampWithTimeZone
            //
            // # \sf timestamptz_mi
            // CREATE OR REPLACE FUNCTION pg_catalog.timestamptz_mi(timestamp with time zone, timestamp with time zone)
            //  RETURNS interval
            //  LANGUAGE internal
            //  IMMUTABLE PARALLEL SAFE STRICT
            // AS $function$timestamp_mi$function$
            direct_function_call(pg_sys::timestamp_mi, &[self.into_datum(), rhs.into_datum()])
                .unwrap()
        }
    }
}

impl Add<TimeWithTimeZone> for Date {
    type Output = TimestampWithTimeZone;

    fn add(self, rhs: TimeWithTimeZone) -> Self::Output {
        unsafe {
            direct_function_call(
                pg_sys::datetimetz_timestamptz,
                &[self.into_datum(), rhs.into_datum()],
            )
            .unwrap()
        }
    }
}

impl Add<TimeWithTimeZone> for Interval {
    type Output = TimeWithTimeZone;

    fn add(self, rhs: TimeWithTimeZone) -> Self::Output {
        rhs + self
    }
}

impl Add<Interval> for TimeWithTimeZone {
    type Output = TimeWithTimeZone;

    fn add(self, rhs: Interval) -> Self::Output {
        unsafe {
            direct_function_call(pg_sys::timetz_pl_interval, &[self.into_datum(), rhs.as_datum()])
                .unwrap()
        }
    }
}

impl Add<TimestampWithTimeZone> for Interval {
    type Output = TimestampWithTimeZone;

    fn add(self, rhs: TimestampWithTimeZone) -> Self::Output {
        rhs + self
    }
}

impl Sub<Interval> for TimestampWithTimeZone {
    type Output = TimestampWithTimeZone;

    fn sub(self, rhs: Interval) -> Self::Output {
        unsafe {
            direct_function_call(
                pg_sys::timestamptz_mi_interval,
                &[self.into_datum(), rhs.as_datum()],
            )
            .unwrap()
        }
    }
}

impl Add<Interval> for TimestampWithTimeZone {
    type Output = TimestampWithTimeZone;

    fn add(self, rhs: Interval) -> Self::Output {
        unsafe {
            direct_function_call(
                pg_sys::timestamptz_pl_interval,
                &[self.into_datum(), rhs.as_datum()],
            )
            .unwrap()
        }
    }
}

impl Add<Date> for TimeWithTimeZone {
    type Output = TimestampWithTimeZone;

    fn add(self, rhs: Date) -> Self::Output {
        rhs + self
    }
}

impl Sub<Interval> for TimeWithTimeZone {
    type Output = TimeWithTimeZone;

    fn sub(self, rhs: Interval) -> Self::Output {
        unsafe {
            direct_function_call(pg_sys::timetz_mi_interval, &[self.into_datum(), rhs.as_datum()])
                .unwrap()
        }
    }
}
