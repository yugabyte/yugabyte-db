use std::sync::Arc;

use object_store::aws::AmazonS3Builder;
use object_store::gcp::GoogleCloudStorageBuilder;
use url::Url;

use super::credentials::{GcsAuthMode, GcsCredentialConfig};
use super::object_store_cache::ObjectStoreWithExpiration;

const GCS_DEFAULT_HMAC_ENDPOINT: &str = "https://storage.googleapis.com";

pub(crate) fn create_gcs_object_store(
    uri: &Url,
    config: &GcsCredentialConfig,
) -> ObjectStoreWithExpiration {
    let bucket_name = parse_gcs_bucket(uri).unwrap_or_else(|| {
        panic!("unsupported gcs uri: {}", uri);
    });

    match &config.mode {
        GcsAuthMode::ServiceAccount { key, path } => {
            let mut gcs_builder =
                GoogleCloudStorageBuilder::new().with_bucket_name(bucket_name.clone());

            if let Some(service_account_key) = key {
                gcs_builder = gcs_builder.with_service_account_key(service_account_key);
            }

            if let Some(service_account_path) = path {
                gcs_builder = gcs_builder.with_service_account_path(service_account_path);
            }

            let object_store = gcs_builder.build().unwrap_or_else(|e| panic!("{}", e));

            ObjectStoreWithExpiration {
                object_store: Arc::new(object_store),
                expire_at: config.expire_at,
            }
        }
        GcsAuthMode::Hmac { key_id, secret } => {
            let endpoint = config
                .endpoint
                .clone()
                .unwrap_or_else(|| GCS_DEFAULT_HMAC_ENDPOINT.to_string());

            let object_store = AmazonS3Builder::new()
                .with_bucket_name(bucket_name)
                .with_endpoint(endpoint)
                .with_virtual_hosted_style_request(false)
                .with_access_key_id(key_id.clone())
                .with_secret_access_key(secret.clone())
                .build()
                .unwrap_or_else(|e| panic!("{}", e));

            ObjectStoreWithExpiration {
                object_store: Arc::new(object_store),
                expire_at: config.expire_at,
            }
        }
    }
}

pub(crate) fn parse_gcs_bucket(uri: &Url) -> Option<String> {
    let host = uri.host_str()?;

    if uri.scheme() == "gs" {
        return Some(host.to_string());
    }

    None
}
