use pgrx::prelude::*;

// BorrowDatum only describes something as a valid argument, but every &T has a U we CAN return,
// going from &T as arg to T as ret type. We also don't necessarily have SPI support for each type.
// Thus we don't *exactly* reuse the roundtrip test macro.

macro_rules! clonetrip {
    ($fname:ident, $tname:ident, &$lt:lifetime $rtype:ty, &$expected:expr) => {
        #[pg_extern]
        fn $fname<$lt>(i: &$lt $rtype) -> $rtype {
            i.clone()
        }

        clonetrip_test!($fname, $tname, &'_ $rtype, $expected);
    };
    ($fname:ident, $tname:ident, &$lt:lifetime $ref_ty:ty => $own_ty:ty, $test:expr, $value:expr) => {
        #[pg_extern]
        fn $fname(i: &$ref_ty) -> $own_ty {
            i.into()
        }

        clonetrip_test!($fname, $tname, &'_ $ref_ty => $own_ty, $test, $value);
    };
}

macro_rules! clonetrip_test {
    ($fname:ident, $tname:ident, &'_ $rtype:ty, $expected:expr) => {
        #[pg_test]
        fn $tname() -> Result<(), Box<dyn std::error::Error>> {
            let expected: $rtype = $expected;
            let result: $rtype = Spi::get_one_with_args(
                &format!("SELECT {}($1)", stringify!(tests.$fname)),
                &[expected.into()],
            )?
            .unwrap();

            assert_eq!(&$expected, &result);
            Ok(())
        }
    };
    ($fname:ident, $tname:ident, $ref_ty:ty => $own_ty:ty, $test:expr, $value:expr) => {
        #[pg_test]
        fn $tname() -> Result<(), Box<dyn std::error::Error>> {
            let value: $own_ty = $value;
            let result: $own_ty = Spi::get_one_with_args(
                &format!("SELECT {}($1)", stringify!(tests.$fname)),
                &[value.into()],
            )?
            .unwrap();

            assert_eq!($test, &*result);
            Ok(())
        }
    };
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use super::*;
    #[allow(unused)]
    use crate as pgrx_tests;
    use std::ffi::{CStr, CString};

    // Exercising BorrowDatum impls
    clonetrip!(clone_bool, test_clone_bool, &'a bool, &false);
    clonetrip!(clone_i8, test_clone_i8, &'a i8, &i8::MIN);
    clonetrip!(clone_i16, test_clone_i16, &'a i16, &i16::MIN);
    clonetrip!(clone_i32, test_clone_i32, &'a i32, &-1i32);
    clonetrip!(clone_f64, test_clone_f64, &'a f64, &f64::NEG_INFINITY);
    clonetrip!(
        clone_point,
        test_clone_point,
        &'a pg_sys::Point,
        &pg_sys::Point { x: -1.0, y: f64::INFINITY }
    );
    clonetrip!(clone_str, test_clone_str, &'a CStr => CString, c"cee string", CString::from(c"cee string"));
    clonetrip!(
        clone_oid,
        test_clone_oid,
        &'a pg_sys::Oid,
        &pg_sys::Oid::from(pg_sys::BuiltinOid::RECORDOID)
    );
}
