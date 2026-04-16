//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use pgrx::datum::{Uuid, UuidBytes};
use pgrx::prelude::*;

pub const TEST_UUID_V4: UuidBytes = [
    0x12, 0x3e, 0x45, 0x67, 0xe8, 0x9b, 0x12, 0xd3, 0xa4, 0x56, 0x42, 0x66, 0x14, 0x17, 0x40, 0x00,
];

#[pg_extern]
fn accept_uuid(uuid: Uuid) -> Uuid {
    uuid
}

#[pg_extern]
fn return_uuid() -> Uuid {
    Uuid::from_bytes(TEST_UUID_V4)
}

#[pg_extern]
fn display_uuid(uuid: Uuid) -> String {
    format!("{uuid}")
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;
    use pgrx::datum::Uuid;
    use pgrx::prelude::*;

    #[pg_test]
    fn test_display_uuid() {
        let result = Spi::get_one::<bool>("SELECT display_uuid('123e4567-e89b-12d3-a456-426614174000'::uuid) = '123e4567-e89b-12d3-a456-426614174000';");
        assert_eq!(result, Ok(Some(true)));

        let uuid = Uuid::from_bytes(super::TEST_UUID_V4);
        assert_eq!(format!("{uuid}"), "123e4567-e89b-12d3-a456-426614174000");

        // Lowercase hex formatting
        assert_eq!(format!("{uuid:-x}"), "123e4567-e89b-12d3-a456-426614174000");
        assert_eq!(format!("{uuid:x}"), "123e4567e89b12d3a456426614174000");

        // Uppercase hex formatting
        assert_eq!(format!("{uuid:-X}"), "123E4567-E89B-12D3-A456-426614174000");
        assert_eq!(format!("{uuid:X}"), "123E4567E89B12D3A456426614174000");
    }

    #[pg_test]
    fn test_accept_uuid() {
        let result = Spi::get_one::<bool>("SELECT accept_uuid('123e4567-e89b-12d3-a456-426614174000'::uuid) = '123e4567-e89b-12d3-a456-426614174000'::uuid;");
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_return_uuid() {
        let result = Spi::get_one::<bool>(
            "SELECT return_uuid() = '123e4567-e89b-12d3-a456-426614174000'::uuid;",
        );
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn test_parse_uuid_v4() {
        let uuid = Spi::get_one::<Uuid>("SELECT '123e4567-e89b-12d3-a456-426614174000'::uuid;");
        assert_eq!(uuid, Ok(Some(Uuid::from_bytes(super::TEST_UUID_V4))));
    }
}
