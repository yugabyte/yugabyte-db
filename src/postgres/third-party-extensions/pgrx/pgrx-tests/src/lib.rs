//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#![cfg_attr(feature = "nightly", feature(allocator_api))]

mod framework;
#[cfg(any(test, feature = "pg_test"))]
mod tests;

pub use framework::*;
#[cfg(feature = "proptest")]
pub mod proptest;

#[cfg(any(test, feature = "pg_test"))]
pgrx::pg_module_magic!();

#[cfg(test)]
pub mod pg_test {
    pub fn setup(_options: Vec<&str>) {
        // noop
    }

    pub fn postgresql_conf_options() -> Vec<&'static str> {
        vec!["shared_preload_libraries='pgrx_tests'"]
    }
}
