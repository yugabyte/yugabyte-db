use std::sync::LazyLock;

use parquet_copy_hook::hook::{init_parquet_copy_hook, ENABLE_PARQUET_COPY_HOOK};
use parquet_copy_hook::pg_compat::MarkGUCPrefixReserved;
use pgrx::{prelude::*, GucContext, GucFlags, GucRegistry};
use tokio::runtime::Runtime;

mod arrow_parquet;
mod fdw;
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

extension_sql!(
    r#"
    CREATE FUNCTION parquet.fdw_handler()
    RETURNS fdw_handler
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'parquet_fdw_handler_wrapper';

    CREATE FUNCTION parquet.fdw_validator(options text[], catalog oid)
    RETURNS void
    LANGUAGE c
    AS 'MODULE_PATHNAME', 'parquet_fdw_validator_wrapper';

    CREATE FOREIGN DATA WRAPPER parquet
      HANDLER parquet.fdw_handler
      VALIDATOR parquet.fdw_validator;

    CREATE FUNCTION parquet.create_simple_secret(
        type                 text,
        key_id               text DEFAULT '',
        secret               text DEFAULT '',
        session_token        text DEFAULT '',
        region               text DEFAULT '',
        url_style            text DEFAULT '',
        endpoint             text DEFAULT '',
        scope                text DEFAULT '',
        use_ssl              text DEFAULT '',
        allow_http           text DEFAULT '',
        service_account_key  text DEFAULT '',
        service_account_path text DEFAULT ''
    ) RETURNS text
    LANGUAGE plpgsql
    SET search_path = pg_catalog, pg_temp
    AS $body$
    DECLARE
        lc_type           text := lower(type);
        base_name         text;
        server_name       text;
        suffix            int := 0;
        server_options    text := '';
        mapping_options   text := '';
        has_hmac          boolean := false;
        has_service_acct  boolean := false;
    BEGIN
        IF lc_type NOT IN ('s3', 'gcs') THEN
            RAISE EXCEPTION 'parquet.create_simple_secret: type must be ''s3'' or ''gcs'', got %', type;
        END IF;

        IF lc_type = 's3' AND (service_account_key <> '' OR service_account_path <> '') THEN
            RAISE EXCEPTION 'parquet.create_simple_secret: service_account_* options are only valid for type=''gcs''';
        END IF;

        IF lc_type = 'gcs' AND (region <> '' OR url_style <> '' OR allow_http <> '') THEN
            RAISE EXCEPTION 'parquet.create_simple_secret: region, url_style and allow_http are only valid for type=''s3''';
        END IF;

        has_hmac := (key_id <> '' OR secret <> '');
        has_service_acct := (service_account_key <> '' OR service_account_path <> '');

        IF lc_type = 's3' THEN
            IF NOT (key_id <> '' AND secret <> '') THEN
                RAISE EXCEPTION 'parquet.create_simple_secret: type=''s3'' requires non-empty key_id and secret';
            END IF;
        ELSIF lc_type = 'gcs' THEN
            IF has_hmac AND has_service_acct THEN
                RAISE EXCEPTION 'parquet.create_simple_secret: type=''gcs'' cannot mix HMAC and service-account options';
            END IF;
            IF NOT has_hmac AND NOT has_service_acct THEN
                RAISE EXCEPTION 'parquet.create_simple_secret: type=''gcs'' requires HMAC (key_id+secret) or service_account_key/service_account_path';
            END IF;
            IF has_hmac AND NOT (key_id <> '' AND secret <> '') THEN
                RAISE EXCEPTION 'parquet.create_simple_secret: type=''gcs'' HMAC auth requires both key_id and secret';
            END IF;
            IF service_account_key <> '' AND service_account_path <> '' THEN
                RAISE EXCEPTION 'parquet.create_simple_secret: type=''gcs'' may set service_account_key or service_account_path, not both';
            END IF;
        END IF;

        base_name := 'simple_' || lc_type || '_secret';
        server_name := base_name;
        WHILE EXISTS (SELECT 1 FROM pg_catalog.pg_foreign_server WHERE srvname = server_name) LOOP
            suffix := suffix + 1;
            server_name := base_name || '_' || suffix::text;
        END LOOP;

        IF region <> '' THEN
            server_options := server_options || ', region ' || quote_literal(region);
        END IF;
        IF url_style <> '' THEN
            server_options := server_options || ', url_style ' || quote_literal(url_style);
        END IF;
        IF endpoint <> '' THEN
            server_options := server_options || ', endpoint ' || quote_literal(endpoint);
        END IF;
        IF scope <> '' THEN
            server_options := server_options || ', scope ' || quote_literal(scope);
        END IF;
        IF use_ssl <> '' THEN
            server_options := server_options || ', use_ssl ' || quote_literal(use_ssl);
        END IF;
        IF allow_http <> '' THEN
            server_options := server_options || ', allow_http ' || quote_literal(allow_http);
        END IF;

        IF length(server_options) > 0 THEN
            server_options := ' OPTIONS (' || substr(server_options, 3) || ')';
        END IF;

        EXECUTE format(
            'CREATE SERVER %I TYPE %L FOREIGN DATA WRAPPER parquet%s',
            server_name, lc_type, server_options);

        IF has_hmac THEN
            mapping_options := 'key_id ' || quote_literal(key_id)
                            || ', secret ' || quote_literal(secret);
            IF session_token <> '' THEN
                mapping_options := mapping_options || ', session_token ' || quote_literal(session_token);
            END IF;
        ELSE
            IF service_account_key <> '' THEN
                mapping_options := 'service_account_key ' || quote_literal(service_account_key);
            ELSE
                mapping_options := 'service_account_path ' || quote_literal(service_account_path);
            END IF;
        END IF;

        EXECUTE format(
            'CREATE USER MAPPING FOR CURRENT_USER SERVER %I OPTIONS (%s)',
            server_name, mapping_options);

        RETURN server_name;
    END;
    $body$;
    "#,
    name = "parquet_fdw",
);

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
