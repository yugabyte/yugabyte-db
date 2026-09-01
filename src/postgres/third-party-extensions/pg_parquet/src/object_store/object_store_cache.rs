use std::{
    collections::HashMap,
    hash::{Hash, Hasher},
    sync::Arc,
    time::SystemTime,
};

use object_store::{path::Path, ObjectStore, ObjectStoreScheme};
use once_cell::sync::Lazy;
use pgrx::{
    ereport,
    pg_guard,
    pg_sys::{Datum, GetUserId, SysCacheIdentifier},
    PgLogLevel, PgSqlErrorCode,
};

extern "C" {
    fn CacheRegisterSyscacheCallback(
        cacheid: i32,
        func: Option<unsafe extern "C-unwind" fn(Datum, i32, u32)>,
        arg: Datum,
    );
}
use crate::arrow_parquet::uri_utils::ParsedUriInfo;

use super::{
    aws::create_s3_object_store,
    azure::create_azure_object_store,
    credentials::{resolve_credentials, ResolvedCredentials, ResolvedObjectStoreCreds},
    gcs::create_gcs_object_store,
    http::create_http_object_store,
    local_file::create_local_file_object_store,
};

type SyscacheCallbackFunction = Option<unsafe extern "C-unwind" fn(Datum, i32, u32)>;

static mut OBJECT_STORE_CACHE: Lazy<ObjectStoreCache> = Lazy::new(ObjectStoreCache::new);
static mut SYSCACHE_CALLBACK_REGISTERED: bool = false;

pub(crate) fn get_or_create_object_store(
    uri_info: &ParsedUriInfo,
    copy_from: bool,
) -> (Arc<dyn ObjectStore>, Path) {
    register_syscache_callbacks();
    #[allow(static_mut_refs)]
    unsafe {
        OBJECT_STORE_CACHE.get_or_create(uri_info, copy_from)
    }
}

pub(crate) fn clear_object_store_cache() {
    #[allow(static_mut_refs)]
    unsafe {
        OBJECT_STORE_CACHE.cache.clear();
    }
}

fn register_syscache_callbacks() {
    unsafe {
        if SYSCACHE_CALLBACK_REGISTERED {
            return;
        }
        SYSCACHE_CALLBACK_REGISTERED = true;
        CacheRegisterSyscacheCallback(
            SysCacheIdentifier::USERMAPPINGUSERSERVER as i32,
            Some(invalidate_object_store_cache),
            Datum::from(0),
        );
        CacheRegisterSyscacheCallback(
            SysCacheIdentifier::FOREIGNSERVEROID as i32,
            Some(invalidate_object_store_cache),
            Datum::from(0),
        );
    }
}

#[pg_guard]
unsafe extern "C-unwind" fn invalidate_object_store_cache(_arg: Datum, _cacheid: i32, _hashvalue: u32) {
    clear_object_store_cache();
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
        let scheme = uri_info.scheme.clone();
        let bucket = uri_info.bucket.clone();
        let path = uri_info.path.clone();

        if scheme == ObjectStoreScheme::Local {
            let item = Self::create(scheme, uri_info, copy_from, None);
            return (item.object_store, path);
        }

        let bucket = bucket.expect("bucket is None");

        let resolved = match scheme {
            ObjectStoreScheme::AmazonS3 | ObjectStoreScheme::GoogleCloudStorage => {
                Some(resolve_credentials(uri_info))
            }
            _ => None,
        };

        let key = ObjectStoreCacheKey {
            scheme: scheme.clone(),
            bucket: bucket.clone(),
            role_oid: unsafe { GetUserId() },
            server_oid: resolved.as_ref().map(|r| r.server_oid),
        };

        if let Some(item) = self.cache.get(&key) {
            if item.expired(&bucket) {
                self.cache.remove(&key);
            } else {
                return (item.object_store.clone(), path);
            }
        }

        let item = Self::create(scheme, uri_info, copy_from, resolved.as_ref());

        self.cache.insert(key, item.clone());

        (item.object_store.clone(), path)
    }

    fn create(
        scheme: ObjectStoreScheme,
        uri_info: &ParsedUriInfo,
        copy_from: bool,
        resolved: Option<&ResolvedObjectStoreCreds>,
    ) -> ObjectStoreWithExpiration {
        let uri = &uri_info.uri;
        match scheme {
            ObjectStoreScheme::AmazonS3 => {
                let resolved = resolved
                    .expect("resolved S3 credentials must be available for AmazonS3 scheme");
                match &resolved.creds {
                    ResolvedCredentials::S3(config) => create_s3_object_store(uri, config),
                    _ => panic!("expected resolved S3 credentials for AmazonS3 scheme"),
                }
            }
            ObjectStoreScheme::GoogleCloudStorage => {
                let resolved = resolved
                    .expect("resolved GCS credentials must be available for GoogleCloudStorage scheme");
                match &resolved.creds {
                    ResolvedCredentials::Gcs(config) => create_gcs_object_store(uri, config),
                    _ => panic!("expected resolved GCS credentials for GoogleCloudStorage scheme"),
                }
            }
            ObjectStoreScheme::MicrosoftAzure => create_azure_object_store(uri),
            ObjectStoreScheme::Http => create_http_object_store(uri),
            ObjectStoreScheme::Local => create_local_file_object_store(uri, copy_from),
            _ => panic!(
                "unsupported scheme {} in uri {}. pg_parquet supports local paths, https://, s3://, az:// or gs:// schemes.",
                uri.scheme(),
                uri
            ),
        }
    }
}

#[derive(Clone)]
pub(crate) struct ObjectStoreWithExpiration {
    pub(crate) object_store: Arc<dyn object_store::ObjectStore>,
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

#[derive(Clone, Eq, PartialEq)]
struct ObjectStoreCacheKey {
    scheme: ObjectStoreScheme,
    bucket: String,
    role_oid: pgrx::pg_sys::Oid,
    server_oid: Option<pgrx::pg_sys::Oid>,
}

impl Hash for ObjectStoreCacheKey {
    fn hash<H: Hasher>(&self, state: &mut H) {
        let schema_tag = self.scheme.clone() as i32;
        schema_tag.hash(state);
        self.bucket.hash(state);
        self.role_oid.hash(state);
        self.server_oid.hash(state);
    }
}

#[cfg(feature = "pg_test")]
#[pgrx::pg_schema]
mod parquet_test {
    use std::time::UNIX_EPOCH;

    use pgrx::{iter::TableIterator, name, pg_extern, pg_sys::Timestamp};

    use super::OBJECT_STORE_CACHE;

    #[pg_extern]
    fn object_store_cache_clear() {
        super::clear_object_store_cache();
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
            name!(role_oid, String),
            name!(server_oid, Option<String>),
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
                let role_oid = key.role_oid.to_string();
                let server_oid = key.server_oid.map(|oid| oid.to_string());
                let expire_at = value
                    .expire_at
                    .map(|t| t.duration_since(UNIX_EPOCH).unwrap().as_micros() as Timestamp);

                (scheme, bucket, role_oid, server_oid, expire_at)
            })
            .collect::<Vec<_>>();

        TableIterator::new(rows)
    }
}
