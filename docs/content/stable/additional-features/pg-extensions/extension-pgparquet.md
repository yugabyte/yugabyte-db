---
title: pg_parquet extension
headerTitle: pg_parquet extension
linkTitle: pg_parquet
description: Using the pg_parquet extension in YugabyteDB
tags:
  feature: tech-preview
menu:
  stable:
    identifier: extension-pgparquet
    parent: pg-extensions
    weight: 20
type: docs
---

{{< warning title="Extension support" >}}
Support for pg_parquet extension is in {{<tags/feature/tp>}} only. It may get replaced by another extension in future. So, it is not recommended for use in production.
{{< /warning >}}

The [pg_parquet](https://www.crunchydata.com/blog/pg_parquet-an-extension-to-connect-postgres-and-parquet) extension allows you to read from and write to [Parquet](https://parquet.apache.org/) files by extending the PostgreSQL COPY command. Files can be located in S3, Azure Blob Storage, Google Cloud Storage, http(s) endpoints, or on a file system.

## Enable pg_parquet

To enable the pg_parquet extension:

1. Add `pg_parquet` to `shared_preload_libraries` in the PostgreSQL server configuration parameters using the YB-TServer [--ysql_pg_conf_csv](../../../reference/configuration/yb-tserver/#ysql-pg-conf-csv) flag:

    ```sh
    --ysql_pg_conf_csv="shared_preload_libraries='pg_parquet'"
    ```

    Note that modifying `shared_preload_libraries` requires restarting the YB-TServer.

1. Enable the extension:

    ```sql
    CREATE EXTENSION pg_parquet;
    ```

## Use pg_parquet

You can use pg_parquet to do the following:

- Export tables or queries to Parquet files.
- Ingest data from Parquet files to tables.
- Inspect the schema and metadata of Parquet files.

### Copy to and from Parquet files

To read and write Parquet files, you use the COPY command. The following example shows how to write a YSQL table, with complex types, into a Parquet file, and then read the Parquet file content back to the same table.

1. Create composite types that will be used in the table:

    ```sql
    CREATE TYPE product_item AS (id INT, name TEXT, price float4);
    CREATE TYPE product AS (id INT, name TEXT, items product_item[]);

1. Create a table that uses the composite types, including arrays and timestamp columns:

    ```sql
    CREATE TABLE product_example (
        id int,
        product product,
        products product[],
        created_at TIMESTAMP,
        updated_at TIMESTAMPTZ
    );
    ```

1. Insert some rows into the table:

    ```sql
    INSERT INTO product_example VALUES (
        1,
        ROW(1, 'product 1', ARRAY[ROW(1, 'item 1', 1.0), ROW(2, 'item 2', 2.0), NULL]::product_item[])::product,
        ARRAY[ROW(1, NULL, NULL)::product, NULL],
        now(),
        '2022-05-01 12:00:00-04'
    );
    ```

1. Export the table data to a Parquet file using COPY TO with gzip compression:

    ```sql
    COPY product_example TO '/tmp/product_example.parquet' (format 'parquet', compression 'gzip');
    -- show table
    SELECT * FROM product_example;
    ```

1. Import the Parquet file data back to the table using COPY FROM to verify the round-trip export and import:

    ```sql
    COPY product_example FROM '/tmp/product_example.parquet';
    -- show table
    SELECT * FROM product_example;
    ```

### Inspect Parquet schema

The following SELECT query outputs the schema of the Parquet file at a given URI:

```sql
SELECT * FROM parquet.schema('/tmp/product_example.parquet') LIMIT 10;
```

```output
             uri              |     name     | type_name  | type_length | repetition_type | num_children | converted_type | scale | precision | field_id | logical_type
------------------------------+--------------+------------+-------------+-----------------+--------------+----------------+-------+-----------+----------+--------------
 /tmp/product_example.parquet | arrow_schema |            |             |                 |            5 |                |       |           |          |
 /tmp/product_example.parquet | id           | INT32      |             | OPTIONAL        |              |                |       |           |        0 |
 /tmp/product_example.parquet | product      |            |             | OPTIONAL        |            3 |                |       |           |        1 |
 /tmp/product_example.parquet | id           | INT32      |             | OPTIONAL        |              |                |       |           |        2 |
 /tmp/product_example.parquet | name         | BYTE_ARRAY |             | OPTIONAL        |              | UTF8           |       |           |        3 | STRING
 /tmp/product_example.parquet | items        |            |             | OPTIONAL        |            1 | LIST           |       |           |        4 |
 /tmp/product_example.parquet | list         |            |             | REPEATED        |            1 |                |       |           |          |
 /tmp/product_example.parquet | element      |            |             | OPTIONAL        |            3 |                |       |           |        5 |
 /tmp/product_example.parquet | id           | INT32      |             | OPTIONAL        |              |                |       |           |        6 |
 /tmp/product_example.parquet | name         | BYTE_ARRAY |             | OPTIONAL        |              | UTF8           |       |           |        7 | STRING
(10 rows)
```

### Inspect Parquet metadata

Use the following SELECT query to discover the detailed metadata of the Parquet file, such as column statistics, at a given URI:

```sql
SELECT uri, row_group_id, row_group_num_rows, row_group_num_columns, row_group_bytes, column_id, file_offset, num_values, path_in_schema, type_name FROM parquet.metadata('/tmp/product_example.parquet') LIMIT 1;
```

```output
             uri              | row_group_id | row_group_num_rows | row_group_num_columns | row_group_bytes | column_id | file_offset | num_values | path_in_schema | type_name
------------------------------+--------------+--------------------+-----------------------+-----------------+-----------+-------------+------------+----------------+-----------
 /tmp/product_example.parquet |            0 |                  1 |                    13 |             842 |         0 |           0 |          1 | id             | INT32
(1 row)
```

```sql
SELECT stats_null_count, stats_distinct_count, stats_min, stats_max, compression, encodings, index_page_offset, dictionary_page_offset, data_page_offset, total_compressed_size, total_uncompressed_size FROM parquet.metadata('/tmp/product_example.parquet') LIMIT 1;
```

```output
 stats_null_count | stats_distinct_count | stats_min | stats_max |    compression     |        encodings         | index_page_offset | dictionary_page_offset | data_page_offset | total_compressed_size | total_uncompressed_size
------------------+----------------------+-----------+-----------+--------------------+--------------------------+-------------------+------------------------+------------------+-----------------------+-------------------------
                0 |                      | 1         | 1         | GZIP(GzipLevel(6)) | PLAIN,RLE,RLE_DICTIONARY |                   |                      4 |               42 |                   101 |                      61
(1 row)
```

Use the following SELECT query to discover file level metadata of the Parquet file, such as format version, at a given URI:

```sql
SELECT * FROM parquet.file_metadata('/tmp/product_example.parquet')
```

```output
             uri              | created_by | num_rows | num_row_groups | format_version
------------------------------+------------+----------+----------------+----------------
 /tmp/product_example.parquet | pg_parquet |        1 |              1 | 1
(1 row)
```

Use the following SELECT query to get custom key-value metadata of the Parquet file at a given URI:

```sql
SELECT uri, encode(key, 'escape') as key, encode(value, 'escape') as value FROM parquet.kv_metadata('/tmp/product_example.parquet');
```

```output
             uri              |     key      |    value
------------------------------+--------------+---------------------
 /tmp/product_example.parquet | ARROW:schema | /////5gIAAAQAAAA ...
(1 row)
```

### Inspect Parquet column statistics

Use the following SELECT query to discover the column statistics of the Parquet file, such as min and max value for the column, at a given URI:

```sql
SELECT * FROM parquet.column_stats('/tmp/product_example.parquet');
```

```output
 column_id | field_id |         stats_min          |         stats_max          | stats_null_count | stats_distinct_count
-----------+----------+----------------------------+----------------------------+------------------+----------------------
         4 |        7 | item 1                     | item 2                     |                1 |
         6 |       11 | 1                          | 1                          |                1 |
         7 |       12 |                            |                            |                2 |
        10 |       17 |                            |                            |                2 |
         0 |        0 | 1                          | 1                          |                0 |
        11 |       18 | 2025-03-11 14:01:22.045739 | 2025-03-11 14:01:22.045739 |                0 |
         3 |        6 | 1                          | 2                          |                1 |
        12 |       19 | 2022-05-01 19:00:00+03     | 2022-05-01 19:00:00+03     |                0 |
         8 |       15 |                            |                            |                2 |
         5 |        8 | 1                          | 2                          |                1 |
         9 |       16 |                            |                            |                2 |
         1 |        2 | 1                          | 1                          |                0 |
         2 |        3 | product 1                  | product 1                  |                0 |
(13 rows)
```

## Object Store support

pg_parquet supports reading and writing Parquet files in S3, Azure Blob Storage, http(s), and Google Cloud Storage object stores.


To write to an object store location, you need to grant the `parquet_object_store_write` role to your current user. Similarly, to read from an object store location, you need to grant the `parquet_object_store_read` role to your current user.
For information on how to grant roles, refer to [GRANT](../../../api/ysql/the-sql-language/statements/dcl_grant/).


### S3 Storage

S3 credentials are configured through the `parquet` foreign data wrapper. Non-sensitive options (such as `region`, `endpoint`, `scope`) live on a `CREATE SERVER ... TYPE 's3' FOREIGN DATA WRAPPER parquet`. Sensitive options (`key_id`, `secret`, `session_token`) live on a per-user `CREATE USER MAPPING`. Environment variables and `~/.aws` files are not used by pg_parquet.

#### Quick start with create_simple_secret

The easiest way to register credentials is `parquet.create_simple_secret`, which atomically creates the SERVER and USER MAPPING for the current user and returns the generated server name:

```sql
SELECT parquet.create_simple_secret(
    type    := 's3',
    key_id  := 'AKIAIOSFODNN7EXAMPLE',
    secret  := 'wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY',
    region  := 'eu-central-1'
);
```

#### Manual SERVER + USER MAPPING

For advanced cases (custom server name, sharing across users, scoping by bucket), create the objects yourself:

```sql
CREATE SERVER pgp_s3 TYPE 's3' FOREIGN DATA WRAPPER parquet
  OPTIONS (region 'eu-central-1', scope 'my-bucket');

CREATE USER MAPPING FOR CURRENT_USER SERVER pgp_s3
  OPTIONS (key_id 'AKIAIOSFODNN7EXAMPLE', secret 'wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY');

GRANT USAGE ON FOREIGN SERVER pgp_s3 TO CURRENT_USER;
```

Supported SERVER options for `TYPE 's3'`: `region`, `endpoint`, `scope` (bucket or `bucket/prefix`), `url_style`, `use_ssl`, `allow_http`.

Supported USER MAPPING options for `TYPE 's3'`: `key_id`, `secret`, `session_token`.

The validator rejects any sensitive option (`key_id`, `secret`, `session_token`, `service_account_key`, `service_account_path`) placed on a SERVER.

The supported S3 URI formats are:

- `s3://<bucket>/<path>`
- `https://<bucket>.s3.amazonaws.com/<path>`
- `https://s3.amazonaws.com/<bucket>/<path>`

### Azure Blob Storage

The basic way to configure object storage is by creating the standard `~/.azure/config` file:

```sh
$ cat ~/.azure/config
[storage]
account = devstoreaccount1
key = Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==
```

Alternatively, you can use environment variables when starting postgres to configure the Azure Blob Storage client as described in the following table:

| Variable | Description |
| :--- | :--- |
| `AZURE_STORAGE_ACCOUNT` | Storage account name of the Azure Blob. |
| `AZURE_STORAGE_KEY` | Storage key of the Azure Blob. |
| `AZURE_STORAGE_CONNECTION_STRING` | Connection string for the Azure Blob (overrides any other configuration). |
| `AZURE_STORAGE_SAS_TOKEN` | Storage SAS token for the Azure Blob. |
| `AZURE_TENANT_ID` | Tenant ID for client secret authentication (only via environment variables). |
| `AZURE_CLIENT_ID` | Client ID for client secret authentication (only via environment variables). |
| `AZURE_CLIENT_SECRET` | Client secret for client secret authentication (only via environment variables). |
| `AZURE_STORAGE_ENDPOINT` | The endpoint (only via environment variables). |
| `AZURE_CONFIG_FILE` | An alternative location for the configuration file (only via environment variables). |
| `AZURE_ALLOW_HTTP` | Allows HTTP endpoints (only via environment variables). |

The following table summarizes key Azure Blob client configuration and authorization methods' priorities.

| Configuration source priority order | Supported authorization methods' priority order |
| :--- | :--- | :--- |
| <ol><li>Connection string (read from environment variable<br/> or configuration file)</li><li>Environment variables</li><li>Configuration file</li></ol> | <ol><li>Bearer token via client secret</li><li>Sas token</li><li>Storage key</li></ol> |

The supported Azure Blob Storage URI formats are as follows:

- `az://<container>/<path>`
- `azure://<container>/<path>`
- `https://<account>.blob.core.windows.net/<container>`


### HTTP(s) Storage

HTTPS URIs are supported by default. You can set `ALLOW_HTTP` environment variables to allow HTTP URIs.

### Google Cloud Storage

GCS credentials are configured through the same `parquet` foreign data wrapper. SERVER carries non-sensitive options (`endpoint`, `scope`); USER MAPPING carries either HMAC credentials (`key_id` + `secret`) or service-account credentials (`service_account_key` JSON blob, or `service_account_path` file path). Environment variables and `~/.config/gcloud` files are not used by pg_parquet.

#### Quick start with create_simple_secret

```sql
-- HMAC credentials
SELECT parquet.create_simple_secret(
    type   := 'gcs',
    key_id := 'GOOG1EXAMPLEHMACKEY',
    secret := 'EXAMPLEHMACSECRET'
);

-- Service-account JSON
SELECT parquet.create_simple_secret(
    type                := 'gcs',
    service_account_key := '{"type":"service_account", ...}'
);

-- Service-account file
SELECT parquet.create_simple_secret(
    type                 := 'gcs',
    service_account_path := '/etc/yb/gcs-sa.json'
);
```

#### Manual SERVER + USER MAPPING

```sql
CREATE SERVER pgp_gcs TYPE 'gcs' FOREIGN DATA WRAPPER parquet
  OPTIONS (scope 'my-gcs-bucket');

-- HMAC
CREATE USER MAPPING FOR CURRENT_USER SERVER pgp_gcs
  OPTIONS (key_id 'GOOG1EXAMPLEHMACKEY', secret 'EXAMPLEHMACSECRET');

-- OR service-account JSON / path (do not combine with HMAC)
CREATE USER MAPPING FOR CURRENT_USER SERVER pgp_gcs
  OPTIONS (service_account_path '/etc/yb/gcs-sa.json');

GRANT USAGE ON FOREIGN SERVER pgp_gcs TO CURRENT_USER;
```

Supported SERVER options for `TYPE 'gcs'`: `endpoint`, `scope`.

Supported USER MAPPING options for `TYPE 'gcs'`: `key_id`, `secret`, `session_token`, `service_account_key`, `service_account_path`. HMAC and service-account options cannot be combined on the same mapping; `service_account_key` and `service_account_path` are mutually exclusive.

Supported Google Cloud Storage URI format is `gs://<bucket>/<path>`.

## Copy options

The COPY TO command options pg_parquet supports is described in the following table:

| Option | Description |
| :--- | :--- |
| `format parquet` | Specify this option to read or write Parquet files that do not end with the `.parquet[.<compression>]` extension. |
| `file_size_bytes <string>` | Total file size per Parquet file. When set, Parquet files with the target size are created under a parent directory (named the same as the file name). By default, when not specified, a single file is generated without a parent folder. You can specify total bytes without a unit (for example, `file_size_bytes 2000000`), or with a unit (KB, MB, or GB, for example, `file_size_bytes '1MB'`). |
| `field_ids <string>` | Field IDs that are assigned to the fields in the Parquet file schema. By default, no field IDs are assigned. Pass `auto` to let pg_parquet generate field IDs. You can pass a JSON string to explicitly provide the field IDs. |
| `row_group_size <int64>` | Number of rows in each row group while writing Parquet files. Default is `122880`. |
| `row_group_size_bytes <int64>` | Total byte size of rows in each row group while writing Parquet files. Default is `row_group_size * 1024`. |
| `compression <string>` | The compression format to use while writing Parquet files. Supported formats are `uncompressed`, `snappy` (default), `gzip`, `brotli`, `lz4`, `lz4raw`, and `zstd`. If not specified, the format is determined by the file extension. |
| `compression_level <int>` | The compression level to use while writing Parquet files. This is only supported for `gzip`, `zstd`, and `brotli`. The default is `6` for gzip (0-10), `1` for zstd (1-22), and `1` for brotli (0-11). |
| `parquet_version <string>` | The writer version of the Parquet file. By default, it is set to `v1` for better interoperability. You can set it to `v2` to unlock new encodings. |

The COPY FROM command options pg_parquet supports is described in the following table:

| Option | Description |
| :--- | :--- |
| `format parquet`| Specify this option to read or write Parquet files which does not end with `.parquet[.<compression>]` extension|
|`match_by <string>`| Method to match Parquet file fields to PostgreSQL table columns. Available methods are `position` (default) and `name`. You can set it to `name` to match the columns by their name rather than by their position in the schema (default). Match by name is useful when field order differs between the Parquet file and the table, but their names match. |