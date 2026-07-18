use std::{
    collections::HashMap,
    hash::{Hash, Hasher},
    sync::Arc,
    time::SystemTime,
};

use object_store::{path::Path, ObjectStore, ObjectStoreScheme};
use once_cell::sync::Lazy;
use pgrx::{ereport, PgLogLevel, PgSqlErrorCode};
use url::Url;

use crate::arrow_parquet::uri_utils::ParsedUriInfo;

use super::{
    aws::create_s3_object_store, azure::create_azure_object_store, gcs::create_gcs_object_store,
    http::create_http_object_store, local_file::create_local_file_object_store,
};

// OBJECT_STORE_CACHE is a global cache for object stores per Postgres session.
// It caches object stores based on the scheme and bucket.
// Local paths are not cached.
static mut OBJECT_STORE_CACHE: Lazy<ObjectStoreCache> = Lazy::new(ObjectStoreCache::new);

pub(crate) fn get_or_create_object_store(
    uri_info: &ParsedUriInfo,
    copy_from: bool,
) -> (Arc<dyn ObjectStore>, Path) {
    #[allow(static_mut_refs)]
    unsafe {
        OBJECT_STORE_CACHE.get_or_create(uri_info, copy_from)
    }
}

struct ObjectStoreCache {
    cache: HashMap<ObjectStoreCacheKey, ObjectStoreWithExpiration>,
}

impl ObjectStoreCache {
    fn new() -> Self {
        Self {
            cache: HashMap::new(),
        }
    }

    fn get_or_create(
        &mut self,
        uri_info: &ParsedUriInfo,
        copy_from: bool,
    ) -> (Arc<dyn ObjectStore>, Path) {
        let uri = uri_info.uri.clone();
        let scheme = uri_info.scheme.clone();
        let bucket = uri_info.bucket.clone();
        let path = uri_info.path.clone();

        // no need to cache local files
        if scheme == ObjectStoreScheme::Local {
            let item = Self::create(scheme, &uri, copy_from);
            return (item.object_store, path);
        }

        let bucket = bucket.expect("bucket is None");

        let key = ObjectStoreCacheKey {
            scheme: scheme.clone(),
            bucket,
        };

        if let Some(item) = self.cache.get(&key) {
            if item.expired(&key.bucket) {
                self.cache.remove(&key);
            } else {
                return (item.object_store.clone(), path);
            }
        }

        let item = Self::create(scheme, &uri, copy_from);

        self.cache.insert(key, item.clone());

        (item.object_store.clone(), path)
    }

    fn create(scheme: ObjectStoreScheme, uri: &Url, copy_from: bool) -> ObjectStoreWithExpiration {
        // object_store crate can recognize a bunch of different schemes and paths, but we only support
        // local, s3, azure, https and gs schemes with a subset of all supported paths.
        match scheme {
            ObjectStoreScheme::AmazonS3 => create_s3_object_store(uri),
            ObjectStoreScheme::MicrosoftAzure => create_azure_object_store(uri),
            ObjectStoreScheme::Http => create_http_object_store(uri),
            ObjectStoreScheme::GoogleCloudStorage => create_gcs_object_store(uri),
            ObjectStoreScheme::Local => create_local_file_object_store(uri, copy_from),
            _ => panic!(
                    "unsupported scheme {} in uri {}. pg_parquet supports local paths, https://, s3://, az:// or gs:// schemes.",
                    uri.scheme(),
                    uri
                ),
        }
    }
}

// ObjectStoreWithExpiration is a value for the object store cache map.
#[derive(Clone)]
pub(crate) struct ObjectStoreWithExpiration {
    pub(crate) object_store: Arc<dyn object_store::ObjectStore>,

    // expiration time (if not applicable, the object_store will never expire)
    pub(crate) expire_at: Option<SystemTime>,
}

impl ObjectStoreWithExpiration {
    fn expired(&self, bucket: &str) -> bool {
        if let Some(expire_at) = self.expire_at {
            let expired = expire_at < SystemTime::now();

            if expired {
                ereport!(
                    PgLogLevel::DEBUG2,
                    PgSqlErrorCode::ERRCODE_WARNING,
                    format!("credentials for {bucket} expired at {expire_at:?}"),
                );
            }

            expired
        } else {
            false
        }
    }
}

// ObjectStoreCacheKey is a key for the object store cache map
// We cache object stores based on the scheme and bucket.
// i.e. 1 object store per scheme and bucket.
#[derive(Clone, Eq, PartialEq)]
struct ObjectStoreCacheKey {
    scheme: ObjectStoreScheme,
    bucket: String,
}

impl Hash for ObjectStoreCacheKey {
    fn hash<H: Hasher>(&self, state: &mut H) {
        let schema_tag = self.scheme.clone() as i32;
        schema_tag.hash(state);
        self.bucket.hash(state);
    }
}

// The following udfs are only used for testing purposes.
#[cfg(feature = "pg_test")]
#[pgrx::pg_schema]
mod parquet_test {
    use std::time::UNIX_EPOCH;

    use pgrx::{iter::TableIterator, name, pg_extern, pg_sys::Timestamp};

    use super::OBJECT_STORE_CACHE;

    #[pg_extern]
    fn object_store_cache_clear() {
        #[allow(static_mut_refs)]
        unsafe {
            OBJECT_STORE_CACHE.cache.clear();
        }
    }

    #[pg_extern]
    fn object_store_cache_expire_bucket(bucket: &str) {
        #[allow(static_mut_refs)]
        let cache = unsafe { &mut super::OBJECT_STORE_CACHE.cache };

        cache.retain(|key, _| key.bucket != bucket);
    }

    #[pg_extern]
    fn object_store_cache_items() -> TableIterator<
        'static,
        (
            name!(scheme, String),
            name!(bucket, String),
            name!(expire_at, Option<Timestamp>),
        ),
    > {
        #[allow(static_mut_refs)]
        let cache = unsafe { &super::OBJECT_STORE_CACHE.cache };

        let rows = cache
            .iter()
            .map(|(key, value)| {
                let scheme = format!("{:?}", key.scheme);
                let bucket = key.bucket.clone();
                let expire_at = value
                    .expire_at
                    .map(|t| t.duration_since(UNIX_EPOCH).unwrap().as_micros() as Timestamp);

                (scheme, bucket, expire_at)
            })
            .collect::<Vec<_>>();

        TableIterator::new(rows)
    }
}
