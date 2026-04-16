//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
#![allow(unexpected_cfgs)]
use pgrx::prelude::*;

#[cfg(any(test, feature = "pg_test"))]
#[pg_extern]
fn func_test_cfg() {}

#[cfg(feature = "nonexistent")]
#[pg_extern]
fn func_non_existent_cfg(t: NonexistentType) {}

#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;

    use pgrx::prelude::*;

    #[pg_test]
    fn test_cfg_exists() -> Result<(), spi::Error> {
        Spi::run("SELECT func_test_cfg();")
    }
}
