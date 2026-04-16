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
    Date, DateTimeConversionError, DateTimeParts, DateTimeTypeVisitor, FromDatum,
    HasExtractableParts, Interval, IntoDatum, Time, TimestampWithTimeZone, ToIsoString,
};
use crate::{direct_function_call, pg_sys};
use pgrx_pg_sys::errcodes::PgSqlErrorCode;
use pgrx_pg_sys::PgTryBuilder;
use pgrx_sql_entity_graph::metadata::{
    ArgumentError, Returns, ReturnsError, SqlMapping, SqlTranslatable,
};
use std::num::TryFromIntError;

/// A safe wrapper around Postgres `TIMESTAMP WITHOUT TIME ZONE` type, backed by a [`pg_sys::Timestamp`] integer value.
#[derive(Debug, Copy, Clone)]
#[repr(transparent)]
pub struct Timestamp(pg_sys::Timestamp);

impl From<(Date, Time)> for Timestamp {
    fn from(value: (Date, Time)) -> Self {
        let (date, time) = value;
        Timestamp::new(
            date.year(),
            date.month(),
            date.day(),
            time.hour(),
            time.minute(),
            time.second(),
        )
        .unwrap()
    }
}

impl From<Date> for Timestamp {
    fn from(value: Date) -> Self {
        unsafe { direct_function_call(pg_sys::date_timestamp, &[value.into_datum()]).unwrap() }
    }
}

impl From<TimestampWithTimeZone> for Timestamp {
    fn from(value: TimestampWithTimeZone) -> Self {
        unsafe {
            direct_function_call(pg_sys::timestamptz_timestamp, &[value.into_datum()]).unwrap()
        }
    }
}

impl From<Timestamp> for pg_sys::Timestamp {
    #[inline]
    fn from(ts: Timestamp) -> Self {
        ts.0
    }
}

/// Create a [`Timestamp`] from a [`pg_sys::Timestamp`]
///
/// Note that `pg_sys::Timestamp` is an `i64` as a microsecond offset from the "Postgres epoch".
/// This impl currently allows producing a `Timestamp` that cannot be constructed by SQL,
/// such as a timestamp before "Julian day zero".
///
/// The details of the encoding may also prove surprising, for instance:
/// - Postgres uses Julian days to reason about time instead of the Gregorian calendar
/// - There is no "year zero", so Postgres defines the offset of -2000 years as 1 BC instead
/// - Some values such as `i64::MIN` and `i64::MAX` have special meanings as infinities
/// - The way `Timestamp` translates to/from `TimestampWithTimeZone`, or rather doesn't, is "fun"
/// - In the past `pg_sys::Timestamp`'s type has changed, so in the future this may change with it
/// - This is likely to coincide with a change in Timestamp's precision
impl TryFrom<pg_sys::Timestamp> for Timestamp {
    type Error = pg_sys::Timestamp;
    #[inline]
    fn try_from(ts: pg_sys::Timestamp) -> Result<Self, Self::Error> {
        // defined by Postgres, but bindgen doesn't evaluate them for some reason:
        const MIN_TIMESTAMP: pg_sys::Timestamp = -211_813_488_000_000_000;
        const END_TIMESTAMP: pg_sys::Timestamp = 9_223_371_331_200_000_000;
        //  #define IS_VALID_TIMESTAMP(t)  (MIN_TIMESTAMP <= (t) && (t) < END_TIMESTAMP)
        const MAX_TIMESTAMP: pg_sys::Timestamp = END_TIMESTAMP - 1;
        match ts {
            i64::MIN | i64::MAX | MIN_TIMESTAMP..=MAX_TIMESTAMP => Ok(Timestamp(ts)),
            _ => Err(ts),
        }
    }
}

impl TryFrom<pg_sys::Datum> for Timestamp {
    type Error = TryFromIntError;

    fn try_from(datum: pg_sys::Datum) -> Result<Self, Self::Error> {
        pg_sys::Timestamp::try_from(datum.value() as isize).map(Timestamp)
    }
}

impl IntoDatum for Timestamp {
    fn into_datum(self) -> Option<pg_sys::Datum> {
        Some(pg_sys::Datum::from(self.0))
    }
    fn type_oid() -> pg_sys::Oid {
        pg_sys::TIMESTAMPOID
    }
}

impl FromDatum for Timestamp {
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

impl Timestamp {
    const NEG_INFINITY: pg_sys::Timestamp = pg_sys::Timestamp::MIN;
    const INFINITY: pg_sys::Timestamp = pg_sys::Timestamp::MAX;

    /// Construct a new [`Timestamp`] from its constituent parts.
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
                pg_sys::make_timestamp,
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

    /// Construct a new [`Timestamp`] from its constituent parts.
    ///
    /// Elides the overhead of trapping errors for out-of-bounds parts
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
                pg_sys::make_timestamp,
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

    pub fn saturating_from_raw(value: pg_sys::Timestamp) -> Self {
        match Self::try_from(value) {
            Ok(ts) => ts,
            Err(int) if int.is_negative() => Timestamp(Self::NEG_INFINITY),
            Err(_int) => Timestamp(Self::INFINITY),
        }
    }

    /// Construct a new [`Timestamp`] representing positive infinity
    pub fn positive_infinity() -> Self {
        Self(Self::INFINITY)
    }

    /// Construct a new [`Timestamp`] representing negative infinity
    pub fn negative_infinity() -> Self {
        Self(Self::NEG_INFINITY)
    }

    /// Does this [`Timestamp`] represent positive infinity?
    #[inline]
    pub fn is_infinity(&self) -> bool {
        self.0 == Self::INFINITY
    }

    /// Does this [`Timestamp`] represent negative infinity?
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

    pub fn is_finite(&self) -> bool {
        !matches!(self.0, pg_sys::Timestamp::MIN | pg_sys::Timestamp::MAX)
    }

    /// Truncate [`Timestamp`] to specified units
    pub fn truncate(self, units: DateTimeParts) -> Self {
        unsafe {
            direct_function_call(pg_sys::timestamp_trunc, &[units.into_datum(), self.into_datum()])
                .unwrap()
        }
    }

    /// Subtract `other` from `self`, producing a “symbolic” result that uses years and months, rather than just days
    pub fn age(&self, other: &Timestamp) -> Interval {
        unsafe {
            direct_function_call(pg_sys::timestamp_age, &[self.into_datum(), other.into_datum()])
                .unwrap()
        }
    }

    /// Return the backing [`pg_sys::Timestamp`] value.
    #[inline]
    pub fn into_inner(self) -> pg_sys::Timestamp {
        self.0
    }
}

impl serde::Serialize for Timestamp {
    /// Serialize this [`Timestamp`] in ISO form, compatible with most JSON parsers
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

impl<'de> serde::Deserialize<'de> for Timestamp {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::de::Deserializer<'de>,
    {
        deserializer.deserialize_str(DateTimeTypeVisitor::<Self>::new())
    }
}

unsafe impl SqlTranslatable for crate::datum::Timestamp {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("timestamp"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("timestamp")))
    }
}
