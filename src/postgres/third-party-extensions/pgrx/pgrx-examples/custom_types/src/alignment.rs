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
use serde::*;

#[derive(PostgresType, Serialize, Deserialize)]
#[pgrx(alignment = "on")]
pub struct AlignedTo4Bytes {
    v1: u32,
    v2: [u32; 3],
}

#[derive(PostgresType, Serialize, Deserialize)]
#[pgrx(alignment = "on")]
pub struct AlignedTo8Bytes {
    v1: u64,
    v2: [u64; 3],
}

#[derive(PostgresType, Serialize, Deserialize)]
#[pgrx(alignment = "off")]
pub struct NotAlignedTo8Bytes {
    v1: u64,
    v2: [u64; 3],
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use pgrx::prelude::*;

    #[cfg(not(feature = "no-schema-generation"))]
    #[pg_test]
    fn test_alignment_is_correct() {
        let val = Spi::get_one::<String>(r#"SELECT typalign::text FROM pg_type WHERE typname = 'alignedto4bytes'"#).unwrap().unwrap();

        assert!(val == "i");

        let val = Spi::get_one::<String>(r#"SELECT typalign::text FROM pg_type WHERE typname = 'alignedto8bytes'"#).unwrap().unwrap();

        assert!(val == "d");

        let val = Spi::get_one::<String>(r#"SELECT typalign::text FROM pg_type WHERE typname = 'notalignedto8bytes'"#).unwrap().unwrap();

        assert!(val == "i");
    }
}
