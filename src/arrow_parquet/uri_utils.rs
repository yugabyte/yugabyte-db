use std::{sync::Arc, sync::LazyLock};

use arrow::datatypes::SchemaRef;
use aws_config::BehaviorVersion;
use aws_credential_types::provider::ProvideCredentials;
use object_store::{
    aws::{AmazonS3, AmazonS3Builder},
    local::LocalFileSystem,
    path::Path,
    ObjectStore,
};
use parquet::{
    arrow::{
        arrow_to_parquet_schema,
        async_reader::{ParquetObjectReader, ParquetRecordBatchStream},
        async_writer::ParquetObjectWriter,
        AsyncArrowWriter, ParquetRecordBatchStreamBuilder,
    },
    file::{metadata::ParquetMetaData, properties::WriterProperties},
    schema::types::SchemaDescriptor,
};
use pgrx::{
    ereport,
    pg_sys::{get_role_oid, has_privs_of_role, superuser, AsPgCStr, GetUserId},
};
use tokio::runtime::Runtime;
use url::Url;

use crate::arrow_parquet::parquet_writer::DEFAULT_ROW_GROUP_SIZE;

const PARQUET_OBJECT_STORE_READ_ROLE: &str = "parquet_object_store_read";
const PARQUET_OBJECT_STORE_WRITE_ROLE: &str = "parquet_object_store_write";

// PG_BACKEND_TOKIO_RUNTIME creates a tokio runtime that uses the current thread
// to run the tokio reactor. This uses the same thread that is running the Postgres backend.
pub(crate) static PG_BACKEND_TOKIO_RUNTIME: LazyLock<Runtime> = LazyLock::new(|| {
    tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .build()
        .unwrap_or_else(|e| panic!("failed to create tokio runtime: {}", e))
});

fn parse_bucket_and_key(uri: &Url) -> (String, String) {
    debug_assert!(uri.scheme() == "s3");

    let bucket = uri
        .host_str()
        .unwrap_or_else(|| panic!("bucket not found in uri: {}", uri));

    let key = uri.path();

    (bucket.to_string(), key.to_string())
}

fn object_store_with_location(uri: &Url, copy_from: bool) -> (Arc<dyn ObjectStore>, Path) {
    if uri.scheme() == "s3" {
        let (bucket_name, key) = parse_bucket_and_key(uri);

        let storage_container = PG_BACKEND_TOKIO_RUNTIME
            .block_on(async { Arc::new(get_s3_object_store(&bucket_name).await) });

        let location = Path::from(key);

        (storage_container, location)
    } else {
        debug_assert!(uri.scheme() == "file");

        let uri = uri_as_string(uri);

        if !copy_from {
            // create or overwrite the local file
            std::fs::OpenOptions::new()
                .write(true)
                .truncate(true)
                .create(true)
                .open(&uri)
                .unwrap_or_else(|e| panic!("{}", e));
        }

        let storage_container = Arc::new(LocalFileSystem::new());

        let location = Path::from_filesystem_path(&uri).unwrap_or_else(|e| panic!("{}", e));

        (storage_container, location)
    }
}

// get_s3_object_store creates an AmazonS3 object store with the given bucket name.
// It is configured by environment variables and aws config files as fallback method.
// We need to read the config files to make the fallback method work since object_store
// does not provide a way to read them. Currently, we only support to extract
// "AWS_ACCESS_KEY_ID", "AWS_SECRET_ACCESS_KEY", "AWS_SESSION_TOKEN", "AWS_ENDPOINT_URL",
// and "AWS_REGION" from the config files.
async fn get_s3_object_store(bucket_name: &str) -> AmazonS3 {
    let mut aws_s3_builder = AmazonS3Builder::from_env().with_bucket_name(bucket_name);

    // first tries environment variables and then the config files
    let sdk_config = aws_config::defaults(BehaviorVersion::v2024_03_28())
        .load()
        .await;

    if let Some(credential_provider) = sdk_config.credentials_provider() {
        if let Ok(credentials) = credential_provider.provide_credentials().await {
            // AWS_ACCESS_KEY_ID
            aws_s3_builder = aws_s3_builder.with_access_key_id(credentials.access_key_id());

            // AWS_SECRET_ACCESS_KEY
            aws_s3_builder = aws_s3_builder.with_secret_access_key(credentials.secret_access_key());

            if let Some(token) = credentials.session_token() {
                // AWS_SESSION_TOKEN
                aws_s3_builder = aws_s3_builder.with_token(token);
            }
        }
    }

    // AWS_ENDPOINT_URL
    if let Some(aws_endpoint_url) = sdk_config.endpoint_url() {
        aws_s3_builder = aws_s3_builder.with_endpoint(aws_endpoint_url);
    }

    // AWS_REGION
    if let Some(aws_region) = sdk_config.region() {
        aws_s3_builder = aws_s3_builder.with_region(aws_region.as_ref());
    }

    aws_s3_builder.build().unwrap_or_else(|e| panic!("{}", e))
}

pub(crate) fn parse_uri(uri: &str) -> Url {
    if !uri.contains("://") {
        // local file
        return Url::from_file_path(uri)
            .unwrap_or_else(|_| panic!("not a valid file path: {}", uri));
    }

    let uri = Url::parse(uri).unwrap_or_else(|e| panic!("{}", e));

    if uri.scheme() != "s3" {
        panic!(
            "unsupported uri {}. Only local files and URIs with s3:// prefix are supported.",
            uri
        );
    }

    uri
}

pub(crate) fn uri_as_string(uri: &Url) -> String {
    if uri.scheme() == "file" {
        // removes file:// prefix from the local path uri
        return uri
            .to_file_path()
            .unwrap_or_else(|_| panic!("invalid local path: {}", uri))
            .to_string_lossy()
            .to_string();
    }

    uri.to_string()
}

pub(crate) fn parquet_schema_from_uri(uri: &Url) -> SchemaDescriptor {
    let parquet_reader = parquet_reader_from_uri(uri);

    let arrow_schema = parquet_reader.schema();

    arrow_to_parquet_schema(arrow_schema).unwrap_or_else(|e| panic!("{}", e))
}

pub(crate) fn parquet_metadata_from_uri(uri: &Url) -> Arc<ParquetMetaData> {
    let copy_from = true;
    let (parquet_object_store, location) = object_store_with_location(uri, copy_from);

    PG_BACKEND_TOKIO_RUNTIME.block_on(async {
        let object_store_meta = parquet_object_store
            .head(&location)
            .await
            .unwrap_or_else(|e| {
                panic!("failed to get object store metadata for uri {}: {}", uri, e)
            });

        let parquet_object_reader =
            ParquetObjectReader::new(parquet_object_store, object_store_meta);

        let builder = ParquetRecordBatchStreamBuilder::new(parquet_object_reader)
            .await
            .unwrap_or_else(|e| panic!("{}", e));

        builder.metadata().to_owned()
    })
}

pub(crate) fn parquet_reader_from_uri(uri: &Url) -> ParquetRecordBatchStream<ParquetObjectReader> {
    let copy_from = true;
    let (parquet_object_store, location) = object_store_with_location(uri, copy_from);

    PG_BACKEND_TOKIO_RUNTIME.block_on(async {
        let object_store_meta = parquet_object_store
            .head(&location)
            .await
            .unwrap_or_else(|e| {
                panic!("failed to get object store metadata for uri {}: {}", uri, e)
            });

        let parquet_object_reader =
            ParquetObjectReader::new(parquet_object_store, object_store_meta);

        let builder = ParquetRecordBatchStreamBuilder::new(parquet_object_reader)
            .await
            .unwrap_or_else(|e| panic!("{}", e));

        pgrx::debug2!("Converted arrow schema is: {}", builder.schema());

        builder
            .with_batch_size(DEFAULT_ROW_GROUP_SIZE as usize)
            .build()
            .unwrap_or_else(|e| panic!("{}", e))
    })
}

pub(crate) fn parquet_writer_from_uri(
    uri: &Url,
    arrow_schema: SchemaRef,
    writer_props: WriterProperties,
) -> AsyncArrowWriter<ParquetObjectWriter> {
    let copy_from = false;
    let (parquet_object_store, location) = object_store_with_location(uri, copy_from);

    let parquet_object_writer = ParquetObjectWriter::new(parquet_object_store, location);

    AsyncArrowWriter::try_new(parquet_object_writer, arrow_schema, Some(writer_props))
        .unwrap_or_else(|e| panic!("failed to create parquet writer for uri {}: {}", uri, e))
}

pub(crate) fn ensure_access_privilege_to_uri(uri: &Url, copy_from: bool) {
    if unsafe { superuser() } {
        return;
    }

    let user_id = unsafe { GetUserId() };
    let is_file = uri.scheme() == "file";

    let required_role_name = if is_file {
        if copy_from {
            "pg_read_server_files"
        } else {
            "pg_write_server_files"
        }
    } else if copy_from {
        PARQUET_OBJECT_STORE_READ_ROLE
    } else {
        PARQUET_OBJECT_STORE_WRITE_ROLE
    };

    let required_role_id =
        unsafe { get_role_oid(required_role_name.to_string().as_pg_cstr(), false) };

    let operation_str = if copy_from { "from" } else { "to" };
    let object_type = if is_file { "file" } else { "remote uri" };

    if !unsafe { has_privs_of_role(user_id, required_role_id) } {
        ereport!(
            pgrx::PgLogLevel::ERROR,
            pgrx::PgSqlErrorCode::ERRCODE_INSUFFICIENT_PRIVILEGE,
            format!(
                "permission denied to COPY {} a {}",
                operation_str, object_type
            ),
            format!(
                "Only roles with privileges of the \"{}\" role may COPY {} a {}.",
                required_role_name, operation_str, object_type
            ),
        );
    }
}
