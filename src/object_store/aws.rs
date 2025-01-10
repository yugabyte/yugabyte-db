use aws_config::BehaviorVersion;
use aws_credential_types::provider::ProvideCredentials;
use object_store::aws::{AmazonS3, AmazonS3Builder};
use url::Url;

use super::PG_BACKEND_TOKIO_RUNTIME;

// create_s3_object_store creates an AmazonS3 object store with the given bucket name.
// It is configured by environment variables and aws config files as fallback method.
// We need to read the config files to make the fallback method work since object_store
// does not provide a way to read them. Currently, we only support following environment
// variables and config parameters:
// - AWS_ACCESS_KEY_ID
// - AWS_SECRET_ACCESS_KEY
// - AWS_SESSION_TOKEN
// - AWS_ENDPOINT_URL
// - AWS_REGION
// - AWS_SHARED_CREDENTIALS_FILE (env var only)
// - AWS_CONFIG_FILE (env var only)
// - AWS_PROFILE (env var only)
// - AWS_ALLOW_HTTP (env var only, object_store specific)
pub(crate) fn create_s3_object_store(uri: &Url) -> AmazonS3 {
    let bucket_name = parse_s3_bucket(uri).unwrap_or_else(|| {
        panic!("unsupported s3 uri: {}", uri);
    });

    // we do not use builder::from_env() here because not all environment variables have
    // a fallback to the config files
    let mut aws_s3_builder = AmazonS3Builder::new().with_bucket_name(bucket_name);

    let aws_s3_config = AwsS3Config::load();

    // allow http
    aws_s3_builder = aws_s3_builder.with_allow_http(aws_s3_config.allow_http);

    // access key id
    if let Some(access_key_id) = aws_s3_config.access_key_id {
        aws_s3_builder = aws_s3_builder.with_access_key_id(access_key_id);
    }

    // secret access key
    if let Some(secret_access_key) = aws_s3_config.secret_access_key {
        aws_s3_builder = aws_s3_builder.with_secret_access_key(secret_access_key);
    }

    // session token
    if let Some(session_token) = aws_s3_config.session_token {
        aws_s3_builder = aws_s3_builder.with_token(session_token);
    }

    // endpoint url
    if let Some(endpoint_url) = aws_s3_config.endpoint_url {
        aws_s3_builder = aws_s3_builder.with_endpoint(endpoint_url);
    }

    // region
    if let Some(region) = aws_s3_config.region {
        aws_s3_builder = aws_s3_builder.with_region(region);
    }

    aws_s3_builder.build().unwrap_or_else(|e| panic!("{}", e))
}

fn parse_s3_bucket(uri: &Url) -> Option<String> {
    let host = uri.host_str()?;

    // s3(a)://{bucket}/key
    if uri.scheme() == "s3" {
        return Some(host.to_string());
    }
    // https://s3.amazonaws.com/{bucket}/key
    else if host == "s3.amazonaws.com" {
        let path_segments: Vec<&str> = uri.path_segments()?.collect();

        // Bucket name is the first part of the path
        return Some(
            path_segments
                .first()
                .expect("unexpected error during parsing s3 uri")
                .to_string(),
        );
    }
    // https://{bucket}.s3.amazonaws.com/key
    else if host.ends_with(".s3.amazonaws.com") {
        let bucket_name = host.split('.').next()?;
        return Some(bucket_name.to_string());
    }

    None
}

// AwsS3Config is a struct that holds the configuration that is
// used to configure the AmazonS3 object store. object_store does
// not provide a way to read the config files, so we need to read
// them ourselves via aws sdk.
struct AwsS3Config {
    region: Option<String>,
    access_key_id: Option<String>,
    secret_access_key: Option<String>,
    session_token: Option<String>,
    endpoint_url: Option<String>,
    allow_http: bool,
}

impl AwsS3Config {
    // load reads the s3 config from the environment variables first and config files as fallback.
    fn load() -> Self {
        let allow_http = if let Ok(allow_http) = std::env::var("AWS_ALLOW_HTTP") {
            allow_http.parse().unwrap_or(false)
        } else {
            false
        };

        // first tries environment variables and then the config files
        let sdk_config = PG_BACKEND_TOKIO_RUNTIME.block_on(async {
            aws_config::defaults(BehaviorVersion::v2024_03_28())
                .load()
                .await
        });

        let mut access_key_id = None;
        let mut secret_access_key = None;
        let mut session_token = None;

        if let Some(credential_provider) = sdk_config.credentials_provider() {
            if let Ok(credentials) = PG_BACKEND_TOKIO_RUNTIME
                .block_on(async { credential_provider.provide_credentials().await })
            {
                access_key_id = Some(credentials.access_key_id().to_string());
                secret_access_key = Some(credentials.secret_access_key().to_string());
                session_token = credentials.session_token().map(|t| t.to_string());
            }
        }

        let endpoint_url = sdk_config.endpoint_url().map(|u| u.to_string());

        let region = sdk_config.region().map(|r| r.as_ref().to_string());

        Self {
            region,
            access_key_id,
            secret_access_key,
            session_token,
            endpoint_url,
            allow_http,
        }
    }
}
