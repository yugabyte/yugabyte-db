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
    use pgrx::AllocatedByRust;

    #[pg_test]
    fn pgbox_alloc() {
        let mut ptr: PgBox<i32, AllocatedByRust> = unsafe { PgBox::<i32>::alloc() };
        // ptr is uninitialized data!!! This is dangerous to read from!!!
        *ptr = 5;

        assert_eq!(*ptr, 5);
    }

    #[pg_test]
    fn pgbox_alloc0() {
        let mut ptr: PgBox<i32, AllocatedByRust> = unsafe { PgBox::<i32>::alloc0() };

        assert_eq!(*ptr, 0);

        *ptr = 5;

        assert_eq!(*ptr, 5);
    }
}
