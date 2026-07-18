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
    fn return_bytes() -> &'static [u8] {
        b"bytes"
    }

    #[pg_test]
    fn test_return_bytes() {
        let bytes = Spi::get_one::<&[u8]>("SELECT tests.return_bytes();");
        assert_eq!(bytes, Ok(Some(b"bytes".as_slice())));
    }

    #[pg_extern]
    fn return_bytes_slice(bytes: &[u8]) -> &[u8] {
        &bytes[1..=3]
    }

    #[pg_test]
    fn test_return_bytes_slice() {
        let slice = Spi::get_one::<&[u8]>("SELECT tests.return_bytes_slice('abcdefg'::bytea);");
        assert_eq!(slice, Ok(Some(b"bcd".as_slice())));
    }

    #[pg_extern]
    fn return_vec_bytes() -> Vec<u8> {
        b"bytes".into_iter().cloned().collect()
    }

    #[pg_test]
    fn test_return_vec_bytes() {
        let vec = Spi::get_one::<Vec<u8>>("SELECT tests.return_vec_bytes();");
        assert_eq!(vec, Ok(Some(vec![b'b', b'y', b't', b'e', b's'])));
    }

    #[pg_extern]
    fn return_vec_subvec(bytes: Vec<u8>) -> Vec<u8> {
        (&bytes[1..=3]).into_iter().cloned().collect()
    }

    #[pg_test]
    fn test_return_vec_subvec() {
        let vec = Spi::get_one::<Vec<u8>>("SELECT tests.return_vec_subvec('abcdefg'::bytea);");
        assert_eq!(vec, Ok(Some(vec![b'b', b'c', b'd'])));
    }
}
