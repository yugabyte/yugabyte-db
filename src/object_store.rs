use std::sync::Arc;

use object_store::{path::Path, ObjectStore, ObjectStoreScheme};
use url::Url;

use crate::{
    arrow_parquet::uri_utils::uri_as_string,
    object_store::{
        aws::create_s3_object_store, azure::create_azure_object_store,
        local_file::create_local_file_object_store,
    },
    PG_BACKEND_TOKIO_RUNTIME,
};

pub(crate) mod aws;
pub(crate) mod azure;
pub(crate) mod local_file;

pub(crate) fn create_object_store(uri: &Url, copy_from: bool) -> (Arc<dyn ObjectStore>, Path) {
    let (scheme, path) = ObjectStoreScheme::parse(uri).unwrap_or_else(|_| {
        panic!(
            "unrecognized uri {}. pg_parquet supports local paths, s3:// or azure:// schemes.",
            uri
        )
    });

    // object_store crate can recognize a bunch of different schemes and paths, but we only support
    // local, azure, and s3 schemes with a subset of all supported paths.
    match scheme {
        ObjectStoreScheme::AmazonS3 => {
            let storage_container = Arc::new(create_s3_object_store(uri));

            (storage_container, path)
        }
        ObjectStoreScheme::MicrosoftAzure => {
            let storage_container = Arc::new(create_azure_object_store(uri));

            (storage_container, path)
        }
        ObjectStoreScheme::Local => {
            let storage_container = Arc::new(create_local_file_object_store(uri, copy_from));

            let path =
                Path::from_filesystem_path(uri_as_string(uri)).unwrap_or_else(|e| panic!("{}", e));

            (storage_container, path)
        }
        _ => {
            panic!(
                "unsupported scheme {} in uri {}. pg_parquet supports local paths, s3:// or azure:// schemes.",
                uri.scheme(),
                uri
            );
        }
    }
}
