use std::{panic, sync::Arc};

use arrow::datatypes::SchemaRef;
use object_store::{path::Path, ObjectStoreScheme};
use parquet::{
    arrow::{
        async_reader::{ParquetObjectReader, ParquetRecordBatchStream},
        async_writer::ParquetObjectWriter,
        ArrowSchemaConverter, AsyncArrowWriter, ParquetRecordBatchStreamBuilder,
    },
    file::{metadata::ParquetMetaData, properties::WriterProperties},
    schema::types::SchemaDescriptor,
};
use pgrx::{
    ereport,
    pg_sys::{get_role_oid, has_privs_of_role, superuser, AsPgCStr, GetUserId},
};
use url::Url;

use crate::{
    arrow_parquet::parquet_writer::DEFAULT_ROW_GROUP_SIZE,
    object_store::{
        aws::parse_s3_bucket, azure::parse_azure_blob_container, http::parse_http_base_uri,
        object_store_cache::get_or_create_object_store,
    },
    PG_BACKEND_TOKIO_RUNTIME,
};

const PARQUET_OBJECT_STORE_READ_ROLE: &str = "parquet_object_store_read";
const PARQUET_OBJECT_STORE_WRITE_ROLE: &str = "parquet_object_store_write";

// ParsedUriInfo is a struct that holds the parsed uri information.
#[derive(Debug, Clone)]
pub(crate) struct ParsedUriInfo {
    pub(crate) uri: Url,
    pub(crate) bucket: Option<String>,
    pub(crate) path: Path,
    pub(crate) scheme: ObjectStoreScheme,
}

impl ParsedUriInfo {
    fn try_parse_uri(uri: &str) -> Result<Url, String> {
        if !uri.contains("://") {
            // local file
            Url::from_file_path(uri).map_err(|_| format!("not a valid file path: {}", uri))
        } else {
            Url::parse(uri).map_err(|e| e.to_string())
        }
    }

    fn try_parse_scheme(uri: &Url) -> Result<(ObjectStoreScheme, Path), String> {
        ObjectStoreScheme::parse(uri).map_err(|_| {
            format!(
                "unrecognized uri {}. pg_parquet supports local paths, https://, s3:// or az:// schemes.",
                uri
            )
        })
    }

    fn try_parse_bucket(scheme: &ObjectStoreScheme, uri: &Url) -> Result<Option<String>, String> {
        match scheme {
            ObjectStoreScheme::AmazonS3 => parse_s3_bucket(uri)
                .ok_or(format!("unsupported s3 uri {uri}"))
                .map(Some),
            ObjectStoreScheme::MicrosoftAzure => parse_azure_blob_container(uri)
                .ok_or(format!("unsupported azure blob storage uri: {uri}"))
                .map(Some),
            ObjectStoreScheme::Http => parse_http_base_uri(uri).
                ok_or(format!("unsupported http storage uri: {uri}"))
                .map(Some),
            ObjectStoreScheme::Local => Ok(None),
            _ => Err(format!("unsupported scheme {} in uri {}. pg_parquet supports local paths, https://, s3:// or az:// schemes.",
                            uri.scheme(), uri))
        }
    }
}

impl TryFrom<&str> for ParsedUriInfo {
    type Error = String;

    fn try_from(uri: &str) -> Result<Self, Self::Error> {
        let uri = Self::try_parse_uri(uri)?;

        let (scheme, path) = Self::try_parse_scheme(&uri)?;

        let bucket = Self::try_parse_bucket(&scheme, &uri)?;

        Ok(ParsedUriInfo {
            uri: uri.clone(),
            bucket,
            path,
            scheme,
        })
    }
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

pub(crate) fn parquet_schema_from_uri(uri_info: ParsedUriInfo) -> SchemaDescriptor {
    let parquet_reader = parquet_reader_from_uri(uri_info);

    let arrow_schema = parquet_reader.schema();

    ArrowSchemaConverter::new()
        .convert(arrow_schema)
        .unwrap_or_else(|e| panic!("{}", e))
}

pub(crate) fn parquet_metadata_from_uri(uri_info: ParsedUriInfo) -> Arc<ParquetMetaData> {
    let copy_from = true;
    let (parquet_object_store, location) = get_or_create_object_store(uri_info.clone(), copy_from);

    PG_BACKEND_TOKIO_RUNTIME.block_on(async {
        let object_store_meta = parquet_object_store
            .head(&location)
            .await
            .unwrap_or_else(|e| {
                panic!(
                    "failed to get object store metadata for uri {}: {}",
                    uri_info.uri, e
                )
            });

        let parquet_object_reader =
            ParquetObjectReader::new(parquet_object_store, object_store_meta);

        let builder = ParquetRecordBatchStreamBuilder::new(parquet_object_reader)
            .await
            .unwrap_or_else(|e| panic!("{}", e));

        builder.metadata().to_owned()
    })
}

pub(crate) fn parquet_reader_from_uri(
    uri_info: ParsedUriInfo,
) -> ParquetRecordBatchStream<ParquetObjectReader> {
    let copy_from = true;
    let (parquet_object_store, location) = get_or_create_object_store(uri_info.clone(), copy_from);

    PG_BACKEND_TOKIO_RUNTIME.block_on(async {
        let object_store_meta = parquet_object_store
            .head(&location)
            .await
            .unwrap_or_else(|e| {
                panic!(
                    "failed to get object store metadata for uri {}: {}",
                    uri_info.uri, e
                )
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
    uri_info: ParsedUriInfo,
    arrow_schema: SchemaRef,
    writer_props: WriterProperties,
) -> AsyncArrowWriter<ParquetObjectWriter> {
    let copy_from = false;
    let (parquet_object_store, location) = get_or_create_object_store(uri_info.clone(), copy_from);

    let parquet_object_writer = ParquetObjectWriter::new(parquet_object_store, location);

    AsyncArrowWriter::try_new(parquet_object_writer, arrow_schema, Some(writer_props))
        .unwrap_or_else(|e| {
            panic!(
                "failed to create parquet writer for uri {}: {}",
                uri_info.uri, e
            )
        })
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
