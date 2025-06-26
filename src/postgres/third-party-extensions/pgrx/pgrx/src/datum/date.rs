//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use core::num::TryFromIntError;
use pgrx_pg_sys::errcodes::PgSqlErrorCode;
use pgrx_pg_sys::PgTryBuilder;
use pgrx_sql_entity_graph::metadata::{
    ArgumentError, Returns, ReturnsError, SqlMapping, SqlTranslatable,
};

use super::datetime_support::{DateTimeParts, HasExtractableParts};
use crate::datum::{
    DateTimeConversionError, DateTimeTypeVisitor, FromDatum, IntoDatum, Timestamp,
    TimestampWithTimeZone, ToIsoString,
};
use crate::{direct_function_call, pg_sys};

const JULIAN_DAY_ZERO: i32 = pg_sys::DATETIME_MIN_JULIAN as i32 - POSTGRES_EPOCH_JDATE;
const LAST_JULIAN_DAY: i32 = pg_sys::DATE_END_JULIAN as i32 - POSTGRES_EPOCH_JDATE - 1;
const LT_JULIAN_DAY_ZERO: i32 = JULIAN_DAY_ZERO - 1;
const GT_LAST_JULIAN_DAY: i32 = LAST_JULIAN_DAY + 1;

pub const POSTGRES_EPOCH_JDATE: i32 = pg_sys::POSTGRES_EPOCH_JDATE as i32;
pub const UNIX_EPOCH_JDATE: i32 = pg_sys::UNIX_EPOCH_JDATE as i32;

/// A safe wrapper around Postgres `DATE` type, backed by a [`pg_sys::DateADT`] integer value.
#[derive(Debug, Copy, Clone)]
#[repr(transparent)]
pub struct Date(pg_sys::DateADT);

impl From<Date> for pg_sys::DateADT {
    #[inline]
    fn from(value: Date) -> Self {
        value.0
    }
}

impl From<Timestamp> for Date {
    fn from(value: Timestamp) -> Self {
        unsafe { direct_function_call(pg_sys::timestamp_date, &[value.into_datum()]).unwrap() }
    }
}

impl From<TimestampWithTimeZone> for Date {
    fn from(value: TimestampWithTimeZone) -> Self {
        unsafe { direct_function_call(pg_sys::timestamptz_date, &[value.into_datum()]).unwrap() }
    }
}

/// Create a [`Date`] from a [`pg_sys::DateADT`]
///
/// Note that [`pg_sys::DateADT`] is an `i32` as a Julian day offset from the "Postgres epoch".
///
/// The details of the encoding may also prove surprising, for instance:
/// - It is not a Gregorian calendar date, but rather a Julian day
/// - Despite having the numerical range for it, it does not support values before Julian day 0
///   (4713 BC, January 1), nor does it support many values far into the future.
/// - There is no "year zero" in either the Julian or the Gregorian calendars,
///   so you may have to account for a "skip" from 1 BC to 1 AD
/// - Some values such as `i32::MIN` and `i32::MAX` have special meanings as infinities
impl TryFrom<pg_sys::DateADT> for Date {
    type Error = DateTimeConversionError;
    #[inline]
    fn try_from(value: pg_sys::DateADT) -> Result<Self, Self::Error> {
        match value {
            i32::MIN | i32::MAX | JULIAN_DAY_ZERO..=LAST_JULIAN_DAY => Ok(Date(value)),
            // these aren't quite overflows, semantically...
            ..=LT_JULIAN_DAY_ZERO => Err(DateTimeConversionError::OutOfRange),
            GT_LAST_JULIAN_DAY.. => Err(DateTimeConversionError::OutOfRange),
        }
    }
}

impl TryFrom<pg_sys::Datum> for Date {
    type Error = TryFromIntError;

    #[inline]
    fn try_from(datum: pg_sys::Datum) -> Result<Self, Self::Error> {
        Ok(Date(datum.value() as pg_sys::DateADT))
    }
}

impl FromDatum for Date {
    #[inline]
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
            Some(datum.try_into().expect("Error converting date datum"))
        }
    }
}

impl IntoDatum for Date {
    fn into_datum(self) -> Option<pg_sys::Datum> {
        Some(pg_sys::Datum::from(self.0))
    }
    fn type_oid() -> pg_sys::Oid {
        pg_sys::DATEOID
    }
}

impl Date {
    const NEG_INFINITY: pg_sys::DateADT = pg_sys::DateADT::MIN;
    const INFINITY: pg_sys::DateADT = pg_sys::DateADT::MAX;

    /// Construct a new [`Date`] from its constituent parts.
    ///
    /// # Errors
    ///
    /// Returns a [`DateTimeConversionError`] if any of the specified parts don't fit within
    /// the bounds of a standard date.
    pub fn new(year: i32, month: u8, day: u8) -> Result<Self, DateTimeConversionError> {
        let month: i32 = month as _;
        let day: i32 = day as _;

        PgTryBuilder::new(|| unsafe {
            Ok(direct_function_call(
                pg_sys::make_date,
                &[year.into_datum(), month.into_datum(), day.into_datum()],
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

    /// Construct a new [`Date`] from its constituent parts.
    ///
    /// This function elides the error trapping overhead in the event of out-of-bounds parts.
    ///
    /// # Panics
    ///
    /// This function will panic, aborting the current transaction, if any part is out-of-bounds.
    pub fn new_unchecked(year: isize, month: u8, day: u8) -> Self {
        let year: i32 = year.try_into().expect("invalid year");
        let month: i32 = month.try_into().expect("invalid month");
        let day: i32 = day.try_into().expect("invalid day");

        unsafe {
            direct_function_call(
                pg_sys::make_date,
                &[year.into_datum(), month.into_datum(), day.into_datum()],
            )
            .unwrap()
        }
    }

    /// From Date's raw encoding type (`i32`), construct a valid in-range Date by saturating to
    /// the nearest `infinity` if out-of-bounds.
    #[inline]
    pub fn saturating_from_raw(date_int: pg_sys::DateADT) -> Date {
        match date_int {
            ..=LT_JULIAN_DAY_ZERO => Date(i32::MIN),
            JULIAN_DAY_ZERO..=LAST_JULIAN_DAY => Date(date_int),
            GT_LAST_JULIAN_DAY.. => Date(i32::MAX),
        }
    }

    /// Construct a new [`Date`] representing positive infinity
    pub fn positive_infinity() -> Self {
        Self(Self::INFINITY)
    }

    /// Construct a new [`Date`] representing negative infinity
    pub fn negative_infinity() -> Self {
        Self(Self::NEG_INFINITY)
    }

    /// Create a new [`Date`] from an integer value from Postgres' epoch, in days.
    ///
    /// # Safety
    ///
    /// This function is unsafe as you must guarantee `pg_epoch_days` is valid.  You'll always
    /// get a fully constructed [`Date`] in return, but it may not be something Postgres actually
    /// understands.
    #[inline]
    pub unsafe fn from_pg_epoch_days(pg_epoch_days: i32) -> Date {
        Date(pg_epoch_days)
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

    /// Does this [`Date`] represent positive infinity?
    #[inline]
    pub fn is_infinity(&self) -> bool {
        self.0 == Self::INFINITY
    }

    /// Does this [`Date`] represent negative infinity?
    #[inline]
    pub fn is_neg_infinity(&self) -> bool {
        self.0 == Self::NEG_INFINITY
    }

    /// Return the Julian days value of this [`Date`]
    #[inline]
    pub fn to_julian_days(&self) -> i32 {
        self.0 + POSTGRES_EPOCH_JDATE
    }

    /// Return the Postgres epoch days value of this [`Date`]
    #[inline]
    pub fn to_pg_epoch_days(&self) -> i32 {
        self.0
    }

    /// Returns the date as an i32 representing the elapsed time since UNIX epoch in days
    #[inline]
    pub fn to_unix_epoch_days(&self) -> i32 {
        self.0 + POSTGRES_EPOCH_JDATE - UNIX_EPOCH_JDATE
    }

    /// Return the date as a stack-allocated [`libc::time_t`] instance
    #[inline]
    pub fn to_posix_time(&self) -> libc::time_t {
        let secs_per_day: libc::time_t =
            pg_sys::SECS_PER_DAY.try_into().expect("couldn't fit time into time_t");
        libc::time_t::from(self.to_unix_epoch_days()) * secs_per_day
    }

    pub fn is_finite(&self) -> bool {
        !matches!(self.0, pg_sys::DateADT::MIN | pg_sys::DateADT::MAX)
    }

    /// Return the backing [`pg_sys::DateADT`] value.
    #[inline]
    pub fn into_inner(self) -> pg_sys::DateADT {
        self.0
    }
}

impl serde::Serialize for Date {
    /// Serialize this [`Date`] in ISO form, compatible with most JSON parsers
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

impl<'de> serde::Deserialize<'de> for Date {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::de::Deserializer<'de>,
    {
        deserializer.deserialize_str(DateTimeTypeVisitor::<Self>::new())
    }
}

unsafe impl SqlTranslatable for Date {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("date"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("date")))
    }
}
