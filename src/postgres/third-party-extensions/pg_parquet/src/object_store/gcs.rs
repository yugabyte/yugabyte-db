use std::sync::Arc;

use object_store::gcp::GoogleCloudStorageBuilder;
use url::Url;

use super::object_store_cache::ObjectStoreWithExpiration;

// create_gcs_object_store a GoogleCloudStorage object store from given uri.
// It is configured by environment variables. Currently, we only support
// following environment variables:
// - GOOGLE_SERVICE_ACCOUNT_KEY
// - GOOGLE_SERVICE_ACCOUNT_PATH
pub(crate) fn create_gcs_object_store(uri: &Url) -> ObjectStoreWithExpiration {
    let bucket_name = parse_gcs_bucket(uri).unwrap_or_else(|| {
        panic!("unsupported gcs uri: {}", uri);
    });

    let mut gcs_builder = GoogleCloudStorageBuilder::new().with_bucket_name(bucket_name);

    let gcs_config = GoogleStorageConfig::load();

    // service account key
    if let Some(service_account_key) = gcs_config.service_account_key {
        gcs_builder = gcs_builder.with_service_account_key(&service_account_key);
    }

    // service account path
    if let Some(service_account_path) = gcs_config.service_account_path {
        gcs_builder = gcs_builder.with_service_account_path(&service_account_path);
    }

    let object_store = gcs_builder.build().unwrap_or_else(|e| panic!("{}", e));

    // object store handles refreshing bearer token, so we do not need to handle expiry here
    let expire_at = None;

    ObjectStoreWithExpiration {
        object_store: Arc::new(object_store),
        expire_at,
    }
}

pub(crate) fn parse_gcs_bucket(uri: &Url) -> Option<String> {
    let host = uri.host_str()?;

    // gs://{bucket}/key
    if uri.scheme() == "gs" {
        return Some(host.to_string());
    }

    None
}

// GoogleStorageConfig is a struct that holds the configuration that is
// used to configure the Google Storage object store.
struct GoogleStorageConfig {
    service_account_key: Option<String>,
    service_account_path: Option<String>,
}

impl GoogleStorageConfig {
    // load loads the Google Storage configuration from the environment.
    fn load() -> Self {
        Self {
            service_account_key: std::env::var("GOOGLE_SERVICE_ACCOUNT_KEY").ok(),
            service_account_path: std::env::var("GOOGLE_SERVICE_ACCOUNT_PATH").ok(),
        }
    }
}
