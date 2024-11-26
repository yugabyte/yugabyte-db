use parquet_copy_hook::hook::{init_parquet_copy_hook, ENABLE_PARQUET_COPY_HOOK};
use parquet_copy_hook::pg_compat::MarkGUCPrefixReserved;
use pgrx::{prelude::*, GucContext, GucFlags, GucRegistry};

mod arrow_parquet;
mod parquet_copy_hook;
mod parquet_udfs;
mod pgrx_tests;
mod pgrx_utils;
mod type_compat;

// re-export external api
#[allow(unused_imports)]
pub use crate::arrow_parquet::compression::PgParquetCompression;
#[allow(unused_imports)]
pub use crate::parquet_copy_hook::copy_to_dest_receiver::create_copy_to_parquet_dest_receiver;

pgrx::pg_module_magic!();

extension_sql_file!("../sql/bootstrap.sql", name = "role_setup", bootstrap);

#[pg_guard]
pub extern "C" fn _PG_init() {
    GucRegistry::define_bool_guc(
        "pg_parquet.enable_copy_hooks",
        "Enable parquet copy hooks",
        "Enable parquet copy hooks",
        &ENABLE_PARQUET_COPY_HOOK,
        GucContext::Userset,
        GucFlags::default(),
    );

    MarkGUCPrefixReserved("pg_parquet");

    init_parquet_copy_hook();
}

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
