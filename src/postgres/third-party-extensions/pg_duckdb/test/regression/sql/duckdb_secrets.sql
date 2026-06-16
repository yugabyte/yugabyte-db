-- Invalid type
CREATE SERVER invalid_duckdb_server
TYPE 'unknown'
FOREIGN DATA WRAPPER duckdb;

-- Invalid use of restricted option (with various casing)
CREATE SERVER invalid_duckdb_server
TYPE 'http'
FOREIGN DATA WRAPPER duckdb
OPTIONS (hTtP_pRoXy_PaSsWoRd 'very secret');

CREATE SERVER invalid_duckdb_server
TYPE 's3'
FOREIGN DATA WRAPPER duckdb
OPTIONS (SECRET 'dont leak me', session_TOKEN 'shhhhh');

CREATE SERVER invalid_duckdb_server
TYPE 'azure'
FOREIGN DATA WRAPPER duckdb
OPTIONS (CONNECTION_STRING 'all my life secrets here');

-- No secret was created
SELECT * FROM duckdb.query($$ SELECT count(*) FROM duckdb_secrets(); $$);

-- Valid S3
CREATE SERVER valid_s3_server
TYPE 's3'
FOREIGN DATA WRAPPER duckdb;

-- Secret was created
SELECT * FROM duckdb.query($$ FROM which_secret('s3://some-bucket/file.parquet', 's3'); $$);

-- Valid secrets for other types (don't load Azure or other extensions)
CREATE SERVER valid_r2_server TYPE 'r2' FOREIGN DATA WRAPPER duckdb;
CREATE SERVER valid_hf_server TYPE 'huggingface' FOREIGN DATA WRAPPER duckdb;
CREATE SERVER valid_gcs_server TYPE 'gcs' FOREIGN DATA WRAPPER duckdb;

-- Check them all
SELECT * FROM duckdb.query($$ SELECT name, type FROM duckdb_secrets(); $$);

-- Add one more (test drop & recreate)
CREATE SERVER valid_http_server TYPE 'http' FOREIGN DATA WRAPPER duckdb;

-- And verify we have them all
SELECT * FROM duckdb.query($$ SELECT name, type FROM duckdb_secrets(); $$);

-- PROVIDER option needs the `aws` extension
SELECT duckdb.install_extension('aws');

CREATE SERVER valid_s3_cred_chain
TYPE 's3'
FOREIGN DATA WRAPPER duckdb
OPTIONS (PROVIDER 'credential_chain', VALIDATION 'none'); -- use empty chain otherwise it takes too much time

-- Drop some
DROP SERVER valid_r2_server;
DROP SERVER valid_hf_server;
DROP SERVER valid_gcs_server;
DROP SERVER valid_http_server;

-- Make sure we have the 'credential_chain'
SELECT * FROM duckdb.query($$
    SELECT
        name,
        map_from_entries(
            list_transform( -- split 'key=value' strings to have an array of [key, value]
            list_transform( -- split the secret string by `;` to have 'key=value' strings
                regexp_split_to_array(secret_string, ';'),
                x -> regexp_split_to_array(x, '=')
            ),
            x -> struct_pack(k := x[1], v := x[2])
            )
        ).provider as provider
    FROM duckdb_secrets();
$$);

DROP SERVER valid_s3_server;
DROP SERVER valid_s3_cred_chain;

-- Nothing
SELECT * FROM duckdb.query($$ SELECT name, type FROM duckdb_secrets(); $$);

-- Now create secrets with USER MAPPING
CREATE SERVER valid_s3_server TYPE 's3' FOREIGN DATA WRAPPER duckdb;
CREATE USER MAPPING FOR CURRENT_USER
SERVER valid_s3_server
OPTIONS (KEY_ID 'my_secret_key', SECRET 'my_secret_value');

SELECT * FROM duckdb.query($$
    SELECT
        name,
        secrets.key_id,
        secrets.secret
    FROM (
        SELECT *,
        map_from_entries(
            list_transform( -- split 'key=value' strings to have an array of [key, value]
            list_transform( -- split the secret string by `;` to have 'key=value' strings
                regexp_split_to_array(secret_string, ';'),
                x -> regexp_split_to_array(x, '=')
            ),
            x -> struct_pack(k := x[1], v := x[2])
            )
        ) as secrets
        FROM duckdb_secrets()
    );
$$);

SET client_min_messages=WARNING; -- suppress NOTICE that include username
DROP SERVER valid_s3_server CASCADE;
RESET client_min_messages;

-- Nothing
SELECT * FROM duckdb.query($$ SELECT name, type FROM duckdb_secrets(); $$);

-- User helpers --

-- 1. Simple secrets

-- S3
SELECT duckdb.create_simple_secret('S3', 'my first key', 'my secret', 'my session token', 'my-region-42');
SELECT duckdb.create_simple_secret('S3', 'my other key', 'my secret', 'my session token'); -- Default region
SELECT duckdb.create_simple_secret('S3', 'my third key', 'my secret'); -- No session token, default region

-- With named arguments (simple_s3_secret_3)
SELECT duckdb.create_simple_secret(
    'S3',
    key_id := 'my named key',
    secret := 'my secret',
    session_token := 'foo',
    url_style := 'path',
    provider := 'credential_chain',
    endpoint := 'my-endpoint.com',
    scope := 's3://my-bucket',
    validation := 'none'
);

-- Alter SERVER (public options only)
ALTER SERVER simple_s3_secret_3 OPTIONS (SET endpoint 'my_other_endoint', SET url_style 'true');

-- Alter USER MAPPING
ALTER USER MAPPING FOR CURRENT_USER
SERVER simple_s3_secret_3 OPTIONS (SET secret 'a better secret');

-- R2
SELECT duckdb.create_simple_secret('R2', 'my r2 key1', 'my secret', 'my session token', 'my-region-42');
SELECT duckdb.create_simple_secret('R2', 'my r2 key2', 'my secret');

-- GCS
SELECT duckdb.create_simple_secret('GCS', 'my first key', 'my secret', 'my session token', 'my-region-42');
SELECT duckdb.create_simple_secret('GCS', 'my other key', 'my secret', 'my session token'); -- Default region
SELECT duckdb.create_simple_secret('GCS', 'my third key', 'my secret'); -- No session token, default region

-- Invalid
SELECT duckdb.create_simple_secret('BadType', '-', '-');

-- Test backwards compatibility with 1.0.0 SQL signature (9 parameters instead of 11)
CREATE FUNCTION create_simple_secret_v1_0_0(
    type          TEXT,
    key_id        TEXT,
    secret        TEXT,
    session_token TEXT DEFAULT '',
    region        TEXT DEFAULT '',
    url_style     TEXT DEFAULT '',
    provider      TEXT DEFAULT '',
    endpoint      TEXT DEFAULT '',
    scope         TEXT DEFAULT ''
)
RETURNS TEXT
LANGUAGE C AS 'pg_duckdb', 'pgduckdb_create_simple_secret';

SELECT create_simple_secret_v1_0_0('S3', 'compat-key', 'compat-secret', '', 'us-west-2');
DROP FUNCTION create_simple_secret_v1_0_0;

-- 2. Azure

SELECT duckdb.create_azure_secret('hello world', scope := 'az://myaccount.blob.core.windows.net/');

-- Now check everything.

SELECT fs.srvname, fs.srvtype, fs.srvoptions, um.umoptions
FROM pg_foreign_server fs
INNER JOIN pg_foreign_data_wrapper fdw ON fdw.oid = fs.srvfdw
LEFT JOIN pg_user_mapping um ON um.umserver = fs.oid
WHERE fdw.fdwname = 'duckdb' AND fs.srvtype != 'motherduck'
ORDER BY fs.srvname;

SELECT * FROM duckdb.query($$
    SELECT
        name,
        type,
        secrets.key_id,
        secrets.region,
        secrets.session_token,
        secrets.secret,
        secrets.connection_string
    FROM (
        SELECT *,
        map_from_entries(
            list_transform( -- split 'key=value' strings to have an array of [key, value]
            list_transform( -- split the secret string by `;` to have 'key=value' strings
                regexp_split_to_array(secret_string, ';'),
                x -> regexp_split_to_array(x, '=')
            ),
            x -> struct_pack(k := x[1], v := x[2])
            )
        ) as secrets
        FROM duckdb_secrets()
    );
$$);

set client_min_messages=WARNING; -- suppress NOTICE that include username
DROP SERVER
    simple_s3_secret,
    simple_s3_secret_1,
    simple_s3_secret_2,
    simple_s3_secret_3,
    simple_s3_secret_4,
    simple_r2_secret,
    simple_r2_secret_1,
    simple_gcs_secret,
    simple_gcs_secret_1,
    simple_gcs_secret_2,
    azure_secret
CASCADE;

-- Remove aws extension
DELETE FROM duckdb.extensions WHERE name = 'aws';
