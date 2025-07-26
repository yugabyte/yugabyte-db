use std::sync::Arc;

use object_store::{http::HttpBuilder, ClientOptions};
use url::Url;

use super::object_store_cache::ObjectStoreWithExpiration;

// create_http_object_store creates a http(s) object store with the given bucket name.
pub(crate) fn create_http_object_store(uri: &Url) -> ObjectStoreWithExpiration {
    let base_uri = parse_http_base_uri(uri).unwrap_or_else(|| {
        panic!("unsupported http uri: {}", uri);
    });

    let allow_http = std::env::var("ALLOW_HTTP").is_ok();

    let client_options = ClientOptions::new()
        .with_allow_http2()
        .with_allow_http(allow_http);

    let http_builder = HttpBuilder::new()
        .with_url(base_uri)
        .with_client_options(client_options);

    let object_store = http_builder.build().unwrap_or_else(|e| panic!("{}", e));

    let expire_at = None;

    ObjectStoreWithExpiration {
        object_store: Arc::new(object_store),
        expire_at,
    }
}

pub(crate) fn parse_http_base_uri(uri: &Url) -> Option<String> {
    let scheme = uri.scheme();

    let host = uri.host_str().expect("http uri missing host");

    let port = uri.port().map(|p| format!(":{}", p)).unwrap_or_default();

    Some(format!("{}://{}{}", scheme, host, port))
}
