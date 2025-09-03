---
title: pg_parquet extension
headerTitle: pg_parquet extension
linkTitle: pg_parquet
description: Using the pg_parquet extension in YugabyteDB
menu:
  preview:
    identifier: extension-pg-parquet
    parent: pg-extensions
    weight: 20
type: docs
---

The [pg_parquet](https://www.crunchydata.com/blog/pg_parquet-an-extension-to-connect-postgres-and-parquet) extension allows you to read and write [Parquet](https://parquet.apache.org/) files, which are located in S3, Azure Blob Storage, Google Cloud Storage, http(s) endpoints or file system, from PostgreSQL via COPY TO/FROM commands. It depends on [Apache Arrow](https://arrow.apache.org/rust/arrow/) project to read and write Parquet files and [pgrx](https://github.com/pgcentralfoundation/pgrx) project to extend PostgreSQL's COPY command.

## Enable pg_parquet

To enable the pg_parquet extension, add `pg_parquet` to `shared_preload_libraries` in the PostgreSQL server configuration parameters using the YB-TServer [--ysql_pg_conf_csv](../../../../reference/configuration/yb-tserver/#ysql-pg-conf-csv) flag:

```sh
--ysql_pg_conf_csv="shared_preload_libraries='pg_parquet'"
```

Note that modifying `shared_preload_libraries` requires restarting the YB-TServer.

## Create the pg_parquet extension

To enable the extension:

```sql
CREATE EXTENSION pg_parquet;
```

## Use pg_parquet

You can use pg_parquet to do the following:

- Export tables or queries to Parquet files.
- Ingest data from Parquet files to tables.
- Inspect the schema and metadata of Parquet files.

### COPY to/from Parquet files from/to YSQL tables

You can use COPY command to read and write from/to Parquet files. An example of how to write a YSQL table, with complex types, into a Parquet file and then to read the Parquet file content back to the same table is as follows:

```sql
-- create composite types
CREATE TYPE product_item AS (id INT, name TEXT, price float4);
CREATE TYPE product AS (id INT, name TEXT, items product_item[]);

-- create a table with complex types
CREATE TABLE product_example (
    id int,
    product product,
    products product[],
    created_at TIMESTAMP,
    updated_at TIMESTAMPTZ
);

-- insert some rows into the table
insert into product_example values (
    1,
    ROW(1, 'product 1', ARRAY[ROW(1, 'item 1', 1.0), ROW(2, 'item 2', 2.0), NULL]::product_item[])::product,
    ARRAY[ROW(1, NULL, NULL)::product, NULL],
    now(),
    '2022-05-01 12:00:00-04'
);

-- copy the table to a parquet file
COPY product_example TO '/tmp/product_example.parquet' (format 'parquet', compression 'gzip');

-- show table
SELECT * FROM product_example;

-- copy the parquet file to the table
COPY product_example FROM '/tmp/product_example.parquet';

-- show table
SELECT * FROM product_example;
```

### Inspect Parquet schema

Use the `SELECT * FROM parquet.schema(<uri>)` query to discover the schema of the Parquet file at a given URI as follows:

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

Use the `SELECT * FROM parquet.metadata(<uri>)` query to discover the detailed metadata of the Parquet file, such as column statistics, at a given URI as follows:

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

Use the `SELECT * FROM parquet.file_metadata(<uri>)` query to discover file level metadata of the Parquet file, such as format version, at a given URI.

```sql
SELECT * FROM parquet.file_metadata('/tmp/product_example.parquet')
```

```output
             uri              | created_by | num_rows | num_row_groups | format_version
------------------------------+------------+----------+----------------+----------------
 /tmp/product_example.parquet | pg_parquet |        1 |              1 | 1
(1 row)
```

You can call `SELECT * FROM parquet.kv_metadata(<uri>)` to query custom key-value metadata of the Parquet file at given uri.

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

You can call `SELECT * FROM parquet.column_stats(<uri>)` to discover the column statistics of the Parquet file, such as min and max value for the column, at given uri.

```sql
SELECT * FROM parquet.column_stats('/tmp/product_example.parquet')
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

## Object Store Support

pg_parquet supports reading and writing Parquet files from/to S3, Azure Blob Storage, http(s) and Google Cloud Storage object stores.

{{< note title="Required roles" >}}

To be able to write into an object store location, you need to grant `parquet_object_store_write` role to your current postgres user. Similarly, to read from an object store location, you need to grant `parquet_object_store_read` role to your current postgres user.

{{< /note >}}

### S3 Storage

The simplest way to configure object storage is by creating the standard `~/.aws/credentials` and `~/.aws/config` files:

```sh
$ cat ~/.aws/credentials
[default]
aws_access_key_id = AKIAIOSFODNN7EXAMPLE
aws_secret_access_key = wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY

$ cat ~/.aws/config
[default]
region = eu-central-1
```

Alternatively, you can use the following environment variables when starting postgres to configure the S3 client:

- `AWS_ACCESS_KEY_ID`: the access key ID of the AWS account
- `AWS_SECRET_ACCESS_KEY`: the secret access key of the AWS account
- `AWS_SESSION_TOKEN`: the session token for the AWS account
- `AWS_REGION`: the default region of the AWS account
- `AWS_ENDPOINT_URL`: the endpoint
- `AWS_SHARED_CREDENTIALS_FILE`: an alternative location for the credentials file (only via environment variables)
- `AWS_CONFIG_FILE`: an alternative location for the config file (only via environment variables)
- `AWS_PROFILE`: the name of the profile from the credentials and config file (default profile name is default) (only via environment variables)
- `AWS_ALLOW_HTTP`: allows http endpoints (only via environment variables)

Config source priority order is shown below:

1. Environment variables,
2. Config file.

Supported S3 uri formats are shown below:

- `s3://<bucket>/<path>`
- `https://<bucket>.s3.amazonaws.com/<path>`
- `https://s3.amazonaws.com/<bucket>/<path>`

Supported authorization methods' priority order is shown below:

1. Temporary session tokens by assuming roles,
2. Long term credentials.

### Azure Blob Storage

The simplest way to configure object storage is by creating the standard `~/.azure/config` file:

```sh
$ cat ~/.azure/config
[storage]
account = devstoreaccount1
key = Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==
```

Alternatively, you can use the following environment variables when starting postgres to configure the Azure Blob Storage client:

- `AZURE_STORAGE_ACCOUNT`: the storage account name of the Azure Blob
- `AZURE_STORAGE_KEY`: the storage key of the Azure Blob
- `AZURE_STORAGE_CONNECTION_STRING`: the connection string for the Azure Blob (overrides any other config)
- `AZURE_STORAGE_SAS_TOKEN`: the storage SAS token for the Azure Blob
- `AZURE_TENANT_ID`: the tenant id for client secret auth (only via environment variables)
- `AZURE_CLIENT_ID`: the client id for client secret auth (only via environment variables)
- `AZURE_CLIENT_SECRET`: the client secret for client secret auth (only via environment variables)
- `AZURE_STORAGE_ENDPOINT`: the endpoint (only via environment variables)
- `AZURE_CONFIG_FILE`: an alternative location for the config file (only via environment variables)
- `AZURE_ALLOW_HTTP`: allows http endpoints (only via environment variables)

Config source priority order is shown below:

1. Connection string (read from environment variable or config file),
2. Environment variables,
3. Config file.

Supported Azure Blob Storage uri formats are shown below:

- `az://<container>/<path>`
- `azure://<container>/<path>`
- `https://<account>.blob.core.windows.net/<container>`

Supported authorization methods' priority order is shown below:

1. Bearer token via client secret,
2. Sas token,
3. Storage key.

### Http(s) Storage

Https uris are supported by default. You can set `ALLOW_HTTP` environment variables to allow http uris.

### Google Cloud Storage

The simplest way to configure object storage is by creating a json config file like `~/.config/gcloud/application_default_credentials.json` (can be generated by `gcloud auth application-default login`):

```sh
$ cat ~/.config/gcloud/application_default_credentials.json
{
  "gcs_base_url": "http://localhost:4443",
  "disable_oauth": true,
  "client_email": "",
  "private_key_id": "",
  "private_key": ""
}
```

Alternatively, you can use the following environment variables when starting postgres to configure the Google Cloud Storage client:

- `GOOGLE_SERVICE_ACCOUNT_KEY`: json serialized service account key (only via environment variables)
- `GOOGLE_SERVICE_ACCOUNT_PATH`: an alternative location for the config file (only via environment variables)

Supported Google Cloud Storage uri formats are shown below:

- `gs://<bucket>/<path>`

## Copy Options

pg_parquet supports the following options in the COPY TO command:

- `format parquet`: you need to specify this option to read or write Parquet files which does not end with `.parquet[.<compression>]` extension,
- `file_size_bytes <string>`: the total file size per Parquet file. When set, the parquet files, with target size, are created under parent directory (named the same as file name). By default, when not specified, a single file is generated without creating a parent folder. You can specify total bytes without unit like `file_size_bytes 2000000` or with unit (KB, MB, or GB) like `file_size_bytes '1MB'`,
- `field_ids <string>`: fields ids that are assigned to the fields in Parquet file schema. By default, no field ids are assigned. Pass `auto` to let pg_parquet generate field ids. You can pass a json string to explicitly pass the field ids,
- `row_group_size <int64>`: the number of rows in each row group while writing Parquet files. The default row group size is 122880,
- `row_group_size_bytes <int64>`: the total byte size of rows in each row group while writing Parquet files. The default row group size bytes is `row_group_size * 1024`,
- `compression <string>`: the compression format to use while writing Parquet files. The supported compression formats are `uncompressed`, `snappy`, `gzip`, `brotli`, `lz4`, `lz4raw` and `zstd`. The default compression format is `snappy`. If not specified, the compression format is determined by the file extension,
- `compression_level <int>`: the compression level to use while writing Parquet files. The supported compression levels are only supported for `gzip`, `zstd` and `brotli` compression formats. The default compression level is 6 for gzip (0-10), 1 for zstd (1-22) and 1 for brotli (0-11),
- `parquet_version <string>`: writer version of the Parquet file. By default, it is set to `v1` to be more interoperable with common query engines. (some are not able to read v2 files) You can set it to `v2` to unlock some of the new encodings.

pg_parquet supports the following options in the COPY FROM command:

- `format parquet`: you need to specify this option to read or write Parquet files which does not end with `.parquet[.<compression>]` extension,
- `match_by <string>`: method to match Parquet file fields to PostgreSQL table columns. The available methods are `position` and `name`. The default method is `position`. You can set it to `name` to match the columns by their name rather than by their position in the schema (default). Match by name is useful when field order differs between the Parquet file and the table, but their names match.
