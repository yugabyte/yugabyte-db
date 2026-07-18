use std::sync::Arc;

use azure_storage::{ConnectionString, EndpointProtocol};
use home::home_dir;
use ini::Ini;
use object_store::azure::{AzureConfigKey, MicrosoftAzureBuilder};
use url::Url;

use super::object_store_cache::ObjectStoreWithExpiration;

// create_azure_object_store creates a MicrosoftAzure object store with the given container name.
// It is configured by environment variables and azure config files as fallback method.
// We need to read the config files to make the fallback method work since object_store
// does not provide a way to read them. Currently, we only support following environment
// variables and config parameters:
// - AZURE_STORAGE_ACCOUNT
// - AZURE_STORAGE_KEY
// - AZURE_STORAGE_CONNECTION_STRING
// - AZURE_STORAGE_SAS_TOKEN
// - AZURE_CONFIG_FILE (env var only, object_store specific)
// - AZURE_STORAGE_ENDPOINT (env var only, object_store specific)
// - AZURE_ALLOW_HTTP (env var only, object_store specific)
pub(crate) fn create_azure_object_store(uri: &Url) -> ObjectStoreWithExpiration {
    let container_name = parse_azure_blob_container(uri).unwrap_or_else(|| {
        panic!("unsupported azure blob storage uri: {}", uri);
    });

    let mut azure_builder = MicrosoftAzureBuilder::new().with_container_name(container_name);

    let azure_blob_config = AzureStorageConfig::load();

    // allow http
    azure_builder = azure_builder.with_allow_http(azure_blob_config.allow_http);

    // endpoint
    if let Some(endpoint) = azure_blob_config.endpoint {
        azure_builder = azure_builder.with_endpoint(endpoint);
    }

    // sas token
    if let Some(sas_token) = azure_blob_config.sas_token {
        azure_builder = azure_builder.with_config(AzureConfigKey::SasKey, sas_token);
    }

    // account name
    if let Some(account_name) = azure_blob_config.account_name {
        azure_builder = azure_builder.with_account(account_name);
    }

    // account key
    if let Some(account_key) = azure_blob_config.account_key {
        azure_builder = azure_builder.with_access_key(account_key);
    }

    // tenant id
    if let Some(tenant_id) = azure_blob_config.tenant_id {
        azure_builder = azure_builder.with_tenant_id(tenant_id);
    }

    // client id
    if let Some(client_id) = azure_blob_config.client_id {
        azure_builder = azure_builder.with_client_id(client_id);
    }

    // client secret
    if let Some(client_secret) = azure_blob_config.client_secret {
        azure_builder = azure_builder.with_client_secret(client_secret);
    }

    let object_store = azure_builder.build().unwrap_or_else(|e| panic!("{}", e));

    // object store handles refreshing bearer token, so we do not need to handle expiry here
    let expire_at = None;

    ObjectStoreWithExpiration {
        object_store: Arc::new(object_store),
        expire_at,
    }
}

pub(crate) fn parse_azure_blob_container(uri: &Url) -> Option<String> {
    let host = uri.host_str()?;

    // az(ure)://{container}/key
    if uri.scheme() == "az" || uri.scheme() == "azure" {
        return Some(host.to_string());
    }
    // https://{account}.blob.core.windows.net/{container}
    else if host.ends_with(".blob.core.windows.net") {
        let path_segments: Vec<&str> = uri.path_segments()?.collect();

        // Container name is the first part of the path
        return Some(
            path_segments
                .first()
                .expect("unexpected error during parsing azure blob uri")
                .to_string(),
        );
    }

    None
}

// AzureStorageConfig is a struct that holds the configuration that is
// used to configure the Azure Blob Storage object store. object_store does
// not provide a way to read the config files, so we need to read
// them ourselves via rust-ini and azure sdk.
struct AzureStorageConfig {
    account_name: Option<String>,
    account_key: Option<String>,
    sas_token: Option<String>,
    tenant_id: Option<String>,
    client_id: Option<String>,
    client_secret: Option<String>,
    endpoint: Option<String>,
    allow_http: bool,
}

impl AzureStorageConfig {
    // load reads the azure config from the environment variables first and config files as fallback.
    // There is no proper azure sdk config crate that can read the config files.
    // So, we need to read the config files manually from azure's ini config.
    // See https://learn.microsoft.com/en-us/cli/azure/azure-cli-configuration?view=azure-cli-latest
    fn load() -> Self {
        // ~/.azure/config
        let azure_config_file_path = std::env::var("AZURE_CONFIG_FILE").unwrap_or(
            home_dir()
                .expect("failed to get home directory")
                .join(".azure")
                .join("config")
                .to_str()
                .expect("failed to convert path to string")
                .to_string(),
        );

        let azure_config_content = Ini::load_from_file(&azure_config_file_path).ok();

        let connection_string = match std::env::var("AZURE_STORAGE_CONNECTION_STRING") {
            Ok(connection_string) => Some(connection_string),
            Err(_) => azure_config_content
                .as_ref()
                .and_then(|ini| ini.section(Some("storage")))
                .and_then(|section| section.get("connection_string"))
                .map(|connection_string| connection_string.to_string()),
        };

        // connection string overrides everything
        if let Some(connection_string) = connection_string {
            if let Ok(connection_string) = ConnectionString::new(&connection_string) {
                return connection_string.into();
            }
        }

        let account_name = match std::env::var("AZURE_STORAGE_ACCOUNT") {
            Ok(account) => Some(account),
            Err(_) => azure_config_content
                .as_ref()
                .and_then(|ini| ini.section(Some("storage")))
                .and_then(|section| section.get("account"))
                .map(|account| account.to_string()),
        };

        let account_key = match std::env::var("AZURE_STORAGE_KEY") {
            Ok(key) => Some(key),
            Err(_) => azure_config_content
                .as_ref()
                .and_then(|ini| ini.section(Some("storage")))
                .and_then(|section| section.get("key"))
                .map(|key| key.to_string()),
        };

        let sas_token = match std::env::var("AZURE_STORAGE_SAS_TOKEN") {
            Ok(token) => Some(token),
            Err(_) => azure_config_content
                .as_ref()
                .and_then(|ini| ini.section(Some("storage")))
                .and_then(|section| section.get("sas_token"))
                .map(|token| token.to_string()),
        };

        // endpoint, object_store specific
        let endpoint = std::env::var("AZURE_STORAGE_ENDPOINT").ok();

        // allow http, object_store specific
        let allow_http = std::env::var("AZURE_ALLOW_HTTP")
            .ok()
            .map(|allow_http| allow_http.parse().unwrap_or(false))
            .unwrap_or(false);

        // tenant id, object_store specific
        let tenant_id = std::env::var("AZURE_TENANT_ID").ok();

        // client id, object_store specific
        let client_id = std::env::var("AZURE_CLIENT_ID").ok();

        // client secret, object_store specific
        let client_secret = std::env::var("AZURE_CLIENT_SECRET").ok();

        AzureStorageConfig {
            account_name,
            account_key,
            sas_token,
            tenant_id,
            client_id,
            client_secret,
            endpoint,
            allow_http,
        }
    }
}

impl From<ConnectionString<'_>> for AzureStorageConfig {
    fn from(connection_string: ConnectionString) -> Self {
        let account_name = connection_string
            .account_name
            .map(|account_name| account_name.to_string());

        let account_key = connection_string
            .account_key
            .map(|account_key| account_key.to_string());

        let sas_token = connection_string.sas.map(|sas| sas.to_string());

        let endpoint = connection_string
            .blob_endpoint
            .map(|blob_endpoint| blob_endpoint.to_string());

        let allow_http = matches!(
            connection_string.default_endpoints_protocol,
            Some(EndpointProtocol::Http)
        );

        AzureStorageConfig {
            account_name,
            account_key,
            sas_token,
            tenant_id: None,
            client_id: None,
            client_secret: None,
            endpoint,
            allow_http,
        }
    }
}
