use fake::Fake;
use fake::faker::chrono::raw::*;
use fake::faker::number::raw::*;
use fake::locales::EN;

use pgrx::Range;
#[allow(unused_imports)]
use pgrx::prelude::*;
use rand::Rng;
use std::str::FromStr;

//----------------------------------------------------------------------------
// Range Conversions
//----------------------------------------------------------------------------

/// Convert a pgrx::Range<AnyNumeric> into a Rust Range::<f32>
/// /!\ unbounded range are not allowed
fn range_f32(r: Range<pgrx::AnyNumeric>) -> Option<core::ops::Range<f32>> {
    let s = &r.lower()?;
    let e = &r.upper()?;
    if r.is_infinite() {
        return None;
    }
    let s_f32 = f32::try_from((*s.get()?).clone()).expect("Conversion error");
    let e_f32 = f32::try_from((*e.get()?).clone()).expect("Conversion error");
    Some(core::ops::Range::<f32> {
        start: s_f32,
        end: e_f32,
    })
}

/// Convert a pgrx::Range<AnyNumeric> into a Rust Range::<f64>
/// /!\ unbounded range are not allowed
fn range_f64(r: Range<pgrx::AnyNumeric>) -> Option<core::ops::Range<f64>> {
    let s = &r.lower()?;
    let e = &r.upper()?;
    if r.is_infinite() {
        return None;
    }
    let s_f64 = f64::try_from((*s.get()?).clone()).expect("Conversion error");
    let e_f64 = f64::try_from((*e.get()?).clone()).expect("Conversion error");
    Some(core::ops::Range::<f64> {
        start: s_f64,
        end: e_f64,
    })
}

/// Convert a pgrx::Range<i32> into a Rust Range::<usize>
/// /!\ unbounded range are not allowed
fn range_usize(r: Range<i32>) -> Option<core::ops::Range<usize>> {
    if r.is_infinite() {
        return None;
    }
    Some(core::ops::Range::<usize> {
        start: *r.lower()?.get()? as usize,
        end: *r.upper()?.get()? as usize,
    })
}

/// Convert a pgrx::Range<i32> into a Rust Range::<usize>
/// /!\ unbounded range are not allowed
fn range_isize(r: Range<i32>) -> Option<core::ops::Range<isize>> {
    if r.is_infinite() {
        return None;
    }
    Some(core::ops::Range::<isize> {
        start: *r.lower()?.get()? as isize,
        end: *r.upper()?.get()? as isize,
    })
}

/// Convert a pgrx::Range<i64> into a Rust Range::<isize>
/// /!\ unbounded range are not allowed
fn range_isize_from_i64(r: Range<i64>) -> Option<core::ops::Range<isize>> {
    if r.is_infinite() {
        return None;
    }
    Some(core::ops::Range::<isize> {
        start: *r.lower()?.get()? as isize,
        end: *r.upper()?.get()? as isize,
    })
}

//----------------------------------------------------------------------------
// Time
//----------------------------------------------------------------------------

// Currently there's no way to simply convert a chrono date into a
// pgrx::datum::Date. So we use the String representation of the date as an
// intermediary format.
// but this may change in the near future
// https://github.com/pgcentralfoundation/pgrx/pull/1603
//

// We can't use `fake::faker::chrono::raw::Date(EN).fake()`
// because fake-rs generates dates with year 0 (e.g. 0000-10-14)
// and Postgres does not accept that
// https://github.com/cksac/fake-rs/issues/177
pub fn date() -> pgrx::datum::TimestampWithTimeZone {
    let day_one = "1900-01-01T00:00:00+00:00";
    let d = chrono::DateTime::parse_from_rfc3339(day_one).unwrap();
    let val: String = DateTimeAfter(EN, d.into()).fake();
    pgrx::datum::TimestampWithTimeZone::from_str(&val).unwrap()
}

/*
pub fn date_after(t: pgrx::datum::TimestampWithTimeZone)
    -> pgrx::datum::TimestampWithTimeZone
{
    let s: String = t.to_string();
    let d = chrono::DateTime::parse_from_rfc3339(&s)
            .expect("DateTime conversion failed");
    let val: String = DateTimeAfter(EN,d.into()).fake();
    pgrx::datum::TimestampWithTimeZone::from_str(&val).unwrap()
}

pub fn date_before(t: pgrx::datum::TimestampWithTimeZone)
    -> pgrx::datum::TimestampWithTimeZone
{
    let s: String = t.to_string();
    let d = chrono::DateTime::parse_from_rfc3339(&s)
            .expect("DateTime conversion failed");
    let val: String = DateTimeBefore(EN,d.into()).fake();
    pgrx::datum::TimestampWithTimeZone::from_str(&val).unwrap()
}
*/

pub fn time() -> pgrx::datum::Time {
    let val: String = fake::faker::chrono::raw::Time(EN).fake();
    pgrx::datum::Time::from_str(&val).unwrap()
}

//----------------------------------------------------------------------------
// Numbers
//----------------------------------------------------------------------------

pub fn bigint(r: Range<i64>) -> Option<i64> {
    Some(i64::try_from(range_isize_from_i64(r)?.fake::<isize>()).expect("Out of Bound"))
}

pub fn double_precision(r: Range<pgrx::AnyNumeric>) -> Option<f64> {
    Some(range_f64(r)?.fake::<f64>())
}

pub fn int(r: Range<i32>) -> Option<i32> {
    Some(i32::try_from(range_isize(r)?.fake::<isize>()).expect("Out of Bound"))
}

pub fn number_with_format(format: String) -> String {
    NumberWithFormat(EN, &format).fake()
}

pub fn numeric(r: Range<pgrx::AnyNumeric>) -> Option<pgrx::AnyNumeric> {
    Some(pgrx::AnyNumeric::try_from(range_f64(r)?.fake::<f64>()).expect("Out of Bound"))
}

pub fn real(r: Range<pgrx::AnyNumeric>) -> Option<f32> {
    Some(range_f32(r)?.fake::<f32>())
}

//----------------------------------------------------------------------------
// Strings
//----------------------------------------------------------------------------

pub fn string(r: Range<i32>) -> Option<String> {
    Some(range_usize(r)?.fake::<String>())
}

//----------------------------------------------------------------------------
// GPS Coordinates using the Point type
//----------------------------------------------------------------------------

pub fn random_gps_coordinates() -> pgrx::pg_sys::Point {
    // This rounding method has problems with big and tiny numbers. For GPS
    // coordinates, it shouldn't matter since it's clamped between (-180° and
    // 180°) and GPS don't have infinite precision. It also has lots of rounding
    // errors but we don't care since we're creating random points.
    let x = (rand::thread_rng().gen_range(-90.0..90.0) * 1e6_f64).round() / 1e6;
    let y = (rand::thread_rng().gen_range(-180.00..180.0) * 1e6_f64).round() / 1e6;
    pgrx::pg_sys::Point { x, y }
}

//----------------------------------------------------------------------------
// Points
//----------------------------------------------------------------------------

pub fn random_point_in_box(rect: pgrx::pg_sys::BOX) -> pgrx::pg_sys::Point {
    // Any two opposite corners can be supplied on input of a box, but the values
    // will be reordered by PostgreSQL as needed to store the upper right and lower
    // left corners, in that order.
    let x = rand::thread_rng().gen_range(rect.low.x..rect.high.x);
    let y = rand::thread_rng().gen_range(rect.low.y..rect.high.y);
    pgrx::pg_sys::Point { x, y }
}

//----------------------------------------------------------------------------
// Tests
//----------------------------------------------------------------------------

#[allow(unused_imports)]
use pgrx::prelude::*;

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use crate::random::*;

    #[pg_test]
    fn test_int() {
        assert!(int(pgrx::Range::<i32>::new(1, 10)).is_some());
        assert!(int(pgrx::Range::<i32>::new(-10, -1)).is_some());
        assert_eq!(int(pgrx::Range::<i32>::new(1, 2)), Some(1));
        assert!(int(pgrx::Range::<i32>::new(None, 10)).is_none());
        assert!(int(pgrx::Range::<i32>::new(1, None)).is_none());
        assert!(int(pgrx::Range::<i32>::new(None, None)).is_none());
    }

    #[pg_test]
    fn test_string() {
        assert!(string(pgrx::Range::<i32>::new(1, 10)).is_some());
        assert!(string(pgrx::Range::<i32>::new(None, 10)).is_none());
        assert!(string(pgrx::Range::<i32>::new(1, None)).is_none());
        assert!(string(pgrx::Range::<i32>::new(None, None)).is_none());
    }

    #[pg_test]
    fn test_bigint() {
        assert!(bigint(pgrx::Range::<i64>::new(1, 10)).is_some());
        assert!(bigint(pgrx::Range::<i64>::new(-10, -1)).is_some());
        assert!(bigint(pgrx::Range::<i64>::new(None, 10)).is_none());
        assert!(bigint(pgrx::Range::<i64>::new(1, None)).is_none());
        assert!(bigint(pgrx::Range::<i64>::new(None, None)).is_none());
    }

    #[pg_test]
    fn test_range_f32() {
        let one = pgrx::AnyNumeric::from(1);
        let two = pgrx::AnyNumeric::from(2);
        let six = pgrx::AnyNumeric::from(6);
        let ten = pgrx::AnyNumeric::from(10);
        let minus_one = pgrx::AnyNumeric::from(-1);
        let minus_ten = pgrx::AnyNumeric::from(-10);
        assert!(range_f32(pgrx::Range::<pgrx::AnyNumeric>::new(one, ten)).is_some());
        assert!(range_f32(pgrx::Range::<pgrx::AnyNumeric>::new(minus_one, minus_ten)).is_some());
        assert!(range_f32(pgrx::Range::<pgrx::AnyNumeric>::new(None, six)).is_none());
        assert!(range_f32(pgrx::Range::<pgrx::AnyNumeric>::new(two, None)).is_none());
        assert!(double_precision(pgrx::Range::<pgrx::AnyNumeric>::new(None, None)).is_none());
    }

    #[pg_test]
    fn test_range_f64() {
        let one = pgrx::AnyNumeric::from(1);
        let two = pgrx::AnyNumeric::from(2);
        let six = pgrx::AnyNumeric::from(6);
        let ten = pgrx::AnyNumeric::from(10);
        let minus_one = pgrx::AnyNumeric::from(-1);
        let minus_ten = pgrx::AnyNumeric::from(-10);
        assert!(range_f64(pgrx::Range::<pgrx::AnyNumeric>::new(one, ten)).is_some());
        assert!(range_f64(pgrx::Range::<pgrx::AnyNumeric>::new(minus_one, minus_ten)).is_some());
        assert!(range_f64(pgrx::Range::<pgrx::AnyNumeric>::new(None, six)).is_none());
        assert!(range_f64(pgrx::Range::<pgrx::AnyNumeric>::new(two, None)).is_none());
        assert!(range_f64(pgrx::Range::<pgrx::AnyNumeric>::new(None, None)).is_none());
    }

    #[pg_test]
    fn test_double_precision() {
        let one = pgrx::AnyNumeric::from(1);
        let two = pgrx::AnyNumeric::from(2);
        let six = pgrx::AnyNumeric::from(6);
        let ten = pgrx::AnyNumeric::from(10);
        let minus_one = pgrx::AnyNumeric::from(-1);
        let minus_ten = pgrx::AnyNumeric::from(-10);
        assert!(double_precision(pgrx::Range::<pgrx::AnyNumeric>::new(one, ten)).is_some());
        assert!(
            double_precision(pgrx::Range::<pgrx::AnyNumeric>::new(minus_ten, minus_one)).is_some()
        );
        assert!(double_precision(pgrx::Range::<pgrx::AnyNumeric>::new(None, six)).is_none());
        assert!(double_precision(pgrx::Range::<pgrx::AnyNumeric>::new(two, None)).is_none());
        assert!(double_precision(pgrx::Range::<pgrx::AnyNumeric>::new(None, None)).is_none());
    }

    #[pg_test]
    fn test_numeric() {
        let one = pgrx::AnyNumeric::from(1);
        let two = pgrx::AnyNumeric::from(2);
        let six = pgrx::AnyNumeric::from(6);
        let ten = pgrx::AnyNumeric::from(10);
        let minus_one = pgrx::AnyNumeric::from(-1);
        let minus_ten = pgrx::AnyNumeric::from(-10);
        assert!(numeric(pgrx::Range::<pgrx::AnyNumeric>::new(one, ten)).is_some());
        assert!(numeric(pgrx::Range::<pgrx::AnyNumeric>::new(minus_ten, minus_one)).is_some());
        assert!(numeric(pgrx::Range::<pgrx::AnyNumeric>::new(None, six)).is_none());
        assert!(numeric(pgrx::Range::<pgrx::AnyNumeric>::new(two, None)).is_none());
        assert!(numeric(pgrx::Range::<pgrx::AnyNumeric>::new(None, None)).is_none());
    }

    #[pg_test]
    fn test_real() {
        let one = pgrx::AnyNumeric::from(1);
        let two = pgrx::AnyNumeric::from(2);
        let six = pgrx::AnyNumeric::from(6);
        let ten = pgrx::AnyNumeric::from(10);
        let minus_one = pgrx::AnyNumeric::from(-1);
        let minus_ten = pgrx::AnyNumeric::from(-10);
        assert!(real(pgrx::Range::<pgrx::AnyNumeric>::new(one, ten)).is_some());
        assert!(real(pgrx::Range::<pgrx::AnyNumeric>::new(minus_ten, minus_one)).is_some());
        assert!(real(pgrx::Range::<pgrx::AnyNumeric>::new(None, six)).is_none());
        assert!(real(pgrx::Range::<pgrx::AnyNumeric>::new(two, None)).is_none());
        assert!(real(pgrx::Range::<pgrx::AnyNumeric>::new(None, None)).is_none());
    }

    #[pg_test]
    fn test_date() {
        assert!(!date().to_string().is_empty());
    }

    /*
        #[pg_test]
        #[ignore]
        fn test_date_after() {
            let t = pgrx::datum::TimestampWithTimeZone::from_str("1977-03-20 04:42:00 PDT").unwrap();
            assert!(!date_after(t).to_string().is_empty());
        }

        #[pg_test]
        #[ignore]
        fn test_date_before() {
            let t = pgrx::datum::TimestampWithTimeZone::from_str("1977-03-20 04:42:00 PDT").unwrap();
            assert!(!date_before(t).to_string().is_empty());
        }
    */

    #[pg_test]
    fn test_time() {
        assert!(!time().to_string().is_empty());
    }

    #[pg_test]
    fn test_number_with_format() {
        assert_eq!(number_with_format("###-###".to_string()).len(), 7);
    }
}
