fn main() {}

mod tests {
    use pgrx::prelude::*;
    use std::error::Error;
    use std::ffi::CStr;

    #[allow(unused_imports)]
    use crate as pgrx_tests;

    macro_rules! roundtrip {
        ($fname:ident, $tname:ident, &$lt:lifetime $rtype:ty, $expected:expr) => {
            #[pg_extern]
            fn $fname<$lt>(i: &$lt $rtype) -> &$lt $rtype {
                i
            }

            roundtrip_test!($fname, $tname, &'_ $rtype, $expected);
        };
        ($fname:ident, $tname:ident, $rtype:ty, $expected:expr) => {
            #[pg_extern]
            fn $fname(i: $rtype) -> $rtype {
                i
            }

            roundtrip_test!($fname, $tname, $rtype, $expected);
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
    }

    // TODO: Need to fix these array-of-bytea, array-of-text, and array-of-cstr tests,
    // because `impl FromDatum for <Vec<Option<T>>` is broken enough pg_getarg does not work.
    // This likely requires changing the glue code that fetches arguments.

    roundtrip!(
        rt_array_bytea,
        test_rt_array_bytea,
        Vec<Option<&[u8]>>,
        vec![
            None,
            Some([b'a', b'b', b'c'].as_slice()),
            Some([b'd', b'e', b'f'].as_slice()),
            None,
            Some([b'g', b'h', b'i'].as_slice()),
            None
        ]
    );

    roundtrip!(
        rt_array_refstr,
        test_rt_array_refstr,
        Vec<Option<&'a str>>,
        vec![None, Some("foo"), Some("bar"), None, Some("baz"), None]
    );

    roundtrip!(
        rt_array_cstr,
        test_rt_array_cstr,
        Vec<Option<&'static CStr>>,
        vec![
            None,
            Some( c"&one" ),
            Some( c"&two" ),
            None,
            Some( c"&three" ),
            None,
        ]
    );
}
