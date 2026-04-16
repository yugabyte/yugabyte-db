use std::sync::LazyLock;

use parquet_copy_hook::hook::{init_parquet_copy_hook, ENABLE_PARQUET_COPY_HOOK};
use parquet_copy_hook::pg_compat::MarkGUCPrefixReserved;
use pgrx::{prelude::*, GucContext, GucFlags, GucRegistry};
use tokio::runtime::Runtime;

mod arrow_parquet;
mod object_store;
mod parquet_copy_hook;
mod parquet_udfs;
#[cfg(any(test, feature = "pg_test"))]
mod pgrx_tests;
mod pgrx_utils;
mod type_compat;

// re-export external api
#[allow(unused_imports)]
pub use crate::arrow_parquet::compression::PgParquetCompression;
#[allow(unused_imports)]
pub use crate::parquet_copy_hook::copy_to_split_dest_receiver::create_copy_to_parquet_split_dest_receiver;

pgrx::pg_module_magic!();

extension_sql_file!("../sql/bootstrap.sql", name = "role_setup", bootstrap);

// PG_BACKEND_TOKIO_RUNTIME creates a tokio runtime that uses the current thread
// to run the tokio reactor. This uses the same thread that is running the Postgres backend.
pub(crate) static PG_BACKEND_TOKIO_RUNTIME: LazyLock<Runtime> = LazyLock::new(|| {
    tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .build()
        .unwrap_or_else(|e| panic!("failed to create tokio runtime: {}", e))
});

#[pg_guard]
pub extern "C-unwind" fn _PG_init() {
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
