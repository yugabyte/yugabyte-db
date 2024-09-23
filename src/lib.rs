use pgrx::prelude::*;

mod arrow_parquet;
mod parquet_copy_hook;
mod pgrx_utils;
mod type_compat;

pgrx::pg_module_magic!();

#[allow(static_mut_refs)]
#[pg_guard]
pub extern "C" fn _PG_init() {}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {}

/// This module is required by `cargo pgrx test` invocations.
/// It must be visible at the root of your extension crate.
#[cfg(test)]
pub mod pg_test {
    pub fn setup(_options: Vec<&str>) {
        // perform one-off initialization when the pg_test framework starts
    }

    pub fn postgresql_conf_options() -> Vec<&'static str> {
        // return any postgresql.conf settings that are required for your tests
        vec!["shared_preload_libraries = 'pg_parquet'"]
    }
}
