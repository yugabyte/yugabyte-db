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

#[pg_extern]
fn extern_func() -> bool {
    extern_func_impl::<u8>()
}

// This ensures that parameterized function compiles when it has `pg_guard` attached to it
#[pg_guard]
// Uncommenting the line below will make it fail to compile
// #[no_mangle]
extern "C-unwind" fn extern_func_impl<T>() -> bool {
    true
}

// This ensures that non-parameterized function compiles when it has `pg_guard` attached to it
// and [no_mangle]
#[pg_guard]
#[no_mangle]
extern "C-unwind" fn extern_func_impl_1() -> bool {
    true
}

// This ensures that lifetime-parameterized function compiles when it has `pg_guard` attached to it
// and [no_mangle]
#[pg_guard]
#[no_mangle]
#[allow(unused_lifetimes)]
extern "C-unwind" fn extern_func_impl_2<'a>() -> bool {
    true
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;
}
