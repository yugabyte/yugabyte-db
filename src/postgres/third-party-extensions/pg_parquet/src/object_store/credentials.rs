use std::collections::HashMap;
use std::ffi::CStr;
use std::time::{Duration, SystemTime};

use object_store::ObjectStoreScheme;
use pgrx::pg_sys::{
    pg_foreign_server_aclcheck, untransformRelOptions, ACL_USAGE, AclResult,
    Anum_pg_user_mapping_umoptions, GetUserId, HeapTuple, InvalidOid, SearchSysCache2,
    SysCacheIdentifier,
};
use pgrx::{pg_sys::Oid, PgList, Spi};
use url::Url;

use super::aws::parse_s3_bucket;
use super::gcs::parse_gcs_bucket;
use crate::arrow_parquet::uri_utils::ParsedUriInfo;

const S3_SERVER_TYPE: &str = "s3";
const GCS_SERVER_TYPE: &str = "gcs";

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct S3CredentialConfig {
    pub(crate) access_key_id: String,
    pub(crate) secret_access_key: String,
    pub(crate) session_token: Option<String>,
    pub(crate) region: Option<String>,
    pub(crate) endpoint_url: Option<String>,
    pub(crate) allow_http: bool,
    pub(crate) url_style: Option<String>,
    pub(crate) use_ssl: Option<bool>,
    pub(crate) expire_at: Option<SystemTime>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) enum GcsAuthMode {
    Hmac {
        key_id: String,
        secret: String,
    },
    ServiceAccount {
        key: Option<String>,
        path: Option<String>,
    },
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct GcsCredentialConfig {
    pub(crate) mode: GcsAuthMode,
    pub(crate) endpoint: Option<String>,
    pub(crate) expire_at: Option<SystemTime>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) enum ResolvedCredentials {
    S3(S3CredentialConfig),
    Gcs(GcsCredentialConfig),
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct ResolvedObjectStoreCreds {
    pub(crate) creds: ResolvedCredentials,
    pub(crate) server_oid: Oid,
}

impl ResolvedObjectStoreCreds {
    pub(crate) fn expire_at(&self) -> Option<SystemTime> {
        match &self.creds {
            ResolvedCredentials::S3(cfg) => cfg.expire_at,
            ResolvedCredentials::Gcs(cfg) => cfg.expire_at,
        }
    }
}

pub(crate) fn resolve_credentials(uri_info: &ParsedUriInfo) -> ResolvedObjectStoreCreds {
    let server_type = match uri_info.scheme {
        ObjectStoreScheme::AmazonS3 => S3_SERVER_TYPE,
        ObjectStoreScheme::GoogleCloudStorage => GCS_SERVER_TYPE,
        _ => pgrx::error!(
            "catalog-based credential resolution not supported for uri {}",
            uri_info.uri
        ),
    };

    let bucket = uri_info
        .bucket
        .clone()
        .unwrap_or_else(|| bucket_from_uri(server_type, &uri_info.uri));

    let object_path = uri_info.path.to_string();
    let normalized_path = object_path.trim_start_matches('/');

    let candidates = list_parquet_servers(server_type);
    let mut matches = Vec::new();

    for server in candidates {
        if !server_matches_uri(&server, &bucket, normalized_path, &uri_info.uri) {
            continue;
        }
        if !user_has_server_usage(server.server_oid) {
            continue;
        }
        let userid = unsafe { GetUserId() };
        let Some(mapping_options) = find_user_mapping_options(userid, server.server_oid) else {
            continue;
        };
        matches.push((server, mapping_options));
    }

    if matches.is_empty() {
        pgrx::error!(
            "no parquet {} foreign server and user mapping match uri {} for current user",
            server_type,
            uri_info.uri
        );
    }
    if matches.len() > 1 {
        let server_names = matches
            .iter()
            .map(|(server, _)| server.server_name.as_str())
            .collect::<Vec<_>>()
            .join(", ");
        pgrx::error!(
            "multiple parquet {} foreign servers match uri {}: {}",
            server_type,
            uri_info.uri,
            server_names
        );
    }

    let (server, mapping_options) = matches.remove(0);
    let mut options = server.server_options;
    options.extend(mapping_options);

    let creds = match server_type {
        S3_SERVER_TYPE => ResolvedCredentials::S3(options_to_s3_config(&options)),
        GCS_SERVER_TYPE => ResolvedCredentials::Gcs(options_to_gcs_config(&options)),
        _ => unreachable!(),
    };

    ResolvedObjectStoreCreds {
        creds,
        server_oid: server.server_oid,
    }
}

fn bucket_from_uri(server_type: &str, uri: &Url) -> String {
    let parsed = match server_type {
        S3_SERVER_TYPE => parse_s3_bucket(uri),
        GCS_SERVER_TYPE => parse_gcs_bucket(uri),
        _ => None,
    };
    parsed.unwrap_or_else(|| {
        pgrx::error!(
            "could not determine bucket for {} uri {}",
            server_type,
            uri
        )
    })
}

struct ParquetServer {
    server_oid: Oid,
    server_name: String,
    server_options: HashMap<String, String>,
}

fn list_parquet_servers(server_type: &str) -> Vec<ParquetServer> {
    let server_type_owned = server_type.to_string();
    Spi::connect(|client| {
        let mut servers = Vec::new();
        let table = client
            .select(
                "SELECT fs.oid::oid, fs.srvname::text, fs.srvoptions \
                 FROM pg_catalog.pg_foreign_server fs \
                 INNER JOIN pg_catalog.pg_foreign_data_wrapper fdw ON fdw.oid = fs.srvfdw \
                 WHERE fdw.fdwname = 'parquet' AND fs.srvtype = $1",
                None,
                &[server_type_owned.as_str().into()],
            )
            .expect("failed to list parquet foreign servers");

        for row in table {
            let server_oid: Oid = row.get(1)?.expect("server oid");
            let server_name: String = row.get(2)?.expect("server name");
            let server_options: Option<Vec<String>> = row.get(3)?;
            let server_options = parse_text_array_options(server_options.unwrap_or_default());

            servers.push(ParquetServer {
                server_oid,
                server_name,
                server_options,
            });
        }

        Ok::<_, pgrx::spi::Error>(servers)
    })
    .expect("failed to list parquet foreign servers")
}

fn parse_text_array_options(options: Vec<String>) -> HashMap<String, String> {
    let mut parsed = HashMap::new();
    for option in options {
        let Some((key, value)) = option.split_once('=') else {
            continue;
        };
        parsed.insert(key.to_ascii_lowercase(), value.to_string());
    }
    parsed
}

fn server_matches_uri(
    server: &ParquetServer,
    bucket: &str,
    object_path: &str,
    uri: &Url,
) -> bool {
    if let Some(scope) = server.server_options.get("scope") {
        if let Some((scope_bucket, scope_prefix)) = scope.split_once('/') {
            if scope_bucket != bucket {
                return false;
            }
            if !scope_prefix.is_empty() && !object_path.starts_with(scope_prefix) {
                return false;
            }
        } else if scope != bucket {
            return false;
        }
    }

    if let Some(endpoint) = server.server_options.get("endpoint") {
        if !uri_endpoint_matches(uri, endpoint) {
            return false;
        }
    }

    true
}

fn uri_endpoint_matches(uri: &Url, endpoint: &str) -> bool {
    if uri.scheme() == "s3" || uri.scheme() == "gs" {
        return true;
    }

    let Ok(endpoint_url) = Url::parse(endpoint) else {
        return false;
    };

    uri.host_str() == endpoint_url.host_str()
}

fn user_has_server_usage(server_oid: Oid) -> bool {
    unsafe {
        let acl_result = pg_foreign_server_aclcheck(server_oid, GetUserId(), ACL_USAGE);
        acl_result == AclResult::ACLCHECK_OK
    }
}

fn find_user_mapping_options(userid: Oid, server_oid: Oid) -> Option<HashMap<String, String>> {
    find_user_mapping(userid, server_oid)
        .or_else(|| find_user_mapping(InvalidOid, server_oid))
}

fn find_user_mapping(userid: Oid, server_oid: Oid) -> Option<HashMap<String, String>> {
    unsafe {
        // Note: don't go through `Oid::into_datum()` here -- it maps
        // `InvalidOid` to SQL NULL, but the PUBLIC user mapping is stored
        // with `umuser = 0` (InvalidOid) as a real datum value. We need to
        // pass the raw OID through as an Oid datum so SearchSysCache2 finds
        // the PUBLIC mapping (which we fall back to in
        // `find_user_mapping_options`).
        let tuple = SearchSysCache2(
            SysCacheIdentifier::USERMAPPINGUSERSERVER as i32,
            pgrx::pg_sys::Datum::from(userid.to_u32()),
            pgrx::pg_sys::Datum::from(server_oid.to_u32()),
        );
        if tuple.is_null() {
            return None;
        }

        let options = read_user_mapping_options(tuple);
        pgrx::pg_sys::ReleaseSysCache(tuple);
        options
    }
}

unsafe fn read_user_mapping_options(tuple: HeapTuple) -> Option<HashMap<String, String>> {
    let mut is_null = false;
    let datum = pgrx::pg_sys::SysCacheGetAttr(
        SysCacheIdentifier::USERMAPPINGUSERSERVER as i32,
        tuple,
        Anum_pg_user_mapping_umoptions as i16,
        &mut is_null,
    );
    if is_null {
        return Some(HashMap::new());
    }

    let options_list = untransformRelOptions(datum);
    if options_list.is_null() {
        return Some(HashMap::new());
    }

    let options = PgList::<pgrx::pg_sys::DefElem>::from_pg(options_list);
    let mut parsed = HashMap::new();
    for i in 0..options.len() {
        let def = options.get_ptr(i)?;
        let defname = CStr::from_ptr((*def).defname)
            .to_str()
            .ok()?
            .to_ascii_lowercase();
        let value = CStr::from_ptr(pgrx::pg_sys::defGetString(def))
            .to_str()
            .ok()?
            .to_string();
        parsed.insert(defname, value);
    }

    Some(parsed)
}

fn options_to_s3_config(options: &HashMap<String, String>) -> S3CredentialConfig {
    let access_key_id = options
        .get("key_id")
        .cloned()
        .unwrap_or_else(|| pgrx::error!("parquet S3 user mapping is missing key_id"));
    let secret_access_key = options
        .get("secret")
        .cloned()
        .unwrap_or_else(|| pgrx::error!("parquet S3 user mapping is missing secret"));

    let allow_http = options
        .get("allow_http")
        .map(|value| value.eq_ignore_ascii_case("true") || value == "1")
        .unwrap_or(false);
    let use_ssl = options
        .get("use_ssl")
        .map(|value| value.eq_ignore_ascii_case("true") || value == "1");

    let expire_at = options
        .get("session_token")
        .is_some()
        .then(|| SystemTime::now() + Duration::from_secs(3600));

    S3CredentialConfig {
        access_key_id,
        secret_access_key,
        session_token: options.get("session_token").cloned(),
        region: options.get("region").cloned(),
        endpoint_url: options.get("endpoint").cloned(),
        allow_http,
        url_style: options.get("url_style").cloned(),
        use_ssl,
        expire_at,
    }
}

fn options_to_gcs_config(options: &HashMap<String, String>) -> GcsCredentialConfig {
    let key_id = options.get("key_id").cloned();
    let secret = options.get("secret").cloned();
    let service_account_key = options.get("service_account_key").cloned();
    let service_account_path = options.get("service_account_path").cloned();

    let mode = if key_id.is_some() || secret.is_some() {
        let key_id = key_id.unwrap_or_else(|| {
            pgrx::error!("parquet GCS user mapping with HMAC auth requires key_id")
        });
        let secret = secret.unwrap_or_else(|| {
            pgrx::error!("parquet GCS user mapping with HMAC auth requires secret")
        });
        if service_account_key.is_some() || service_account_path.is_some() {
            pgrx::error!(
                "parquet GCS user mapping cannot mix HMAC and service-account options"
            );
        }
        GcsAuthMode::Hmac { key_id, secret }
    } else if service_account_key.is_some() || service_account_path.is_some() {
        if service_account_key.is_some() && service_account_path.is_some() {
            pgrx::error!(
                "parquet GCS user mapping may set service_account_key or service_account_path, not both"
            );
        }
        GcsAuthMode::ServiceAccount {
            key: service_account_key,
            path: service_account_path,
        }
    } else {
        pgrx::error!(
            "parquet GCS user mapping requires either (key_id, secret) or (service_account_key or service_account_path)"
        );
    };

    let expire_at = options
        .get("session_token")
        .is_some()
        .then(|| SystemTime::now() + Duration::from_secs(3600));

    GcsCredentialConfig {
        mode,
        endpoint: options.get("endpoint").cloned(),
        expire_at,
    }
}
