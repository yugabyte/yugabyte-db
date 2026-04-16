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

    #[pg_test]
    fn test_static_inline_fns() {
        unsafe {
            use pgrx::pg_sys::{itemptr_encode, BlockIdData, ItemPointerData};
            assert_eq!(
                itemptr_encode(&mut ItemPointerData {
                    ip_blkid: BlockIdData { bi_hi: 111, bi_lo: 222 },
                    ip_posid: 333
                }),
                476755919181
            );
        }
    }
}
