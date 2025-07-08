use super::Complex;
use pgrx::datum::Date;
use rand::distr::{Alphanumeric, StandardUniform};
use rand::Rng;

#[derive(pgrx::PostgresType, Clone, PartialEq, Eq, Debug, serde::Serialize, serde::Deserialize)]
pub struct RandomData {
    i: u64,
    s: String,
    a: Vec<Date>,
}

impl RandomData {
    fn random() -> Self {
        RandomData {
            i: rand::random(),
            s: rand::rng()
                .sample_iter(Alphanumeric)
                .take(rand::rng().random_range(0..=1000))
                .map(char::from)
                .collect(),
            a: rand::rng()
                .sample_iter(StandardUniform)
                .take(rand::rng().random_range(0..=1000))
                .map(|_: u32| {
                    Date::new(
                        rand::rng().random_range(1..=3000),
                        rand::rng().random_range(1..=12),
                        rand::rng().random_range(1..=28),
                    )
                    .unwrap()
                })
                .collect(),
        }
    }
}

#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    use std::error::Error;
    use std::ffi::CStr;
    use std::str::FromStr;

    use pgrx::pg_sys::BuiltinOid;
    use pgrx::prelude::*;
    use pgrx::Uuid;

    use super::Complex;
    #[allow(unused_imports)]
    use crate as pgrx_tests;

    use super::RandomData;

    macro_rules! roundtrip {
        ($fname:ident, $tname:ident, &$lt:lifetime $rtype:ty, $expected:expr) => {
            #[pg_extern(requires = [ "create_complex_type" ])] // the "Complex" type comes from another crate, and we need its schema fully created before it can be used here
            fn $fname<$lt>(i: &$lt $rtype) -> &$lt $rtype {
                i
            }

            roundtrip_test!($fname, $tname, &'_ $rtype, $expected);
        };
        ($fname:ident, $tname:ident, $rtype:ty, $expected:expr) => {
            #[pg_extern(requires = [ "create_complex_type" ])] // the "Complex" type comes from another crate, and we need its schema fully created before it can be used here
            fn $fname(i: $rtype) -> $rtype {
                i
            }

            roundtrip_test!($fname, $tname, $rtype, $expected);
        };
        ($fname:ident, $tname:ident, $itype:ty, $input:expr, $otype:ty, $output:expr) => {
            #[pg_extern(requires = [ "create_complex_type" ])] // the "Complex" type comes from another crate, and we need its schema fully created before it can be used here
            fn $fname(i: $itype) -> $otype {
                i.into()
            }

            roundtrip_test!($fname, $tname, $itype, $input, $otype, $output);
        };
    }

    macro_rules! roundtrip_test {
        ($fname:ident, $tname:ident, $rtype:ty, $expected:expr) => {
            #[pg_test]
            fn $tname() -> Result<(), Box<dyn Error>> {
                let value: $rtype = $expected;
                let expected: $rtype = Clone::clone(&value);
                let result: $rtype = Spi::get_one_with_args(
                    &format!("SELECT {}($1)", stringify!(tests.$fname)),
                    &[value.into()],
                )?
                .unwrap();

                assert_eq!(result, expected);
                Ok(())
            }
        };
        ($fname:ident, $tname:ident, $itype:ty, $input:expr, $otype:ty, $output:expr) => {
            #[pg_test]
            fn $tname() -> Result<(), Box<dyn Error>> {
                let input: $itype = $input;
                let output: $otype = $output;
                let result: $otype = Spi::get_one_with_args(
                    &format!("SELECT {}($1)", stringify!(tests.$fname)),
                    &[input.into()],
                )?
                .unwrap();

                assert_eq!(result, output);
                Ok(())
            }
        };
    }

    roundtrip!(rt_bytea, test_rt_bytea, &'a [u8], [b'a', b'b', b'c'].as_slice());
    roundtrip!(rt_char_0, test_rt_char_0, char, 'a');
    roundtrip!(rt_char_1, test_rt_char_1, char, 'ß');
    roundtrip!(rt_char_2, test_rt_char_2, char, 'ℝ');
    roundtrip!(rt_char_3, test_rt_char_3, char, '💣');
    roundtrip!(rt_char_4, test_rt_char_4, char, 'a', String, "a".to_owned());
    roundtrip!(rt_char_5, test_rt_char_5, char, 'ß', String, "ß".to_owned());
    roundtrip!(rt_char_6, test_rt_char_6, char, 'ℝ', String, "ℝ".to_owned());
    roundtrip!(rt_char_7, test_rt_char_7, char, '💣', String, "💣".to_owned());
    roundtrip!(rt_i8, test_rt_i8, i8, i8::MAX);
    roundtrip!(rt_point, test_rt_point, pg_sys::Point, pg_sys::Point { x: 1.0, y: 2.0 });
    roundtrip!(rt_string, test_rt_string, String, String::from("string"));
    roundtrip!(rt_oid, test_rt_oid, pg_sys::Oid, pg_sys::Oid::from(BuiltinOid::ANYOID));
    roundtrip!(rt_xid, test_rt_xid, pg_sys::TransactionId, pg_sys::TransactionId::FIRST_NORMAL);
    roundtrip!(rt_i16, test_rt_i16, i16, i16::MAX);
    roundtrip!(rt_f64, test_rt_f64, f64, f64::MAX);
    roundtrip!(
        rt_box,
        test_rt_box,
        pg_sys::BOX,
        pg_sys::BOX {
            high: pg_sys::Point { x: 1.0, y: 2.0 },
            low: pg_sys::Point { x: 3.0, y: 4.0 }
        }
    );
    roundtrip!(rt_i64, test_rt_i64, i64, i64::MAX);
    roundtrip!(rt_i32, test_rt_i32, i32, i32::MAX);
    roundtrip!(rt_refstr, test_rt_refstr, &'a str, "foo");
    roundtrip!(rt_bool, test_rt_bool, bool, true);
    roundtrip!(rt_f32, test_rt_f32, f32, f32::MAX);
    roundtrip!(rt_numeric, test_rt_numeric, Numeric<100,0>, Numeric::from_str("31241234123412341234").unwrap());
    roundtrip!(
        rt_anynumeric,
        test_rt_anynumeric,
        AnyNumeric,
        AnyNumeric::from_str("31241234123412341234").unwrap()
    );
    roundtrip!(rt_cstr, test_rt_cstr, &'a CStr, c"&cstr");

    roundtrip!(rt_date, test_rt_date, Date, Date::from_str("1977-03-20").unwrap());
    roundtrip!(rt_ts, test_rt_ts, Timestamp, Timestamp::from_str("1977-03-20 04:42:00").unwrap());
    roundtrip!(
        rt_tstz,
        test_rt_tstz,
        TimestampWithTimeZone,
        TimestampWithTimeZone::from_str("1977-03-20 04:42:00 PDT").unwrap()
    );
    roundtrip!(rt_time, test_rt_time, Time, Time::from_str("04:42:00").unwrap());
    roundtrip!(
        rt_timetz,
        test_rt_timetz,
        TimeWithTimeZone,
        TimeWithTimeZone::from_str("04:42:00 PDT").unwrap()
    );
    roundtrip!(rt_interval, test_rt_interval, Interval, Interval::new(4, 2, 420).unwrap());

    roundtrip!(
        rt_uuid,
        test_rt_uuid,
        Uuid,
        Uuid::from_bytes([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15])
    );

    roundtrip!(rt_random_data, test_rt_random_data, RandomData, RandomData::random());
    roundtrip!(rt_complex, test_rt_complex, PgBox<Complex>, Complex::random());

    // -----------
    // arrays of the above
    // -----------

    // TODO: Need to fix these array-of-bytea, array-of-text, and array-of-cstr tests,
    // because `impl FromDatum for <Vec<Option<T>>` is broken enough pg_getarg does not work.
    // This likely requires changing the glue code that fetches arguments.

    // roundtrip!(
    //     rt_array_bytea,
    //     test_rt_array_bytea,
    //     'a,
    //     Vec<Option<&[u8]>>,
    //     vec![
    //         None,
    //         Some([b'a', b'b', b'c'].as_slice()),
    //         Some([b'd', b'e', b'f'].as_slice()),
    //         None,
    //         Some([b'g', b'h', b'i'].as_slice()),
    //         None
    //     ]
    // );

    // roundtrip!(
    //     rt_array_refstr,
    //     test_rt_array_refstr,
    //     'a,
    //     Vec<Option<&'a str>>,
    //     vec![None, Some("foo"), Some("bar"), None, Some("baz"), None]
    // );

    // roundtrip!(
    //     rt_array_cstr,
    //     test_rt_array_cstr,
    //     Vec<Option<&'static CStr>>,
    //     vec![
    //         None,
    //         Some( c"&one" ),
    //         Some( c"&two" ),
    //         None,
    //         Some( c"&three" ),
    //         None,
    //     ]
    // );

    roundtrip!(
        rt_array_char,
        test_rt_array_char,
        Vec<Option<char>>,
        vec![None, Some('a'), Some(char::MAX), None, Some('d'), None]
    );
    roundtrip!(
        rt_array_i8,
        test_rt_array_i8,
        Vec<Option<i8>>,
        vec![None, Some(i8::MIN), Some(i8::MAX), None, Some(42), None]
    );
    roundtrip!(
        rt_array_point,
        test_rt_array_point,
        Vec<Option<pg_sys::Point>>,
        vec![
            None,
            Some(pg_sys::Point { x: 1.0, y: 2.0 }),
            Some(pg_sys::Point { x: 3.0, y: 4.0 }),
            None,
            Some(pg_sys::Point { x: 5.0, y: 6.0 }),
            None
        ]
    );
    roundtrip!(
        rt_array_string,
        test_rt_array_string,
        Vec<Option<String>>,
        vec![
            None,
            Some(String::from("one")),
            Some(String::from("two")),
            None,
            Some(String::from("four")),
            None
        ]
    );
    roundtrip!(
        rt_array_oid,
        test_rt_array_oid,
        Vec<Option<pg_sys::Oid>>,
        vec![
            None,
            Some(pg_sys::Oid::from(BuiltinOid::ANYOID)),
            Some(pg_sys::Oid::from(BuiltinOid::TEXTOID)),
            None,
            Some(pg_sys::Oid::from(BuiltinOid::DATEOID)),
            None
        ]
    );
    roundtrip!(
        rt_array_i16,
        test_rt_array_i16,
        Vec<Option<i16>>,
        vec![None, Some(i16::MIN), Some(i16::MAX), None, Some(42), None]
    );
    roundtrip!(
        rt_array_f64,
        test_rt_array_f64,
        Vec<Option<f64>>,
        vec![None, Some(f64::MIN), Some(f64::MAX), None, Some(42.42), None]
    );
    roundtrip!(
        rt_array_box,
        test_rt_array_box,
        Vec<Option<pg_sys::BOX>>,
        vec![
            None,
            Some(pg_sys::BOX {
                high: pg_sys::Point { x: 1.0, y: 2.0 },
                low: pg_sys::Point { x: 3.0, y: 4.0 }
            }),
            Some(pg_sys::BOX {
                high: pg_sys::Point { x: 5.0, y: 6.0 },
                low: pg_sys::Point { x: 7.0, y: 8.0 }
            }),
            None,
            Some(pg_sys::BOX {
                high: pg_sys::Point { x: 9.0, y: 10.0 },
                low: pg_sys::Point { x: 11.0, y: 12.0 }
            }),
            None,
        ]
    );
    roundtrip!(
        rt_array_i64,
        test_rt_array_i64,
        Vec<Option<i64>>,
        vec![None, Some(i64::MIN), Some(i64::MAX), None, Some(42), None]
    );
    roundtrip!(
        rt_array_i32,
        test_rt_array_i32,
        Vec<Option<i32>>,
        vec![None, Some(i32::MIN), Some(i32::MAX), None, Some(42), None]
    );
    roundtrip!(
        rt_array_bool,
        test_rt_array_bool,
        Vec<Option<bool>>,
        vec![None, Some(true), Some(false), None, Some(true), None]
    );
    roundtrip!(
        rt_array_f32,
        test_rt_array_f32,
        Vec<Option<f32>>,
        vec![None, Some(f32::MIN), Some(f32::MAX), None, Some(42.42), None]
    );
    roundtrip!(
        rt_array_numeric,
        test_rt_array_numeric,
        Vec<Option<Numeric<100, 0>>>,
        vec![
            None,
            Some(Numeric::try_from(i128::MIN).unwrap()),
            Some(Numeric::try_from(u128::MAX).unwrap()),
            None,
            Some(Numeric::from_str("31241234123412341234").unwrap()),
            None
        ]
    );
    roundtrip!(
        rt_array_anynumeric,
        test_rt_array_anynumeric,
        Vec<Option<AnyNumeric>>,
        vec![
            None,
            Some(AnyNumeric::try_from(i128::MIN).unwrap()),
            Some(AnyNumeric::try_from(u128::MAX).unwrap()),
            None,
            Some(AnyNumeric::from_str("31241234123412341234").unwrap()),
            None
        ]
    );
    roundtrip!(
        rt_array_date,
        test_rt_array_date,
        Vec<Option<Date>>,
        vec![
            None,
            Some(Date::from_str("1977-03-20").unwrap()),
            Some(Date::from_str("2000-01-01").unwrap()),
            None,
            Some(Date::from_str("2023-07-04").unwrap()),
            None
        ]
    );
    roundtrip!(
        rt_array_ts,
        test_rt_array_ts,
        Vec<Option<Timestamp>>,
        vec![
            None,
            Some(Timestamp::from_str("1977-03-20 04:42:00").unwrap()),
            Some(Timestamp::from_str("2000-01-01 04:42:00").unwrap()),
            None,
            Some(Timestamp::from_str("2023-07-04 04:42:00").unwrap()),
            None
        ]
    );
    roundtrip!(
        rt_array_tstz,
        test_rt_array_tstz,
        Vec<Option<TimestampWithTimeZone>>,
        vec![
            None,
            Some(TimestampWithTimeZone::from_str("1977-03-20 04:42:00 PDT").unwrap()),
            Some(TimestampWithTimeZone::from_str("2000-01-01 04:42:00 PDT").unwrap()),
            None,
            Some(TimestampWithTimeZone::from_str("2023-07-04 04:42:00 PDT").unwrap()),
            None
        ]
    );
    roundtrip!(
        rt_array_time,
        test_rt_array_time,
        Vec<Option<Time>>,
        vec![
            None,
            Some(Time::from_str("04:42:00").unwrap()),
            Some(Time::from_str("15:42:00").unwrap()),
            None,
            Some(Time::from_str("23:42:00").unwrap()),
            None
        ]
    );
    roundtrip!(
        rt_array_timetz,
        test_rt_array_timetz,
        Vec<Option<TimeWithTimeZone>>,
        vec![
            None,
            Some(TimeWithTimeZone::from_str("04:42:00 MDT").unwrap()),
            Some(TimeWithTimeZone::from_str("15:42:00 MDT").unwrap()),
            None,
            Some(TimeWithTimeZone::from_str("23:42:00 MDT").unwrap()),
            None
        ]
    );
    roundtrip!(
        rt_array_interval,
        test_rt_array_interval,
        Vec<Option<Interval>>,
        vec![
            None,
            Some(Interval::new(4, 2, 420).unwrap()),
            Some(Interval::new(5, 6, 789).unwrap()),
            None,
            Some(Interval::new(10, 11, 12).unwrap()),
            None
        ]
    );

    roundtrip!(
        rt_array_uuid,
        test_rt_array_uuid,
        Vec<Option<Uuid>>,
        vec![
            None,
            Some(Uuid::from_bytes([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15])),
            Some(Uuid::from_bytes([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15])),
            None,
            Some(Uuid::from_bytes([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15])),
            None
        ]
    );

    roundtrip!(
        rt_array_random_data,
        test_rt_array_random_data,
        Vec<Option<RandomData>>,
        vec![
            None,
            Some(RandomData::random()),
            Some(RandomData::random()),
            None,
            Some(RandomData::random()),
            None
        ]
    );

    roundtrip!(
        rt_array_complex,
        test_rt_array_complex,
        Vec<Option<PgBox<Complex>>>,
        vec![
            None,
            Some(Complex::random()),
            Some(Complex::random()),
            None,
            Some(Complex::random()),
            None
        ]
    );
}
