---
title: pg_duckdb extension
headerTitle: pg_duckdb extension
linkTitle: pg_duckdb
description: Using the pg_duckdb extension in YugabyteDB
menu:
  v2025.2:
    identifier: extension-pgduckdb
    parent: pg-extensions
    weight: 20
type: docs
---

The [pg_duckdb](https://github.com/duckdb/pg_duckdb) extension embeds the [DuckDB](https://duckdb.org/) analytics engine in PostgreSQL. In YugabyteDB, pg_duckdb runs in a fixed [lake_io](#limitations) mode that supports only data-lake I/O: reading and writing *lake files* (Parquet, CSV, and JSON files on object stores or the local file system). You use it to:

- Import lake files into YugabyteDB tables using `read_parquet()`, `read_csv()`, and `read_json()`.
- Export the result of a YugabyteDB query to a lake file using `COPY (SELECT ...) TO`.

The imported data lands in regular YugabyteDB tables and can be queried and joined normally. `lake_io` mode disables everything else DuckDB offers.

Available in v2025.2.6.0 and later.

## Enable pg_duckdb

pg_duckdb is gated behind the [ysql_yb_enable_pg_duckdb](../../../reference/configuration/yb-tserver/#ysql-yb-enable-pg-duckdb) preview flag and is not available on sanitizer (ASAN/TSAN) builds.

To enable the pg_duckdb extension:

1. Enable the `ysql_yb_enable_pg_duckdb` flag on YB-TServer by adding it to [--allowed_preview_flags_csv](../../../reference/configuration/yb-tserver/#allowed-preview-flags-csv) and setting it to true:

    ```sh
    --allowed_preview_flags_csv="ysql_yb_enable_pg_duckdb" \
      --ysql_yb_enable_pg_duckdb=true
    ```

1. Create the extension:

    ```sql
    CREATE EXTENSION pg_duckdb;
    ```

## Use pg_duckdb

You can use pg_duckdb to do the following:

- Import lake files into YugabyteDB tables.
- Export YugabyteDB query results to lake files.
- Read and write files on Amazon S3 and Google Cloud Storage.

### Import lake files

Use `read_parquet()`, `read_csv()`, or `read_json()` inside a `CREATE TABLE AS` (or `INSERT INTO ... SELECT`) query to load a lake file into a YugabyteDB table. Reference columns from the lake file with the `r['column']` syntax, where `r` is the alias for the function call.

Import a Parquet file:

```sql
CREATE TABLE iris_parquet AS
  SELECT r['sepal.length']::float8 AS sepal_length,
         r['variety']::text        AS variety
  FROM read_parquet('/tmp/iris.parquet') AS r;
SELECT variety, count(*) FROM iris_parquet GROUP BY variety ORDER BY variety;
```

Import a CSV file the same way:

```sql
CREATE TABLE iris_csv AS
  SELECT r['sepal.length']::float8 AS sepal_length,
         r['variety']::text        AS variety
  FROM read_csv('/tmp/iris.csv') AS r;
```

Import a JSON file:

```sql
CREATE TABLE events AS
  SELECT r['a']::text AS a, r['b']::text AS b, r['c']::text AS c
  FROM read_json('/tmp/table.json') AS r;
```

After import, the data is in a regular YugabyteDB table:

```sql
SELECT round(avg(sepal_length)::numeric, 4) AS avg_sepal_length FROM iris_parquet;
```

### Export YugabyteDB data to a lake file

Use `COPY (SELECT ...) TO` to write the result of a query to a lake file:

```sql
COPY (SELECT id, val FROM my_table ORDER BY id) TO '/tmp/export.parquet';
```

The same command works against an object store path (see [Object store support](#object-store-support)):

```sql
COPY (SELECT id, val FROM my_table) TO 's3://my-bucket/export.parquet';
```

## Object store support

pg_duckdb can read and write files on Amazon S3 and Google Cloud Storage using the `s3://` and `gs://` URI schemes.

Configure credentials with the `duckdb.create_simple_secret()` utility function. For S3:

```sql
SELECT duckdb.create_simple_secret(
    type   := 'S3',
    key_id := 'your_access_key_id',
    secret := 'your_secret_access_key',
    region := 'us-east-1'
);
```

After you configure credentials, use the object store path anywhere a file path is accepted:

```sql
CREATE TABLE sales AS
  SELECT r['id']::bigint AS id, r['amount']::numeric AS amount
  FROM read_parquet('s3://my-bucket/sales/*.parquet') AS r;
```

{{< note title="credential_chain is not supported" >}}
In YugabyteDB, the DuckDB `credential_chain` secret provider (which resolves credentials from the environment or instance metadata) is not supported. Specify credentials explicitly using `duckdb.create_simple_secret()`, or a `SERVER` with a `USER MAPPING` that provides `KEY_ID` and `SECRET`.
{{< /note >}}

## Limitations

pg_duckdb is limited to `lake_io` mode, which supports only the data-lake I/O described above. The `duckdb.execution_mode` setting is fixed and cannot be changed. The following are not supported and are rejected with a clear error:

- General DuckDB execution over YugabyteDB tables (for example, `duckdb.query()`, `duckdb.raw_query()`, and `SET duckdb.force_execution`).
- Mixing a lake-file read and a YugabyteDB table in a single statement (for example, joining `read_parquet(...)` with a YugabyteDB table). Import the lake file into a table first, then join.
- DuckDB-managed tables (`CREATE TABLE ... USING duckdb`, `CREATE TABLE ... USING duckdb AS ...`, and `ALTER TABLE ... SET ACCESS METHOD duckdb`).
- Installing or loading DuckDB extensions (`duckdb.install_extension()`, `duckdb.load_extension()`). Supported extensions are built in and load automatically.
- Data-lake table formats other than Parquet, CSV, and JSON reads (for example, `delta_scan()` and `iceberg_scan()`).
- [MotherDuck](https://motherduck.com/).
- Azure Blob Storage (`az://`). The DuckDB `azure` extension is not part of the bundled set (only `httpfs`, `json`, and `icu` are bundled), and `lake_io` mode does not allow loading additional extensions. Azure paths and `duckdb.create_azure_secret()` therefore do not work; use Amazon S3 (`s3://`) or Google Cloud Storage (`gs://`) instead.
- Exports whose query requires a sort or other parallel operation can terminate the session. An export that runs work on a DuckDB worker thread, for example, `COPY (SELECT ... ORDER BY ...) TO ...`, can crash the backend connection. As a workaround, set `duckdb.threads = 1` before running the export. (Tracked in {{<issue 32655>}}.)
