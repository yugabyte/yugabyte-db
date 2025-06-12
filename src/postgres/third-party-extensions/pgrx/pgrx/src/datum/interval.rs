//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::datum::datetime_support::{IntervalConversionError, USECS_PER_DAY, USECS_PER_SEC};
use crate::datum::{DateTimeParts, DateTimeTypeVisitor, FromDatum, IntoDatum, Time, ToIsoString};
use crate::{direct_function_call, pg_sys};
use pgrx_sql_entity_graph::metadata::{
    ArgumentError, Returns, ReturnsError, SqlMapping, SqlTranslatable,
};

/// From the PG docs <https://www.postgresql.org/docs/current/datatype-datetime.html#DATATYPE-INTERVAL-INPUT>
/// Internally interval values are stored as months, days, and microseconds. This is done because the number of days in a month varies,
/// and a day can have 23 or 25hours if a daylight savings time adjustment is involved. The months and days fields are integers while
/// the microseconds field can store fractional seconds. Because intervals are usually created from constant strings or timestamp
/// subtraction, this storage method works well in most cases...
#[derive(Debug, Clone, Copy)]
#[repr(transparent)]
pub struct Interval(pg_sys::Interval);

impl Interval {
    /// This function takes `months`/`days`/`microseconds` as input to convert directly to the internal PG storage struct `pg_sys::Interval`
    /// - the sign of all units must be all matching in the positive or all matching in the negative direction
    pub fn new(months: i32, days: i32, micros: i64) -> Result<Self, IntervalConversionError> {
        if months < 0 {
            if days > 0 || micros > 0 {
                return Err(IntervalConversionError::MismatchedSigns);
            }
        } else if months > 0 && (days < 0 || micros < 0) {
            return Err(IntervalConversionError::MismatchedSigns);
        }

        Ok(Interval(pg_sys::Interval { time: micros, day: days, month: months }))
    }

    pub fn from_years(years: i32) -> Self {
        Self::from(Some(years), None, None, None, None, None, None).unwrap()
    }

    pub fn from_months(months: i32) -> Self {
        Self::from(None, Some(months), None, None, None, None, None).unwrap()
    }

    pub fn from_weeks(weeks: i32) -> Self {
        Self::from(None, None, Some(weeks), None, None, None, None).unwrap()
    }

    pub fn from_days(days: i32) -> Self {
        Self::from(None, None, None, Some(days), None, None, None).unwrap()
    }

    pub fn from_hours(hours: i32) -> Self {
        Self::from(None, None, None, None, Some(hours), None, None).unwrap()
    }

    pub fn from_minutes(minutes: i32) -> Self {
        Self::from(None, None, None, None, None, Some(minutes), None).unwrap()
    }

    pub fn from_seconds(seconds: f64) -> Self {
        Self::from(None, None, None, None, None, None, Some(seconds)).unwrap()
    }

    pub fn from_micros(microseconds: i64) -> Self {
        Self::from_seconds(microseconds as f64 / 1_000_000.0)
    }

    pub fn from(
        years: Option<i32>,
        months: Option<i32>,
        weeks: Option<i32>,
        days: Option<i32>,
        hours: Option<i32>,
        minutes: Option<i32>,
        seconds: Option<f64>,
    ) -> Result<Self, IntervalConversionError> {
        match (years.unwrap_or_default() <= 0
            && months.unwrap_or_default() <= 0
            && weeks.unwrap_or_default() <= 0
            && days.unwrap_or_default() <= 0
            && hours.unwrap_or_default() <= 0
            && minutes.unwrap_or_default() <= 0
            && seconds.unwrap_or_default().is_sign_negative())
            || (years.unwrap_or_default() >= 0
                && months.unwrap_or_default() >= 0
                && weeks.unwrap_or_default() >= 0
                && days.unwrap_or_default() >= 0
                && hours.unwrap_or_default() >= 0
                && minutes.unwrap_or_default() >= 0
                && seconds.unwrap_or_default().is_sign_positive())
        {
            true => unsafe {
                Ok(direct_function_call(
                    pg_sys::make_interval,
                    &[
                        years.into_datum(),
                        months.into_datum(),
                        weeks.into_datum(),
                        days.into_datum(),
                        hours.into_datum(),
                        minutes.into_datum(),
                        seconds.into_datum(),
                    ],
                )
                .unwrap())
            },
            false => Err(IntervalConversionError::MismatchedSigns),
        }
    }

    /// Total number of months before/after 2000-01-01
    #[inline]
    pub fn months(&self) -> i32 {
        self.0.month
    }

    /// Total number of days before/after the `months()` offset (sign must match `months`)
    #[inline]
    pub fn days(&self) -> i32 {
        self.0.day
    }

    /// Total number of microseconds before/after the `days()` offset (sign must match `months`/`days`)
    #[inline]
    pub fn micros(&self) -> i64 {
        self.0.time
    }

    #[inline]
    pub fn as_micros(&self) -> i128 {
        self.micros() as i128
            + self.months() as i128 * pg_sys::DAYS_PER_MONTH as i128 * USECS_PER_DAY as i128
            + self.days() as i128 * USECS_PER_DAY as i128
    }

    #[inline]
    pub fn abs(self) -> Self {
        Interval(pg_sys::Interval {
            time: self.0.time.abs(),
            day: self.0.day.abs(),
            month: self.0.month.abs(),
        })
    }

    #[inline]
    pub fn signum(self) -> Self {
        if self.0.month == 0 && self.0.day == 0 && self.0.time == 0 {
            Interval(pg_sys::Interval { time: 0, day: 0, month: 0 })
        } else if self.is_positive() {
            Interval(pg_sys::Interval { time: 1, day: 0, month: 0 })
        } else {
            Interval(pg_sys::Interval { time: -1, day: 0, month: 0 })
        }
    }

    #[inline]
    pub fn is_positive(self) -> bool {
        !self.is_negative()
    }

    #[inline]
    pub fn is_negative(self) -> bool {
        self.0.month < 0 || self.0.day < 0 || self.0.time < 0
    }

    /// Truncate [`Interval`] to specified units
    pub fn truncate(self, units: DateTimeParts) -> Self {
        unsafe {
            direct_function_call(pg_sys::interval_trunc, &[units.into_datum(), self.into_datum()])
                .unwrap()
        }
    }

    /// Promote groups of 30 days to numbers of months
    pub fn justify_days(self) -> Self {
        unsafe {
            direct_function_call(pg_sys::interval_justify_days, &[self.into_datum()]).unwrap()
        }
    }

    /// Promote groups of 24 hours to numbers of days
    pub fn justify_hours(self) -> Self {
        unsafe {
            direct_function_call(pg_sys::interval_justify_hours, &[self.into_datum()]).unwrap()
        }
    }

    /// Promote groups of 24 hours to numbers of days and promote groups of 30 days to numbers of months
    pub fn justify(self) -> Self {
        unsafe {
            direct_function_call(pg_sys::interval_justify_interval, &[self.into_datum()]).unwrap()
        }
    }

    #[inline]
    pub(crate) unsafe fn as_datum(&self) -> Option<pg_sys::Datum> {
        Some(pg_sys::Datum::from(&self.0 as *const _))
    }

    /// Return the backing [`pg_sys::Interval`] value.
    #[inline]
    pub fn into_inner(self) -> pg_sys::Interval {
        self.0
    }
}

impl FromDatum for Interval {
    unsafe fn from_polymorphic_datum(
        datum: pg_sys::Datum,
        is_null: bool,
        _typoid: pg_sys::Oid,
    ) -> Option<Self>
    where
        Self: Sized,
    {
        if is_null {
            None
        } else {
            let ptr = datum.cast_mut_ptr::<pg_sys::Interval>();
            // SAFETY:  Caller asserted the datum points to a pg_sys::Interval
            Some(Interval(ptr.read()))
        }
    }
}

impl IntoDatum for Interval {
    fn into_datum(self) -> Option<pg_sys::Datum> {
        unsafe {
            let ptr =
                pg_sys::palloc(std::mem::size_of::<pg_sys::Interval>()).cast::<pg_sys::Interval>();
            ptr.write(self.0);
            Some(pg_sys::Datum::from(ptr))
        }
    }
    fn type_oid() -> pg_sys::Oid {
        pg_sys::INTERVALOID
    }
}

impl TryFrom<std::time::Duration> for Interval {
    type Error = IntervalConversionError;
    fn try_from(duration: std::time::Duration) -> Result<Interval, Self::Error> {
        let microseconds = duration.as_micros();
        let seconds = microseconds / USECS_PER_SEC as u128;
        let days = seconds / pg_sys::SECS_PER_DAY as u128;
        let months = days / pg_sys::DAYS_PER_MONTH as u128;
        let leftover_days = days - months * pg_sys::DAYS_PER_MONTH as u128;
        let leftover_microseconds = microseconds
            - (leftover_days * USECS_PER_DAY as u128
                + (months * pg_sys::DAYS_PER_MONTH as u128 * USECS_PER_DAY as u128));

        Interval::new(
            months.try_into().map_err(|_| IntervalConversionError::DurationMonthsOutOfBounds)?,
            leftover_days.try_into().expect("bad math during Duration to Interval days"),
            leftover_microseconds.try_into().expect("bad math during Duration to Interval micros"),
        )
    }
}

impl From<Time> for Interval {
    fn from(value: Time) -> Self {
        unsafe { direct_function_call(pg_sys::time_interval, &[value.into_datum()]).unwrap() }
    }
}

impl TryFrom<Interval> for std::time::Duration {
    type Error = IntervalConversionError;

    fn try_from(interval: Interval) -> Result<Self, Self::Error> {
        if interval.0.time < 0 || interval.0.month < 0 || interval.0.day < 0 {
            return Err(IntervalConversionError::NegativeInterval);
        }

        let micros = interval.0.time as u128
            + interval.0.day as u128 * pg_sys::SECS_PER_DAY as u128 * USECS_PER_SEC as u128
            + interval.0.month as u128 * pg_sys::DAYS_PER_MONTH as u128 * USECS_PER_DAY as u128;

        Ok(std::time::Duration::from_micros(
            micros.try_into().map_err(|_| IntervalConversionError::IntervalTooLarge)?,
        ))
    }
}

impl serde::Serialize for Interval {
    fn serialize<S>(
        &self,
        serializer: S,
    ) -> std::result::Result<<S as serde::Serializer>::Ok, <S as serde::Serializer>::Error>
    where
        S: serde::Serializer,
    {
        serializer
            .serialize_str(&self.to_iso_string())
            .map_err(|err| serde::ser::Error::custom(format!("formatting problem: {err:?}")))
    }
}

impl<'de> serde::Deserialize<'de> for Interval {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::de::Deserializer<'de>,
    {
        deserializer.deserialize_str(DateTimeTypeVisitor::<Self>::new())
    }
}
unsafe impl SqlTranslatable for Interval {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("interval"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("interval")))
    }
}
