//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use super::Complex;

#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;

    use super::Complex;
    use pgrx::prelude::*;

    #[pg_test]
    fn test_complex_in() -> Result<(), pgrx::spi::Error> {
        Spi::connect(|client| {
            let complex = client
                .select("SELECT '1.1,2.2'::complex;", None, &[])?
                .first()
                .get_one::<PgBox<Complex>>()?
                .expect("datum was null");

            assert_eq!(&complex.r, &1.1);
            assert_eq!(&complex.i, &2.2);
            Ok(())
        })
    }

    #[pg_test]
    fn test_complex_out() {
        let string_val = Spi::get_one::<&str>("SELECT complex_out('1.1,2.2')::text");

        assert_eq!(string_val, Ok(Some("1.1, 2.2")));
    }

    #[pg_test]
    fn test_complex_from_text() -> Result<(), pgrx::spi::Error> {
        Spi::connect(|client| {
            let complex = client
                .select("SELECT '1.1, 2.2'::complex;", None, &[])?
                .first()
                .get_one::<PgBox<Complex>>()?
                .expect("datum was null");

            assert_eq!(&complex.r, &1.1);
            assert_eq!(&complex.i, &2.2);
            Ok(())
        })
    }

    #[pg_test]
    fn test_complex_storage_and_retrieval() -> Result<(), pgrx::spi::Error> {
        let complex = Spi::connect_mut(|client| {
            client.update(
                "CREATE TABLE complex_test AS SELECT s as id, (s || '.0, 2.0' || s)::complex as value FROM generate_series(1, 1000) s;\
                SELECT value FROM complex_test ORDER BY id;", None, &[])?.first().get_one::<PgBox<Complex>>()
        })?.expect("datum was null");

        assert_eq!(&complex.r, &1.0);
        assert_eq!(&complex.i, &2.01);
        Ok(())
    }
}
