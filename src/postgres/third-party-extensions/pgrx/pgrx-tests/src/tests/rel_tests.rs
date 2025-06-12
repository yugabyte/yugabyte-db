//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use pgrx::prelude::*;
use pgrx::rel::PgRelation;
#[pg_extern]
fn accept_relation(rel: PgRelation) {
    _ = rel;
}

#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;
    use pgrx::prelude::*;

    #[pg_test]
    fn test_accept_relation() {
        Spi::run("CREATE TABLE relation_test(name TEXT);").unwrap();
        Spi::run("CREATE INDEX relation_test_index ON relation_test(name);").unwrap();
        Spi::run("SELECT accept_relation('relation_test_index');").unwrap();
    }
}
