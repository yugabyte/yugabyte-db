use std::ffi::{CStr, CString};

use pgrx::{
    datum::{Date, Interval, Time, TimeWithTimeZone, Timestamp, TimestampWithTimeZone},
    direct_function_call, pg_sys, AnyNumeric, IntoDatum,
};

pub(crate) const MAX_DECIMAL_PRECISION: usize = 38;

pub(crate) fn date_to_i32(date: Date) -> i32 {
    // PG epoch is (2000-01-01). Convert it to Unix epoch (1970-01-01). +10957 days
    let adjusted_date: Date = unsafe {
        direct_function_call(pg_sys::date_pli, &[date.into_datum(), 10957.into_datum()])
            .expect("cannot adjust PG date to Unix date")
    };

    let adjusted_date_as_bytes: Vec<u8> = unsafe {
        direct_function_call(pg_sys::date_send, &[adjusted_date.into_datum()])
            .expect("cannot convert date to bytes")
    };
    i32::from_be_bytes(
        adjusted_date_as_bytes[0..4]
            .try_into()
            .unwrap_or_else(|e| panic!("{}", e)),
    )
}

pub(crate) fn i32_to_date(i32_date: i32) -> Date {
    // Parquet uses Unix epoch (1970-01-01). Convert it to PG epoch (2000-01-01). -10957 days
    unsafe { Date::from_pg_epoch_days(i32_date - 10957) }
}

pub(crate) fn timestamp_to_i64(timestamp: Timestamp) -> i64 {
    // PG epoch is (2000-01-01). Convert it to Unix epoch (1970-01-01). +10957 days
    let adjustment_interval = Interval::from_days(10957);
    let adjusted_timestamp: Timestamp = unsafe {
        direct_function_call(
            pg_sys::timestamp_pl_interval,
            &[timestamp.into_datum(), adjustment_interval.into_datum()],
        )
        .expect("cannot adjust PG timestamp to Unix timestamp")
    };

    let adjusted_timestamp_as_bytes: Vec<u8> = unsafe {
        direct_function_call(pg_sys::time_send, &[adjusted_timestamp.into_datum()])
            .expect("cannot convert timestamp to bytes")
    };
    i64::from_be_bytes(
        adjusted_timestamp_as_bytes[0..8]
            .try_into()
            .unwrap_or_else(|e| panic!("{}", e)),
    )
}

pub(crate) fn i64_to_timestamp(i64_timestamp: i64) -> Timestamp {
    let timestamp: Timestamp = i64_timestamp.try_into().unwrap_or_else(|e| panic!("{}", e));

    // Parquet uses Unix epoch (1970-01-01). Convert it to PG epoch (2000-01-01). -10957 days
    let adjustment_interval = Interval::from_days(10957);
    let adjusted_timestamp: Timestamp = unsafe {
        direct_function_call(
            pg_sys::timestamp_mi_interval,
            &[timestamp.into_datum(), adjustment_interval.into_datum()],
        )
        .expect("cannot adjust Unix timestamp to PG timestamp")
    };

    adjusted_timestamp
}

pub(crate) fn timestamptz_to_i64(timestamptz: TimestampWithTimeZone) -> i64 {
    // PG epoch is (2000-01-01). Convert it to Unix epoch (1970-01-01). +10957 days
    let adjustment_interval = Interval::from_days(10957);
    let adjusted_timestamptz: TimestampWithTimeZone = unsafe {
        direct_function_call(
            pg_sys::timestamptz_pl_interval,
            &[timestamptz.into_datum(), adjustment_interval.into_datum()],
        )
        .expect("cannot adjust PG timestamptz to Unix timestamptz")
    };

    let adjusted_timestamptz_as_bytes: Vec<u8> = unsafe {
        direct_function_call(
            pg_sys::timestamptz_send,
            &[adjusted_timestamptz.into_datum()],
        )
        .expect("cannot convert timestamptz to bytes")
    };
    i64::from_be_bytes(
        adjusted_timestamptz_as_bytes[0..8]
            .try_into()
            .unwrap_or_else(|e| panic!("{}", e)),
    )
}

pub(crate) fn i64_to_timestamptz(i64_timestamptz: i64) -> TimestampWithTimeZone {
    let timestamptz: TimestampWithTimeZone = i64_timestamptz
        .try_into()
        .unwrap_or_else(|e| panic!("{}", e));

    // Parquet uses Unix epoch (1970-01-01). Convert it to PG epoch (2000-01-01). -10957 days
    let adjustment_interval = Interval::from_days(10957);
    let adjusted_timestamptz: TimestampWithTimeZone = unsafe {
        direct_function_call(
            pg_sys::timestamptz_mi_interval,
            &[timestamptz.into_datum(), adjustment_interval.into_datum()],
        )
        .expect("cannot adjust Unix timestamptz to PG timestamptz")
    };

    adjusted_timestamptz
}

pub(crate) fn time_to_i64(time: Time) -> i64 {
    let time_as_bytes: Vec<u8> = unsafe {
        direct_function_call(pg_sys::time_send, &[time.into_datum()])
            .expect("cannot convert time to bytes")
    };
    i64::from_be_bytes(
        time_as_bytes[0..8]
            .try_into()
            .unwrap_or_else(|e| panic!("{}", e)),
    )
}

pub(crate) fn i64_to_time(i64_time: i64) -> Time {
    i64_time.try_into().unwrap_or_else(|e| panic!("{}", e))
}

pub(crate) fn timetz_to_i64(timetz: TimeWithTimeZone) -> i64 {
    let timezone_as_secs: AnyNumeric = unsafe {
        direct_function_call(
            pg_sys::extract_timetz,
            &["timezone".into_datum(), timetz.into_datum()],
        )
    }
    .expect("cannot extract timezone from timetz");

    let timezone_as_secs: f64 = timezone_as_secs
        .try_into()
        .unwrap_or_else(|e| panic!("{}", e));

    let timezone_as_interval = Interval::from_seconds(timezone_as_secs);
    let adjusted_timetz: TimeWithTimeZone = unsafe {
        direct_function_call(
            pg_sys::timetz_mi_interval,
            &[timetz.into_datum(), timezone_as_interval.into_datum()],
        )
        .expect("cannot adjust PG timetz to Unix timetz")
    };

    let adjusted_timetz_as_bytes: Vec<u8> = unsafe {
        direct_function_call(pg_sys::timetz_send, &[adjusted_timetz.into_datum()])
            .expect("cannot convert timetz to bytes")
    };
    i64::from_be_bytes(
        adjusted_timetz_as_bytes[0..8]
            .try_into()
            .unwrap_or_else(|e| panic!("{}", e)),
    )
}

pub(crate) fn i64_to_timetz(i64_timetz: i64) -> TimeWithTimeZone {
    let utc_tz = 0;
    (i64_timetz, utc_tz)
        .try_into()
        .unwrap_or_else(|e| panic!("{}", e))
}

pub(crate) fn numeric_to_i128(numeric: AnyNumeric) -> i128 {
    // obtain numeric's string representation
    // cannot use numeric_send because byte representation is not compatible with parquet's decimal
    let numeric_str: &CStr = unsafe {
        direct_function_call(pg_sys::numeric_out, &[numeric.into_datum()])
            .expect("cannot convert numeric to bytes")
    };
    let numeric_str = numeric_str
        .to_str()
        .expect("numeric string is an invalid CString");

    let sign = if numeric_str.starts_with('-') { -1 } else { 1 };

    // remove sign as we already stored it. we also remove the decimal point
    // since arrow decimal expects a i128 representation of the decimal
    let numeric_str = numeric_str.replace(['-', '+', '.'], "");

    let numeric_digits = numeric_str
        .chars()
        .map(|c| c.to_digit(10).expect("not a valid digit") as i8);

    // convert digits into arrow decimal
    let mut decimal: i128 = 0;
    for digit in numeric_digits.into_iter() {
        decimal = decimal * 10 + digit as i128;
    }
    decimal *= sign;

    decimal
}

pub(crate) fn i128_to_numeric(i128_decimal: i128, scale: usize) -> AnyNumeric {
    let sign = if i128_decimal < 0 { "-" } else { "" };
    let i128_decimal = i128_decimal.abs();

    // calculate decimal digits
    let mut decimal_digits = vec![];
    let mut decimal = i128_decimal;
    while decimal > 0 {
        let digit = (decimal % 10) as i8;
        decimal_digits.push(digit);
        decimal /= 10;
    }

    // get fraction as string
    let fraction = decimal_digits
        .iter()
        .take(scale)
        .map(|v| v.to_string())
        .rev()
        .reduce(|acc, v| acc + &v)
        .unwrap_or_default();

    // get integral as string
    let integral = decimal_digits
        .iter()
        .skip(scale)
        .map(|v| v.to_string())
        .rev()
        .reduce(|acc, v| acc + &v)
        .unwrap_or_default();

    // create numeric string representation
    let numeric_str = if integral.is_empty() && fraction.is_empty() {
        "0".into()
    } else {
        format!("{}{}.{}", sign, integral, fraction)
    };

    // numeric_in would not validate the numeric string when typmod is -1
    let typmod = -1;

    // compute numeric from string representation
    let numeric: AnyNumeric = unsafe {
        let numeric_str = CString::new(numeric_str).expect("numeric cstring is invalid");

        direct_function_call(
            pg_sys::numeric_in,
            &[
                numeric_str.into_datum(),
                0.into_datum(),
                typmod.into_datum(),
            ],
        )
        .expect("cannot convert string to numeric")
    };

    numeric
}

// taken from PG's numeric.c
#[inline]
pub(crate) fn extract_precision_from_numeric_typmod(typmod: i32) -> usize {
    (((typmod - pg_sys::VARHDRSZ as i32) >> 16) & 0xffff) as usize
}

// taken from PG's numeric.c
#[inline]
pub(crate) fn extract_scale_from_numeric_typmod(typmod: i32) -> usize {
    ((((typmod - pg_sys::VARHDRSZ as i32) & 0x7ff) ^ 1024) - 1024) as usize
}
