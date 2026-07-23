use std::sync::Arc;

use object_store::aws::AmazonS3Builder;
use url::Url;

use super::{
    credentials::S3CredentialConfig,
    object_store_cache::ObjectStoreWithExpiration,
};

pub(crate) fn create_s3_object_store(
    uri: &Url,
    config: &S3CredentialConfig,
) -> ObjectStoreWithExpiration {
    let bucket_name = parse_s3_bucket(uri).unwrap_or_else(|| {
        panic!("unsupported s3 uri: {}", uri);
    });

    let mut aws_s3_builder = AmazonS3Builder::new().with_bucket_name(bucket_name);

    aws_s3_builder = aws_s3_builder.with_allow_http(config.allow_http);
    aws_s3_builder = aws_s3_builder.with_access_key_id(config.access_key_id.clone());
    aws_s3_builder = aws_s3_builder.with_secret_access_key(config.secret_access_key.clone());

    if let Some(session_token) = &config.session_token {
        aws_s3_builder = aws_s3_builder.with_token(session_token.clone());
    }

    if let Some(endpoint_url) = &config.endpoint_url {
        aws_s3_builder = aws_s3_builder.with_endpoint(endpoint_url.clone());
    }

    if let Some(region) = &config.region {
        aws_s3_builder = aws_s3_builder.with_region(region.clone());
    }

    let object_store = aws_s3_builder.build().unwrap_or_else(|e| panic!("{}", e));

    ObjectStoreWithExpiration {
        object_store: Arc::new(object_store),
        expire_at: config.expire_at,
    }
}

pub(crate) fn parse_s3_bucket(uri: &Url) -> Option<String> {
    let host = uri.host_str()?;

    if uri.scheme() == "s3" {
        return Some(host.to_string());
    } else if host == "s3.amazonaws.com" {
        let path_segments: Vec<&str> = uri.path_segments()?.collect();

        return Some(
            path_segments
                .first()
                .expect("unexpected error during parsing s3 uri")
                .to_string(),
        );
    } else if host.ends_with(".s3.amazonaws.com") {
        let bucket_name = host.split('.').next()?;
        return Some(bucket_name.to_string());
    }

    None
}
