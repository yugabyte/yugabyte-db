//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;

    use pgrx::prelude::*;

    use pgrx::datum::numeric::Error;
    use serde::Deserialize;

    #[pg_extern]
    fn return_an_i32_numeric() -> AnyNumeric {
        AnyNumeric::try_from(32).unwrap()
    }

    #[pg_extern]
    fn return_a_f64_numeric() -> AnyNumeric {
        AnyNumeric::try_from(64.64646464f64).unwrap()
    }

    #[pg_extern]
    fn return_a_u64_numeric() -> AnyNumeric {
        AnyNumeric::try_from(u64::MAX).unwrap()
    }

    #[pg_test]
    fn select_a_numeric() -> Result<(), Box<dyn std::error::Error>> {
        let result = Spi::get_one::<AnyNumeric>("SELECT 42::numeric")?.expect("SPI returned null");
        let expected: AnyNumeric = 42.into();
        assert_eq!(expected, result);
        Ok(())
    }

    #[pg_test]
    fn test_return_an_i32_numeric() {
        let result = Spi::get_one::<bool>("SELECT 32::numeric = tests.return_an_i32_numeric();");
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_return_a_f64_numeric() {
        let result =
            Spi::get_one::<bool>("SELECT 64.64646464::numeric = tests.return_a_f64_numeric();");
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_return_a_u64_numeric() {
        let result = Spi::get_one::<bool>(
            "SELECT 18446744073709551615::numeric = tests.return_a_u64_numeric();",
        );
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_deserialize_numeric() {
        use serde_json::json;
        AnyNumeric::deserialize(&json!(42)).unwrap();
        AnyNumeric::deserialize(&json!(42.4242)).unwrap();
        AnyNumeric::deserialize(&json!(18446744073709551615u64)).unwrap();
        AnyNumeric::deserialize(&json!("64.64646464")).unwrap();

        let error = AnyNumeric::deserialize(&json!("foo")).err().unwrap().to_string();
        assert_eq!("invalid input syntax for type numeric: \"foo\"", &error);
    }

    #[pg_test]
    #[rustfmt::skip]
    fn test_limits() {
        assert_eq!(i8::try_from(AnyNumeric::try_from(i8::MIN).unwrap()).unwrap(), i8::MIN);
        assert_eq!(i16::try_from(AnyNumeric::try_from(i16::MIN).unwrap()).unwrap(), i16::MIN);
        assert_eq!(i32::try_from(AnyNumeric::try_from(i32::MIN).unwrap()).unwrap(), i32::MIN);
        assert_eq!(i64::try_from(AnyNumeric::try_from(i64::MIN).unwrap()).unwrap(), i64::MIN);
        assert_eq!(i128::try_from(AnyNumeric::try_from(i128::MIN).unwrap()).unwrap(), i128::MIN);
        assert_eq!(isize::try_from(AnyNumeric::try_from(isize::MIN).unwrap()).unwrap(), isize::MIN);

        assert_eq!(i8::try_from(AnyNumeric::try_from(i8::MAX).unwrap()).unwrap(), i8::MAX);
        assert_eq!(i16::try_from(AnyNumeric::try_from(i16::MAX).unwrap()).unwrap(), i16::MAX);
        assert_eq!(i32::try_from(AnyNumeric::try_from(i32::MAX).unwrap()).unwrap(), i32::MAX);
        assert_eq!(i64::try_from(AnyNumeric::try_from(i64::MAX).unwrap()).unwrap(), i64::MAX);
        assert_eq!(i128::try_from(AnyNumeric::try_from(i128::MAX).unwrap()).unwrap(), i128::MAX);
        assert_eq!(isize::try_from(AnyNumeric::try_from(isize::MAX).unwrap()).unwrap(), isize::MAX);

        assert_eq!(u8::try_from(AnyNumeric::try_from(u8::MIN).unwrap()).unwrap(), u8::MIN);
        assert_eq!(u16::try_from(AnyNumeric::try_from(u16::MIN).unwrap()).unwrap(), u16::MIN);
        assert_eq!(u32::try_from(AnyNumeric::try_from(u32::MIN).unwrap()).unwrap(), u32::MIN);
        assert_eq!(u64::try_from(AnyNumeric::try_from(u64::MIN).unwrap()).unwrap(), u64::MIN);
        assert_eq!(u128::try_from(AnyNumeric::try_from(u128::MIN).unwrap()).unwrap(), u128::MIN);
        assert_eq!(usize::try_from(AnyNumeric::try_from(usize::MIN).unwrap()).unwrap(), usize::MIN);

        assert_eq!(u8::try_from(AnyNumeric::try_from(u8::MAX).unwrap()).unwrap(), u8::MAX);
        assert_eq!(u16::try_from(AnyNumeric::try_from(u16::MAX).unwrap()).unwrap(), u16::MAX);
        assert_eq!(u32::try_from(AnyNumeric::try_from(u32::MAX).unwrap()).unwrap(), u32::MAX);
        assert_eq!(u64::try_from(AnyNumeric::try_from(u64::MAX).unwrap()).unwrap(), u64::MAX);
        assert_eq!(u128::try_from(AnyNumeric::try_from(u128::MAX).unwrap()).unwrap(), u128::MAX);
        assert_eq!(usize::try_from(AnyNumeric::try_from(usize::MAX).unwrap()).unwrap(), usize::MAX);

        //
        // precision isn't quite the same and so can't compare min/max of floats
        //
        
        // assert_eq!(f32::try_from(AnyNumeric::try_from(f32::MIN).unwrap()).unwrap(), f32::MIN);
        // assert_eq!(f64::try_from(AnyNumeric::try_from(f64::MIN).unwrap()).unwrap(), f64::MIN);
        // assert_eq!(f32::try_from(AnyNumeric::try_from(f32::MAX).unwrap()).unwrap(), f32::MAX);
        // assert_eq!(f64::try_from(AnyNumeric::try_from(f64::MAX).unwrap()).unwrap(), f64::MAX);

        // -/+Infinity isn't supported in these versions of Postgres
        #[cfg(feature = "pg13")]
        {
            assert_eq!(AnyNumeric::try_from(f32::INFINITY).err(),
                       Some(pgrx::numeric::Error::ConversionNotSupported(String::from("cannot convert infinity to numeric"))));
            assert_eq!(AnyNumeric::try_from(f32::NEG_INFINITY).err(),
                       Some(pgrx::numeric::Error::ConversionNotSupported(String::from("cannot convert infinity to numeric"))));
            assert_eq!(AnyNumeric::try_from(f64::INFINITY).err(),
                       Some(pgrx::numeric::Error::ConversionNotSupported(String::from("cannot convert infinity to numeric"))));
            assert_eq!(AnyNumeric::try_from(f64::NEG_INFINITY).err(),
                       Some(pgrx::numeric::Error::ConversionNotSupported(String::from("cannot convert infinity to numeric"))));
        }
        
        
        // but it is in these
        #[cfg(any(feature = "pg14", feature="pg15"))]
        {
            assert_eq!(f32::try_from(AnyNumeric::try_from(f32::INFINITY).unwrap()).unwrap(), f32::INFINITY);
            assert_eq!(f64::try_from(AnyNumeric::try_from(f64::INFINITY).unwrap()).unwrap(), f64::INFINITY);
            assert_eq!(f32::try_from(AnyNumeric::try_from(f32::NEG_INFINITY).unwrap()).unwrap(), f32::NEG_INFINITY);
            assert_eq!(f64::try_from(AnyNumeric::try_from(f64::NEG_INFINITY).unwrap()).unwrap(), f64::NEG_INFINITY);
        }

        assert!(f32::try_from(AnyNumeric::try_from(f32::NAN).unwrap()).unwrap().is_nan());
        assert!(f64::try_from(AnyNumeric::try_from(f64::NAN).unwrap()).unwrap().is_nan());
    }

    #[pg_test]
    #[rustfmt::skip]
    fn test_bad_conversions() {
        let a = AnyNumeric::try_from(u128::MAX).unwrap();
        let b = u64::try_from(a);
        assert_eq!(b, Err(Error::OutOfRange(String::from("number too large to fit in target type"))));

        let a = AnyNumeric::try_from(u128::MAX).unwrap();
        let b = usize::try_from(a);
        assert_eq!(b, Err(Error::OutOfRange(String::from("number too large to fit in target type"))));

        let a = AnyNumeric::try_from(u128::MAX).unwrap();
        let b = u32::try_from(a);
        assert_eq!(b, Err(Error::OutOfRange(String::from("number too large to fit in target type"))));

        let a = AnyNumeric::try_from(u128::MAX).unwrap();
        let b = u16::try_from(a);
        assert_eq!(b, Err(Error::OutOfRange(String::from("number too large to fit in target type"))));

        let a = AnyNumeric::try_from(u128::MAX).unwrap();
        let b = u8::try_from(a);
        assert_eq!(b, Err(Error::OutOfRange(String::from("number too large to fit in target type"))));

        let a = AnyNumeric::try_from(i128::MIN).unwrap();
        let b = u8::try_from(a);
        assert_eq!(b, Err(Error::OutOfRange(String::from("invalid digit found in string"))));
        
        let a = AnyNumeric::try_from(f64::MAX).unwrap();
        match f32::try_from(a) {
            Err(Error::OutOfRange(_)) => {},
            other => panic!("{other:?}"),
        }
    }

    #[pg_test]
    fn test_nan_ordering() {
        let mut v = vec![
            (1, AnyNumeric::try_from("nan").unwrap()),
            (3, AnyNumeric::try_from("nan").unwrap()),
            (2, AnyNumeric::try_from("nan").unwrap()),
        ];

        v.sort_by(|a, b| a.1.cmp(&b.1));
        assert_eq!(
            v,
            vec![
                (1, AnyNumeric::try_from("nan").unwrap()),
                (3, AnyNumeric::try_from("nan").unwrap()),
                (2, AnyNumeric::try_from("nan").unwrap())
            ]
        )
    }

    #[pg_test]
    fn test_ordering() {
        let mut v =
            vec![(3, AnyNumeric::from(3)), (2, AnyNumeric::from(2)), (1, AnyNumeric::from(1))];

        v.sort_by(|a, b| a.1.cmp(&b.1));
        assert_eq!(
            v,
            vec![(1, AnyNumeric::from(1)), (2, AnyNumeric::from(2)), (3, AnyNumeric::from(3)),]
        )
    }

    #[pg_test]
    fn test_anynumeric_sum() -> Result<(), Box<dyn std::error::Error>> {
        let numbers = Spi::get_one::<Vec<AnyNumeric>>("SELECT ARRAY[1.0, 2.0, 3.0]::numeric[]")?
            .expect("SPI result was null");
        let expected: AnyNumeric = 6.into();
        assert_eq!(expected, numbers.into_iter().sum());
        Ok(())
    }

    #[pg_test]
    fn test_option_anynumeric_sum() -> Result<(), Box<dyn std::error::Error>> {
        let numbers =
            Spi::get_one::<Vec<Option<AnyNumeric>>>("SELECT ARRAY[1.0, 2.0, 3.0]::numeric[]")?
                .expect("SPI result was null");
        let expected: AnyNumeric = 6.into();
        assert_eq!(expected, numbers.into_iter().map(|n| n.unwrap_or_default()).sum());
        Ok(())
    }
}
