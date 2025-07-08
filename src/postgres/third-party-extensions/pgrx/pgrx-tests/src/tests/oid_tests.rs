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

    #[pg_extern]
    fn oid_roundtrip(oid: pg_sys::Oid) -> pg_sys::Oid {
        oid
    }

    #[pg_test]
    fn test_reasonable_oid() -> spi::Result<()> {
        let oid = Spi::get_one::<pg_sys::Oid>("SELECT tests.oid_roundtrip(42)")?
            .expect("SPI result was null");
        assert_eq!(oid.to_u32(), 42);
        Ok(())
    }

    #[pg_test]
    fn test_completely_unreasonable_but_still_valid_oid() -> spi::Result<()> {
        // nb:  this stupid value is greater than i32::MAX
        let oid = Spi::get_one::<pg_sys::Oid>("SELECT tests.oid_roundtrip(2147483648)")?
            .expect("SPI result was null");
        assert_eq!(oid.to_u32(), 2_147_483_648);
        Ok(())
    }
}
