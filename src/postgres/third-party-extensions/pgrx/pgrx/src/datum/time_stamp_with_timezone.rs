//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use super::{
    Date, DateTimeConversionError, DateTimeParts, DateTimeTypeVisitor, FromDatum,
    HasExtractableParts, Interval, IntoDatum, Timestamp, ToIsoString,
};
use crate::{direct_function_call, pg_sys};
use pgrx_pg_sys::errcodes::PgSqlErrorCode;
use pgrx_pg_sys::PgTryBuilder;
use pgrx_sql_entity_graph::metadata::{
    ArgumentError, Returns, ReturnsError, SqlMapping, SqlTranslatable,
};
use std::panic::{RefUnwindSafe, UnwindSafe};

// taken from /include/datatype/timestamp.h
const MIN_TIMESTAMP_USEC: i64 = -211_813_488_000_000_000;
const END_TIMESTAMP_USEC: i64 = 9_223_371_331_200_000_000 - 1; // dec by 1 to accommodate exclusive range match pattern

/// A safe wrapper around Postgres `TIMESTAMP WITH TIME ZONE` type, backed by a [`pg_sys::Timestamp`] integer value.
#[derive(Debug, Copy, Clone)]
#[repr(transparent)]
pub struct TimestampWithTimeZone(pg_sys::TimestampTz);

impl From<TimestampWithTimeZone> for pg_sys::TimestampTz {
    #[inline]
    fn from(value: TimestampWithTimeZone) -> Self {
        value.0
    }
}

/// Fallibly create a [`TimestampWithTimeZone`] from a Postgres [`pg_sys::TimestampTz`] value.
impl TryFrom<pg_sys::TimestampTz> for TimestampWithTimeZone {
    type Error = FromTimeError;

    fn try_from(value: pg_sys::TimestampTz) -> Result<Self, Self::Error> {
        match value {
            i64::MIN | i64::MAX | MIN_TIMESTAMP_USEC..=END_TIMESTAMP_USEC => {
                Ok(TimestampWithTimeZone(value))
            }
            _ => Err(FromTimeError::MicrosOutOfBounds),
        }
    }
}

impl TryFrom<pg_sys::Datum> for TimestampWithTimeZone {
    type Error = FromTimeError;
    fn try_from(datum: pg_sys::Datum) -> Result<Self, Self::Error> {
        (datum.value() as pg_sys::TimestampTz).try_into()
    }
}

/// Create a [`TimestampWithTimeZone`] from an existing [`Timestamp`] (which is understood to be
/// in the "current time zone" and a time zone string.
impl<Tz: AsRef<str> + UnwindSafe + RefUnwindSafe> TryFrom<(Timestamp, Tz)>
    for TimestampWithTimeZone
{
    type Error = DateTimeConversionError;

    fn try_from(value: (Timestamp, Tz)) -> Result<Self, Self::Error> {
        let (ts, tz) = value;
        TimestampWithTimeZone::with_timezone(
            ts.year(),
            ts.month(),
            ts.day(),
            ts.hour(),
            ts.minute(),
            ts.second(),
            tz,
        )
    }
}

impl From<Date> for TimestampWithTimeZone {
    fn from(value: Date) -> Self {
        unsafe { direct_function_call(pg_sys::date_timestamptz, &[value.into_datum()]).unwrap() }
    }
}

impl From<Timestamp> for TimestampWithTimeZone {
    fn from(value: Timestamp) -> Self {
        unsafe {
            direct_function_call(pg_sys::timestamp_timestamptz, &[value.into_datum()]).unwrap()
        }
    }
}

impl IntoDatum for TimestampWithTimeZone {
    fn into_datum(self) -> Option<pg_sys::Datum> {
        Some(pg_sys::Datum::from(self.0))
    }
    fn type_oid() -> pg_sys::Oid {
        pg_sys::TIMESTAMPTZOID
    }
}

impl FromDatum for TimestampWithTimeZone {
    unsafe fn from_polymorphic_datum(
        datum: pg_sys::Datum,
        is_null: bool,
        _: pg_sys::Oid,
    ) -> Option<Self>
    where
        Self: Sized,
    {
        if is_null {
            None
        } else {
            Some(datum.try_into().expect("Error converting timestamp with time zone datum"))
        }
    }
}

impl TimestampWithTimeZone {
    const NEG_INFINITY: pg_sys::TimestampTz = pg_sys::TimestampTz::MIN;
    const INFINITY: pg_sys::TimestampTz = pg_sys::TimestampTz::MAX;

    /// Construct a new [`TimestampWithTimeZone`] from its constituent parts.
    ///
    /// # Notes
    ///
    /// This function uses Postgres' "current time zone"
    ///
    /// # Errors
    ///
    /// Returns a [`DateTimeConversionError`] if any part is outside the bounds for that part
    pub fn new(
        year: i32,
        month: u8,
        day: u8,
        hour: u8,
        minute: u8,
        second: f64,
    ) -> Result<Self, DateTimeConversionError> {
        let month: i32 = month as _;
        let day: i32 = day as _;
        let hour: i32 = hour as _;
        let minute: i32 = minute as _;

        PgTryBuilder::new(|| unsafe {
            Ok(direct_function_call(
                pg_sys::make_timestamptz,
                &[
                    year.into_datum(),
                    month.into_datum(),
                    day.into_datum(),
                    hour.into_datum(),
                    minute.into_datum(),
                    second.into_datum(),
                ],
            )
            .unwrap())
        })
        .catch_when(PgSqlErrorCode::ERRCODE_DATETIME_FIELD_OVERFLOW, |_| {
            Err(DateTimeConversionError::FieldOverflow)
        })
        .catch_when(PgSqlErrorCode::ERRCODE_INVALID_DATETIME_FORMAT, |_| {
            Err(DateTimeConversionError::InvalidFormat)
        })
        .execute()
    }

    /// Construct a new [`TimestampWithTimeZone`] from its constituent parts.
    ///
    /// Elides the overhead of trapping errors for out-of-bounds parts
    ///
    /// # Notes
    ///
    /// This function uses Postgres' "current time zone"
    ///
    /// # Panics
    ///
    /// This function panics if any part is out-of-bounds
    pub fn new_unchecked(
        year: isize,
        month: u8,
        day: u8,
        hour: u8,
        minute: u8,
        second: f64,
    ) -> Self {
        let year: i32 = year as _;
        let month: i32 = month as _;
        let day: i32 = day as _;
        let hour: i32 = hour as _;
        let minute: i32 = minute as _;

        unsafe {
            direct_function_call(
                pg_sys::make_timestamptz,
                &[
                    year.into_datum(),
                    month.into_datum(),
                    day.into_datum(),
                    hour.into_datum(),
                    minute.into_datum(),
                    second.into_datum(),
                ],
            )
            .unwrap()
        }
    }

    /// Construct a new [`TimestampWithTimeZone`] from its constituent parts at a specific time zone
    ///
    /// # Errors
    ///
    /// Returns a [`DateTimeConversionError`] if any part is outside the bounds for that part
    pub fn with_timezone<Tz: AsRef<str> + UnwindSafe + RefUnwindSafe>(
        year: i32,
        month: u8,
        day: u8,
        hour: u8,
        minute: u8,
        second: f64,
        timezone: Tz,
    ) -> Result<Self, DateTimeConversionError> {
        let month: i32 = month as _;
        let day: i32 = day as _;
        let hour: i32 = hour as _;
        let minute: i32 = minute as _;
        let timezone_datum = timezone.as_ref().into_datum();

        PgTryBuilder::new(|| unsafe {
            Ok(direct_function_call(
                pg_sys::make_timestamptz_at_timezone,
                &[
                    year.into_datum(),
                    month.into_datum(),
                    day.into_datum(),
                    hour.into_datum(),
                    minute.into_datum(),
                    second.into_datum(),
                    timezone_datum,
                ],
            )
            .unwrap())
        })
        .catch_when(PgSqlErrorCode::ERRCODE_DATETIME_FIELD_OVERFLOW, |_| {
            Err(DateTimeConversionError::FieldOverflow)
        })
        .catch_when(PgSqlErrorCode::ERRCODE_INVALID_DATETIME_FORMAT, |_| {
            Err(DateTimeConversionError::InvalidFormat)
        })
        .catch_when(PgSqlErrorCode::ERRCODE_INVALID_PARAMETER_VALUE, |_| {
            Err(DateTimeConversionError::UnknownTimezone(timezone.as_ref().to_string()))
        })
        .execute()
    }

    /// Construct a new [`TimestampWithTimeZone`] representing positive infinity
    pub fn positive_infinity() -> Self {
        Self(Self::INFINITY)
    }

    /// Construct a new [`TimestampWithTimeZone`] representing negative infinity
    pub fn negative_infinity() -> Self {
        Self(Self::NEG_INFINITY)
    }

    /// Does this [`TimestampWithTimeZone`] represent positive infinity?
    #[inline]
    pub fn is_infinity(&self) -> bool {
        self.0 == Self::INFINITY
    }

    /// Does this [`TimestampWithTimeZone`] represent negative infinity?
    #[inline]
    pub fn is_neg_infinity(&self) -> bool {
        self.0 == Self::NEG_INFINITY
    }

    /// Extract the `month`
    pub fn month(&self) -> u8 {
        self.extract_part(DateTimeParts::Month).unwrap().try_into().unwrap()
    }

    /// Extract the `day`
    pub fn day(&self) -> u8 {
        self.extract_part(DateTimeParts::Day).unwrap().try_into().unwrap()
    }

    /// Extract the `year`
    pub fn year(&self) -> i32 {
        self.extract_part(DateTimeParts::Year).unwrap().try_into().unwrap()
    }

    /// Extract the `hour`
    pub fn hour(&self) -> u8 {
        self.extract_part(DateTimeParts::Hour).unwrap().try_into().unwrap()
    }

    /// Extract the `minute`
    pub fn minute(&self) -> u8 {
        self.extract_part(DateTimeParts::Minute).unwrap().try_into().unwrap()
    }

    /// Extract the `second`
    pub fn second(&self) -> f64 {
        self.extract_part(DateTimeParts::Second).unwrap().try_into().unwrap()
    }

    /// Return the `microseconds` part.  This is not the time counted in microseconds, but the
    /// fractional seconds
    pub fn microseconds(&self) -> u32 {
        self.extract_part(DateTimeParts::Microseconds).unwrap().try_into().unwrap()
    }

    /// Return the `hour`, `minute`, `second`, and `microseconds` as a Rust tuple
    pub fn to_hms_micro(&self) -> (u8, u8, u8, u32) {
        (self.hour(), self.minute(), self.second() as u8, self.microseconds())
    }

    /// Shift the [`Timestamp`] to the `UTC` time zone
    pub fn to_utc(&self) -> Timestamp {
        self.at_timezone("UTC").unwrap()
    }

    /// Shift the [`TimestampWithTimeZone`] to the specified time zone
    ///
    /// # Errors
    ///
    /// Returns a [`DateTimeConversionError`] if the specified time zone is invalid or if for some
    /// other reason the underlying time cannot be represented in the specified time zone
    pub fn at_timezone<Tz: AsRef<str> + UnwindSafe + RefUnwindSafe>(
        &self,
        timezone: Tz,
    ) -> Result<Timestamp, DateTimeConversionError> {
        let timezone_datum = timezone.as_ref().into_datum();
        PgTryBuilder::new(|| unsafe {
            Ok(direct_function_call(
                pg_sys::timestamptz_zone,
                &[timezone_datum, (*self).into_datum()],
            )
            .unwrap())
        })
        .catch_when(PgSqlErrorCode::ERRCODE_DATETIME_FIELD_OVERFLOW, |_| {
            Err(DateTimeConversionError::FieldOverflow)
        })
        .catch_when(PgSqlErrorCode::ERRCODE_INVALID_DATETIME_FORMAT, |_| {
            Err(DateTimeConversionError::InvalidFormat)
        })
        .catch_when(PgSqlErrorCode::ERRCODE_INVALID_PARAMETER_VALUE, |_| {
            Err(DateTimeConversionError::UnknownTimezone(timezone.as_ref().to_string()))
        })
        .execute()
    }

    pub fn is_finite(&self) -> bool {
        !matches!(self.0, pg_sys::TimestampTz::MIN | pg_sys::TimestampTz::MAX)
    }

    /// Truncate [`TimestampWithTimeZone`] to specified units
    pub fn truncate(self, units: DateTimeParts) -> Self {
        unsafe {
            direct_function_call(
                pg_sys::timestamptz_trunc,
                &[units.into_datum(), self.into_datum()],
            )
            .unwrap()
        }
    }

    /// Truncate [`TimestampWithTimeZone`] to specified units in specified time zone
    pub fn truncate_with_time_zone<Tz: AsRef<str>>(self, units: DateTimeParts, zone: Tz) -> Self {
        unsafe {
            direct_function_call(
                pg_sys::timestamptz_trunc_zone,
                &[units.into_datum(), self.into_datum(), zone.as_ref().into_datum()],
            )
            .unwrap()
        }
    }

    /// Subtract `other` from `self`, producing a “symbolic” result that uses years and months, rather than just days
    pub fn age(&self, other: &TimestampWithTimeZone) -> Interval {
        let ts_self: Timestamp = (*self).into();
        let ts_other: Timestamp = (*other).into();
        ts_self.age(&ts_other)
    }

    /// Return the backing [`pg_sys::TimestampTz`] value.
    #[inline]
    pub fn into_inner(self) -> pg_sys::TimestampTz {
        self.0
    }
}

#[derive(thiserror::Error, Debug, Clone, Copy)]
pub enum FromTimeError {
    #[error("timestamp value is negative infinity and shouldn't map to time::PrimitiveDateTime")]
    NegInfinity,
    #[error("timestamp value is negative infinity and shouldn't map to time::PrimitiveDateTime")]
    Infinity,
    #[error("time::PrimitiveDateTime was unable to convert this timestamp")]
    TimeCrate,
    #[error("microseconds outside of target microsecond range")]
    MicrosOutOfBounds,
    #[error("hours outside of target range")]
    HoursOutOfBounds,
    #[error("minutes outside of target range")]
    MinutesOutOfBounds,
    #[error("seconds outside of target range")]
    SecondsOutOfBounds,
}

impl serde::Serialize for TimestampWithTimeZone {
    /// Serialize this [`TimestampWithTimeZone`] in ISO form, compatible with most JSON parsers
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

impl<'de> serde::Deserialize<'de> for TimestampWithTimeZone {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::de::Deserializer<'de>,
    {
        deserializer.deserialize_str(DateTimeTypeVisitor::<Self>::new())
    }
}

unsafe impl SqlTranslatable for TimestampWithTimeZone {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("timestamp with time zone"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("timestamp with time zone")))
    }
}
