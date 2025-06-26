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
    AnyNumeric, Date, Interval, IntoDatum, Time, TimeWithTimeZone, Timestamp, TimestampWithTimeZone,
};
use crate::{direct_function_call, pg_sys};
use core::fmt::{Display, Formatter};
use core::str::FromStr;
use pgrx_pg_sys::errcodes::PgSqlErrorCode;
use pgrx_pg_sys::{pg_tz, PgTryBuilder};
use std::cmp::Ordering;
use std::hash::{Hash, Hasher};
use std::marker::PhantomData;

mod ctor;
mod ops;

pub use ctor::*;

pub const USECS_PER_SEC: i64 = 1_000_000;
pub const USECS_PER_DAY: i64 = pg_sys::SECS_PER_DAY as i64 * USECS_PER_SEC;

/// Tags to identify which "part" of a date or time-type value to extract or truncate to
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum DateTimeParts {
    /// The century
    ///
    /// The first century starts at 0001-01-01 00:00:00 AD, although they did not know it at the time.
    /// This definition applies to all Gregorian calendar countries. There is no century number 0,
    /// you go from -1 century to 1 century. If you disagree with this, please write your complaint
    /// to: Pope, Cathedral Saint-Peter of Roma, Vatican.
    Century,

    /// For `timestamp` values, the day (of the month) field (1–31) ; for `interval values`, the
    /// number of days
    Day,

    /// The year field divided by 10
    Decade,

    /// The day of the week as Sunday (0) to Saturday (6)
    DayOfWeek,

    /// The day of the year (1–365/366)
    DayOfYear,

    /// For timestamp with time zone values, the number of seconds since 1970-01-01 00:00:00 UTC
    /// (negative for timestamps before that); for date and timestamp values, the nominal number of
    /// seconds since 1970-01-01 00:00:00, without regard to time zone or daylight-savings rules; for
    /// interval values, the total number of seconds in the interval
    Epoch,

    /// The hour field (0–23)
    Hour,

    /// The day of the week as Monday (1) to Sunday (7)
    ///
    /// This is identical to dow except for Sunday. This matches the ISO 8601 day of the week numbering.
    ISODayOfWeek,

    /// The ISO 8601 week-numbering year that the date falls in (not applicable to intervals)
    ///
    /// Each ISO 8601 week-numbering year begins with the Monday of the week containing the 4th of
    /// January, so in early January or late December the ISO year may be different from the
    /// Gregorian year. See the week field for more information.
    ISOYear,

    /// The *Julian Date* corresponding to the date or timestamp (not applicable to intervals).
    /// Timestamps that are not local midnight result in a fractional value. See [Section B.7] for
    /// more information.
    ///
    /// [Section B.7](https://www.postgresql.org/docs/current/datetime-julian-dates.html)
    Julian,

    /// The seconds field, including fractional parts, multiplied by 1 000 000; note that this
    /// includes full seconds
    Microseconds,

    /// The millennium
    Millennium,

    /// The seconds field, including fractional parts, multiplied by 1000. Note that this includes
    /// full seconds.
    Milliseconds,

    /// The minutes field (0–59)
    Minute,

    /// For `timestamp` values, the number of the month within the year (1–12) ; for `interval` values,
    /// the number of months, modulo 12 (0–11)
    Month,

    /// The quarter of the year (1–4) that the date is in
    Quarter,

    /// The seconds field, including any fractional seconds
    Second,

    /// The time zone offset from UTC, measured in seconds. Positive values correspond to time zones
    /// east of UTC, negative values to zones west of UTC. (Technically, PostgreSQL does not use UTC
    /// because leap seconds are not handled.)
    Timezone,

    /// The hour component of the time zone offset
    TimezoneHour,

    /// The minute component of the time zone offset
    TimezoneMinute,

    /// The number of the ISO 8601 week-numbering week of the year. By definition, ISO weeks start on
    /// Mondays and the first week of a year contains January 4 of that year. In other words, the
    /// first Thursday of a year is in week 1 of that year.
    ///
    /// In the ISO week-numbering system, it is possible for early-January dates to be part of the
    /// 52nd or 53rd week of the previous year, and for late-December dates to be part of the first
    /// week of the next year. For example, 2005-01-01 is part of the 53rd week of year 2004, and
    /// 2006-01-01 is part of the 52nd week of year 2005, while 2012-12-31 is part of the first week
    /// of 2013. It's recommended to use the isoyear field together with week to get consistent results.
    Week,

    /// The year field. Keep in mind there is no `0 AD`, so subtracting BC years from AD years should
    /// be done with care.
    Year,
}

impl From<DateTimeParts> for &'static str {
    /// Convert to Postgres' string representation of [`DateTimeParts`]
    #[inline]
    fn from(value: DateTimeParts) -> Self {
        match value {
            DateTimeParts::Century => "century",
            DateTimeParts::Day => "day",
            DateTimeParts::Decade => "decade",
            DateTimeParts::DayOfWeek => "dow",
            DateTimeParts::DayOfYear => "doy",
            DateTimeParts::Epoch => "epoch",
            DateTimeParts::Hour => "hour",
            DateTimeParts::ISODayOfWeek => "isodow",
            DateTimeParts::ISOYear => "isodoy",
            DateTimeParts::Julian => "julian",
            DateTimeParts::Microseconds => "microseconds",
            DateTimeParts::Millennium => "millennium",
            DateTimeParts::Milliseconds => "milliseconds",
            DateTimeParts::Minute => "minute",
            DateTimeParts::Month => "month",
            DateTimeParts::Quarter => "quarter",
            DateTimeParts::Second => "second",
            DateTimeParts::Timezone => "timezone",
            DateTimeParts::TimezoneHour => "timezone_hour",
            DateTimeParts::TimezoneMinute => "timezone_minute",
            DateTimeParts::Week => "week",
            DateTimeParts::Year => "year",
        }
    }
}

impl Display for DateTimeParts {
    fn fmt(&self, f: &mut Formatter<'_>) -> core::fmt::Result {
        let name: &'static str = (*self).into();
        write!(f, "{name}")
    }
}

impl IntoDatum for DateTimeParts {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        let name: &'static str = self.into();
        name.into_datum()
    }

    #[inline]
    fn type_oid() -> pg_sys::Oid {
        pg_sys::TEXTOID
    }
}

mod seal {
    #[doc(hidden)]
    pub trait DateTimeType {}
}

pub trait HasExtractableParts: Clone + IntoDatum + seal::DateTimeType {
    const EXTRACT_FUNCTION: unsafe fn(pg_sys::FunctionCallInfo) -> pg_sys::Datum;

    /// Extract a [`DateTimeParts`] part from a date/time-like type
    fn extract_part(&self, field: DateTimeParts) -> Option<AnyNumeric> {
        unsafe {
            let field_datum = field.into_datum();
            #[cfg(feature = "pg13")]
            let field_value: Option<f64> = direct_function_call(
                Self::EXTRACT_FUNCTION,
                &[field_datum, self.clone().into_datum()],
            );
            #[cfg(any(feature = "pg14", feature = "pg15", feature = "pg16", feature = "pg17"))]
            let field_value: Option<AnyNumeric> = direct_function_call(
                Self::EXTRACT_FUNCTION,
                &[field_datum, self.clone().into_datum()],
            );
            // don't leak the TEXT datum we made
            pg_sys::pfree(field_datum.unwrap().cast_mut_ptr());

            #[cfg(feature = "pg13")]
            {
                field_value.map(|v| AnyNumeric::from_str(&format!("{v}")).unwrap())
            }
            #[cfg(not(feature = "pg13"))]
            {
                field_value
            }
        }
    }
}

pub trait ToIsoString: IntoDatum + Sized + Display + seal::DateTimeType {
    /// Encode of this date/time-like type into JSON string in ISO format using
    /// optionally preallocated buffer 'buf'.
    ///
    /// # Notes
    ///
    /// Types `with time zone` use the Postgres globally configured time zone in the text representation
    fn to_iso_string(self) -> String {
        if Self::type_oid() == pg_sys::INTERVALOID {
            // `Interval` is just represented in its string form
            self.to_string()
        } else {
            unsafe {
                let jsonb = pg_sys::JsonEncodeDateTime(
                    std::ptr::null_mut(),
                    self.into_datum().unwrap(),
                    Self::type_oid(),
                    std::ptr::null(),
                );
                let cstr = core::ffi::CStr::from_ptr(jsonb);
                let as_string = cstr.to_str().unwrap().to_string();
                pg_sys::pfree(jsonb.cast());

                as_string
            }
        }
    }

    /// Encode of this date/time-like type into JSON string in ISO format using
    /// optionally preallocated buffer 'buf'.
    ///
    /// # Notes
    ///
    /// This function is only available on Postgres v13 and greater
    #[cfg(any(
        feature = "pg13",
        feature = "pg14",
        feature = "pg15",
        feature = "pg16",
        feature = "pg17"
    ))]
    fn to_iso_string_with_timezone<Tz: AsRef<str>>(
        self,
        timezone: Tz,
    ) -> Result<String, DateTimeConversionError> {
        if Self::type_oid() == pg_sys::INTERVALOID {
            // `Interval` is just represented in its string form
            Ok(self.to_string())
        } else {
            let tzoffset = -get_timezone_offset(&timezone)?;

            unsafe {
                let jsonb = pg_sys::JsonEncodeDateTime(
                    std::ptr::null_mut(),
                    self.into_datum().unwrap(),
                    Self::type_oid(),
                    &tzoffset,
                );
                let cstr = core::ffi::CStr::from_ptr(jsonb);
                let as_string = cstr.to_str().unwrap().to_string();
                pg_sys::pfree(jsonb.cast());

                Ok(as_string)
            }
        }
    }
}

macro_rules! impl_wrappers {
    ($ty:ty, $eq_fn:path, $cmp_fn:path, $hash_fn:path, $extract_fn:path, $input_fn:path, $output_fn:path) => {
        impl seal::DateTimeType for $ty {}

        impl Eq for $ty {}

        impl PartialEq for $ty {
            /// Uses the underlying Postgres "_eq()" function for this type
            fn eq(&self, other: &Self) -> bool {
                unsafe {
                    direct_function_call($eq_fn, &[self.into_datum(), other.into_datum()]).unwrap()
                }
            }
        }

        impl Ord for $ty {
            /// Uses the underlying Postgres "_cmp()" function for this type
            fn cmp(&self, other: &Self) -> Ordering {
                unsafe {
                    match direct_function_call::<i32>(
                        $cmp_fn,
                        &[self.into_datum(), other.into_datum()],
                    ) {
                        Some(-1) => Ordering::Less,
                        Some(0) => Ordering::Equal,
                        Some(1) => Ordering::Greater,
                        _ => panic!("unexpected response from {}", stringify!($cmp_fn)),
                    }
                }
            }
        }

        impl PartialOrd for $ty {
            fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
                Some(self.cmp(other))
            }
        }

        impl Hash for $ty {
            /// Uses the underlying Postgres "hash" function for this type
            fn hash<H: Hasher>(&self, state: &mut H) {
                let hash: i32 = unsafe {
                    direct_function_call($hash_fn, &[self.clone().into_datum()]).unwrap()
                };
                state.write_i32(hash);
            }
        }

        impl HasExtractableParts for $ty {
            const EXTRACT_FUNCTION: unsafe fn(pg_sys::FunctionCallInfo) -> pg_sys::Datum =
                $extract_fn;
        }

        impl ToIsoString for $ty {}

        impl FromStr for $ty {
            type Err = DateTimeConversionError;

            /// Create this type from a string.
            fn from_str(s: &str) -> Result<Self, Self::Err> {
                use pgrx_pg_sys::AsPgCStr;
                let cstr = s.as_pg_cstr();
                let cstr_datum = pg_sys::Datum::from(cstr);
                unsafe {
                    let result = PgTryBuilder::new(|| {
                        let result = direct_function_call::<$ty>(
                            $input_fn,
                            &[
                                Some(cstr_datum),
                                pgrx_pg_sys::InvalidOid.into_datum(),
                                (-1i32).into_datum(),
                            ],
                        )
                        .unwrap();
                        Ok(result)
                    })
                    .catch_when(PgSqlErrorCode::ERRCODE_DATETIME_FIELD_OVERFLOW, |_| {
                        Err(DateTimeConversionError::FieldOverflow)
                    })
                    .catch_when(PgSqlErrorCode::ERRCODE_INVALID_DATETIME_FORMAT, |_| {
                        Err(DateTimeConversionError::InvalidFormat)
                    })
                    .catch_when(PgSqlErrorCode::ERRCODE_INVALID_PARAMETER_VALUE, |_| {
                        Err(DateTimeConversionError::CannotParseTimezone)
                    })
                    .execute();
                    pg_sys::pfree(cstr.cast());
                    result
                }
            }
        }

        impl Display for $ty {
            /// Uses the underlying "output" function to convert this type to a String
            fn fmt(&self, f: &mut Formatter<'_>) -> core::fmt::Result {
                let text: &core::ffi::CStr = unsafe {
                    direct_function_call($output_fn, &[self.clone().into_datum()]).unwrap()
                };
                write!(f, "{}", text.to_str().unwrap())
            }
        }
    };
}

#[cfg(feature = "pg13")]
const DATE_EXTRACT: unsafe fn(fcinfo: pg_sys::FunctionCallInfo) -> pg_sys::Datum = pg13::date_part;

#[cfg(feature = "pg13")]
mod pg13 {
    use crate::prelude::*;

    pub(super) unsafe fn date_part(fcinfo: pg_sys::FunctionCallInfo) -> pg_sys::Datum {
        // we need to first convert the `date` value into a `timestamp` value
        // then call the `timestamp_part` function.
        //
        // this is essentially how the `date_part()` function is declared in the system catalogs
        // for pg13:
        /**
            \sf date_part(text, date)
            CREATE OR REPLACE FUNCTION pg_catalog.date_part(text, date)
             RETURNS double precision
             LANGUAGE sql
             IMMUTABLE PARALLEL SAFE STRICT COST 1
            AS $function$select pg_catalog.date_part($1, cast($2 as timestamp without time zone))$function$
        */
        use crate::fcinfo::*;
        let timezone = pg_getarg_datum(fcinfo, 0);
        let date = pg_getarg_datum(fcinfo, 1);
        let timestamp = direct_function_call_as_datum(pg_sys::date_timestamp, &[date]);
        direct_function_call_as_datum(pg_sys::timestamp_part, &[timezone, timestamp])
            .unwrap_or_else(|| pg_sys::Datum::from(0))
    }
}

#[cfg(any(feature = "pg14", feature = "pg15", feature = "pg16", feature = "pg17"))]
const DATE_EXTRACT: unsafe fn(fcinfo: pg_sys::FunctionCallInfo) -> pg_sys::Datum =
    pg_sys::extract_date;
impl_wrappers!(
    Date,
    pg_sys::date_eq,
    pg_sys::date_cmp,
    pg_sys::hashint8,
    DATE_EXTRACT,
    pg_sys::date_in,
    pg_sys::date_out
);

#[cfg(feature = "pg13")]
const TIME_EXTRACT: unsafe fn(fcinfo: pg_sys::FunctionCallInfo) -> pg_sys::Datum =
    pg_sys::time_part;
#[cfg(any(feature = "pg14", feature = "pg15", feature = "pg16", feature = "pg17"))]
const TIME_EXTRACT: unsafe fn(fcinfo: pg_sys::FunctionCallInfo) -> pg_sys::Datum =
    pg_sys::extract_time;

impl_wrappers!(
    Time,
    pg_sys::time_eq,
    pg_sys::time_cmp,
    pg_sys::time_hash,
    TIME_EXTRACT,
    pg_sys::time_in,
    pg_sys::time_out
);

#[cfg(feature = "pg13")]
const TIMETZ_EXTRACT: unsafe fn(fcinfo: pg_sys::FunctionCallInfo) -> pg_sys::Datum =
    pg_sys::timetz_part;
#[cfg(any(feature = "pg14", feature = "pg15", feature = "pg16", feature = "pg17"))]
const TIMETZ_EXTRACT: unsafe fn(fcinfo: pg_sys::FunctionCallInfo) -> pg_sys::Datum =
    pg_sys::extract_timetz;

impl_wrappers!(
    TimeWithTimeZone,
    pg_sys::timetz_eq,
    pg_sys::timetz_cmp,
    pg_sys::timetz_hash,
    TIMETZ_EXTRACT,
    pg_sys::timetz_in,
    pg_sys::timetz_out
);

#[cfg(feature = "pg13")]
const TIMESTAMP_EXTRACT: unsafe fn(fcinfo: pg_sys::FunctionCallInfo) -> pg_sys::Datum =
    pg_sys::timestamp_part;
#[cfg(any(feature = "pg14", feature = "pg15", feature = "pg16", feature = "pg17"))]
const TIMESTAMP_EXTRACT: unsafe fn(fcinfo: pg_sys::FunctionCallInfo) -> pg_sys::Datum =
    pg_sys::extract_timestamp;

impl_wrappers!(
    Timestamp,
    pg_sys::timestamp_eq,
    pg_sys::timestamp_cmp,
    pg_sys::timestamp_hash,
    TIMESTAMP_EXTRACT,
    pg_sys::timestamp_in,
    pg_sys::timestamp_out
);

#[cfg(feature = "pg13")]
const TIMESTAMPTZ_EXTRACT: unsafe fn(fcinfo: pg_sys::FunctionCallInfo) -> pg_sys::Datum =
    pg_sys::timestamptz_part;
#[cfg(any(feature = "pg14", feature = "pg15", feature = "pg16", feature = "pg17"))]
const TIMESTAMPTZ_EXTRACT: unsafe fn(fcinfo: pg_sys::FunctionCallInfo) -> pg_sys::Datum =
    pg_sys::extract_timestamptz;

impl_wrappers!(
    TimestampWithTimeZone,
    pg_sys::timestamp_eq,   // yes, this is correct
    pg_sys::timestamp_cmp,  // yes, this is correct
    pg_sys::timestamp_hash, // yes, this is correct
    TIMESTAMPTZ_EXTRACT,
    pg_sys::timestamptz_in,
    pg_sys::timestamptz_out
);

#[cfg(feature = "pg13")]
const INTERVAL_EXTRACT: unsafe fn(fcinfo: pg_sys::FunctionCallInfo) -> pg_sys::Datum =
    pg_sys::interval_part;
#[cfg(any(feature = "pg14", feature = "pg15", feature = "pg16", feature = "pg17"))]
const INTERVAL_EXTRACT: unsafe fn(fcinfo: pg_sys::FunctionCallInfo) -> pg_sys::Datum =
    pg_sys::extract_interval;

impl_wrappers!(
    Interval,
    pg_sys::interval_eq,
    pg_sys::interval_cmp,
    pg_sys::interval_hash,
    INTERVAL_EXTRACT,
    pg_sys::interval_in,
    pg_sys::interval_out
);

// ported from `v5.2/src/backend/utils/adt/date.c#3034`
/// Calculate the timezone offset in seconds, from GMT, for the specified named time`zone`.
///
/// If for example, the `zone` is "EDT", which is GMT-4, then the result is `-14400`.  Similarly,
/// if the `zone` is "CEST", which is GMT+2, then the result is `7200`.
///
/// ## Errors
/// Returns a [`DateTimeConversionError`] if the specified timezone is unknown to Postgres
#[cfg(any(feature = "pg16", feature = "pg17"))]
pub fn get_timezone_offset<Tz: AsRef<str>>(zone: Tz) -> Result<i32, DateTimeConversionError> {
    let zone = zone.as_ref();
    PgTryBuilder::new(|| {
        unsafe {
            let tzname = alloc::ffi::CString::new(zone).unwrap();
            let mut tz = 0;
            let mut val = 0;
            let mut tzp: *mut pg_tz = 0 as _;

            let tztype = pg_sys::DecodeTimezoneName(tzname.as_ptr(), &mut val, &mut tzp);

            if tztype == pg_sys::TZNAME_FIXED_OFFSET as i32 {
                /* fixed-offset abbreviation */
                tz = -val;
            } else if tztype == pg_sys::TZNAME_DYNTZ as i32 {
                /* dynamic-offset abbreviation, resolve using transaction start time */
                let now = pg_sys::GetCurrentTransactionStartTimestamp();
                let mut isdst = 0;

                tz = pg_sys::DetermineTimeZoneAbbrevOffsetTS(now, tzname.as_ptr(), tzp, &mut isdst);
            } else {
                /* Get the offset-from-GMT that is valid now for the zone name */
                let now = pg_sys::GetCurrentTransactionStartTimestamp();
                let mut tm = Default::default();
                let mut fsec = 0;

                if pg_sys::timestamp2tm(now, &mut tz, &mut tm, &mut fsec, std::ptr::null_mut(), tzp)
                    != 0
                {
                    return Err(DateTimeConversionError::FieldOverflow);
                }
            }
            Ok(-tz)
        }
    })
    .catch_when(PgSqlErrorCode::ERRCODE_INVALID_PARAMETER_VALUE, |_| {
        Err(DateTimeConversionError::UnknownTimezone(zone.to_string()))
    })
    .execute()
}

#[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
pub fn get_timezone_offset<Tz: AsRef<str>>(zone: Tz) -> Result<i32, DateTimeConversionError> {
    /*
     * Look up the requested time zone.  First we look in the time zone
     * abbreviation table (to handle cases like "EST"), and if that fails, we
     * look in the time zone database (to handle cases like
     * "America/New_York").  (This matches the order in which timestamp input
     * checks the cases; it's important because the time zone database unwisely
     * uses a few zone names that are identical to offset abbreviations.)
     */
    unsafe {
        let mut tz = 0;
        let tzname = alloc::ffi::CString::new(zone.as_ref()).unwrap();

        let mut val = 0;
        let mut tzp: *mut pg_tz = 0 as _;

        /* DecodeTimezoneAbbrev requires lowercase input */
        let lowzone =
            pg_sys::downcase_truncate_identifier(tzname.as_ptr(), zone.as_ref().len() as _, false);

        let tztype = pg_sys::DecodeTimezoneAbbrev(0, lowzone, &mut val, &mut tzp) as u32;

        pg_sys::pfree(lowzone.cast());

        if tztype == pg_sys::TZ || tztype == pg_sys::DTZ {
            /* fixed-offset abbreviation */
            tz = -val;
        } else if tztype == pg_sys::DYNTZ {
            /* dynamic-offset abbreviation, resolve using transaction start time */
            let now = pg_sys::GetCurrentTransactionStartTimestamp();
            let mut isdst = 0;

            tz = pg_sys::DetermineTimeZoneAbbrevOffsetTS(now, tzname.as_ptr(), tzp, &mut isdst);
        } else {
            /* try it as a full zone name */
            crate::warning!("tzname={}", tzname.to_str().unwrap());
            tzp = pg_sys::pg_tzset(tzname.as_ptr());
            if !tzp.is_null() {
                /* Get the offset-from-GMT that is valid now for the zone */
                let now = pg_sys::GetCurrentTransactionStartTimestamp();
                let mut tm = Default::default();
                let mut fsec = 0;

                if pg_sys::timestamp2tm(now, &mut tz, &mut tm, &mut fsec, std::ptr::null_mut(), tzp)
                    != 0
                {
                    return Err(DateTimeConversionError::FieldOverflow);
                }
            } else {
                return Err(DateTimeConversionError::UnknownTimezone(zone.as_ref().to_string()));
            }
        }
        Ok(-tz)
    }
}

pub(crate) struct DateTimeTypeVisitor<T>(PhantomData<T>);

impl<T> DateTimeTypeVisitor<T> {
    pub fn new() -> Self {
        DateTimeTypeVisitor(PhantomData)
    }
}

impl<'a, T: FromStr + seal::DateTimeType> serde::de::Visitor<'a> for DateTimeTypeVisitor<T> {
    type Value = T;

    fn expecting(&self, formatter: &mut alloc::fmt::Formatter) -> alloc::fmt::Result {
        formatter.write_str("a borrowed string")
    }

    fn visit_borrowed_str<E>(self, v: &'a str) -> Result<Self::Value, E>
    where
        E: serde::de::Error,
    {
        T::from_str(v).map_err(|_| {
            serde::de::Error::invalid_value(serde::de::Unexpected::Other("invalid value"), &self)
        })
    }

    fn visit_borrowed_bytes<E>(self, v: &'a [u8]) -> Result<Self::Value, E>
    where
        E: serde::de::Error,
    {
        let s = std::str::from_utf8(v)
            .map_err(|_| serde::de::Error::invalid_value(serde::de::Unexpected::Bytes(v), &self))?;
        self.visit_borrowed_str(s)
    }
}

#[derive(thiserror::Error, Debug, Clone, Copy, PartialEq, Eq)]
pub enum IntervalConversionError {
    #[error("duration's total month count outside of valid i32::MIN..=i32::MAX range")]
    DurationMonthsOutOfBounds,
    #[error("Interval parts must all have the same sign")]
    MismatchedSigns,
    #[error("Negative Intervals cannot be converted into Durations")]
    NegativeInterval,
    #[error("Interval overflows Duration's u64 micros constructor")]
    IntervalTooLarge,
}

/// A problem converting a value into a datetime format
#[derive(thiserror::Error, Debug, PartialEq, Eq)]
pub enum DateTimeConversionError {
    /// ERRCODE_DATETIME_FIELD_OVERFLOW
    #[error("Some part of the date or time is too large")]
    FieldOverflow,
    #[error("The date or time is not in the correct format")]
    InvalidFormat,
    #[error("`{0}` is not a known time zone")]
    UnknownTimezone(String),
    #[error("`{0} is not a valid time zone offset")]
    InvalidTimezoneOffset(i32),
    #[error("Encoded time zone string is unknown")]
    CannotParseTimezone,
    /// ERRCODE_DATETIME_VALUE_OUT_OF_RANGE
    #[error("The value is out-of-range for valid instances of this datetime type")]
    OutOfRange,
}
