# Secrets

DuckDB secrets can be configured either using utility functions or with a Foreign Data Wrapper for more advanced cases.

## Quick Start: Simple Secrets

The easiest way to configure credentials is using the utility functions:

### AWS S3 / Compatible Storage

```sql
-- Basic S3 secret (most common)
SELECT duckdb.create_simple_secret(
    type := 'S3',
    key_id := 'your_access_key_id',
    secret := 'your_secret_access_key',
    region := 'us-east-1'
);
```


There are many more arguments to this function:

```sql
SELECT duckdb.create_simple_secret(
    type          := 'S3',          -- Type: one of (S3, GCS, R2)
    key_id        := 'access_key_id',
    secret        := 'xxx',
    session_token := 'yyy',         -- (optional)
    region        := 'us-east-1',   -- (optional)
    url_style     := 'xxx',         -- (optional)
    provider      := 'xxx',         -- (optional)
    endpoint      := 'xxx',         -- (optional)
    scope         := 'xxx',         -- (optional)
    validation    := 'xxx',         -- (optional)
    use_ssl       := 'xxx'          -- (optional)
)
```

For Azure secrets you may use:
```sql
SELECT duckdb.create_azure_secret(
    '< connection string >',
    scope := 'xxx'          -- (optional)
);
```

## Secrets with `credential_chain` provider:

For more advanced use-cases, one can define secrets with a `SERVER` (and `USER MAPPING`) on `duckdb` Foreign Data Wrapper:

```sql
CREATE SERVER my_s3_secret
TYPE 's3'
FOREIGN DATA WRAPPER duckdb
OPTIONS (PROVIDER 'credential_chain');
```

## Secrets with `secret_access_key`:

When your secret contains sensitive information, you need to create an additional `USER MAPPING` like this:

```sql
CREATE SERVER my_s3_secret TYPE 's3' FOREIGN DATA WRAPPER duckdb;

CREATE USER MAPPING FOR CURRENT_USER SERVER my_s3_secret
OPTIONS (KEY_ID 'my_secret_key', SECRET 'my_secret_value');
```

You may use any of the supported DuckDB secret type as long as the related extension is installed.
Please refer to this page for more: https://duckdb.org/docs/stable/configuration/secrets_manager.html

## How it works

Secrets are stored in a combination of `SERVER` and `USER MAPPING` on `duckdb` Foreign Data Wrapper. The `USER MAPPING` hosts the sensitive elements like `token`, `session_token` and `secret`.
Each time a DuckDB instance is created by pg_duckdb, and when a secret is modified, the secrets are loaded into the DuckDB secrets manager as non-persistent secrets.

## Security Considerations

**Important:** Do not grant `USAGE` permission on the `duckdb` foreign data wrapper to regular users.

The owner of a foreign server can create user mappings for that server for **any user**, so only grant this to access to administrative users. Otherwise a regular user could create secrets for certain scopes for unsuspecting users.

## Further reading

* [DuckDB Secrets Manager](https://duckdb.org/docs/configuration/secrets_manager.html)
* [S3 API Support](https://duckdb.org/docs/extensions/httpfs/s3api.html)
* [Google Cloud Storage Import](https://duckdb.org/docs/guides/network_cloud_storage/gcs_import.html)
* [Cloudflare R2 Import](https://duckdb.org/docs/guides/network_cloud_storage/cloudflare_r2_import.html)
* [Azure Extension](https://duckdb.org/docs/extensions/azure)
