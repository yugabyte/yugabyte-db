# Functions

By default, functions are installed into the `public` schema. You can choose an alternate location by running `CREATE EXTENSION pg_duckdb WITH SCHEMA your_schema_name`.

> **Note**: `ALTER EXTENSION` is not currently supported for moving the extension to a different schema.

## Data Lake Functions

| Name | Description |
| :--- | :---------- |
| [`read_parquet`](#read_parquet) | Read a Parquet file |
| [`read_csv`](#read_csv) | Read a CSV file |
| [`read_json`](#read_json) | Read a JSON file |
| [`iceberg_scan`](#iceberg_scan) | Read an Iceberg dataset |
| [`iceberg_metadata`](#iceberg_metadata) | Read Iceberg metadata |
| [`iceberg_snapshots`](#iceberg_snapshots) | Read Iceberg snapshot information |
| [`delta_scan`](#delta_scan) | Read a Delta dataset |

## JSON Functions

All of the DuckDB [json functions and aggregates](https://duckdb.org/docs/data/json/json_functions.html). Postgres JSON/JSONB functions are not supported.

## Union Type Functions

|Name|Description|
| :--- | :---------- |
| [`union_extract`](#union_extract) | Extracts a value from a union type by tag name. |
| [`union_tag`](#union_tag) | Gets the tag name of the active member in a union type. |

## MAP Functions

All of the DuckDB [map functions](https://duckdb.org/docs/sql/data_types/map.html#map-functions).

| Name | Description |
| :--- | :---------- |
| [`cardinality`](#cardinality) | Return the size of the map |
| [`element_at`](#element_at) | Return the value for a given key as a list |
| [`map_concat`](#map_concat) | Merge multiple maps |
| [`map_contains`](#map_contains) | Check if a map contains a given key |
| [`map_contains_entry`](#map_contains_entry) | Check if a map contains a given key-value pair |
| [`map_contains_value`](#map_contains_value) | Check if a map contains a given value |
| [`map_entries`](#map_entries) | Return a list of struct(k, v) for each key-value pair |
| [`map_extract`](#map_extract) | Extract a value from a map using a key |
| [`map_extract_value`](#map_extract_value) | Return the value for a given key or NULL |
| [`map_from_entries`](#map_from_entries) | Create a map from an array of struct(k, v) |
| [`map_keys`](#map_keys) | Get all keys from a map as a list |
| [`map_values`](#map_values) | Get all values from a map as a list |

## Aggregates

|Name|Description|
| :--- | :---------- |
| [`approx_count_distinct`](#approx_count_distinct) | Approximates the count of distinct elements using HyperLogLog. |

## Sampling Functions

|Name|Description|
| :--- | :---------- |
| [`TABLESAMPLE`](#tablesample) | Samples a subset of rows from a table or query result. |

## Time Functions

|Name|Description|
| :--- | :---------- |
| [`time_bucket`](#time_bucket) | Buckets timestamps into time intervals for time-series analysis. |
| [`strftime`](#strftime) | Formats timestamps as strings using format codes. |
| [`strptime`](#strptime) | Parses strings into timestamps using format codes. |
| [`epoch`](#epoch) | Converts timestamps to Unix epoch seconds. |
| [`epoch_ms`](#epoch_ms) | Converts timestamps to Unix epoch milliseconds. |
| [`epoch_us`](#epoch_us) | Converts timestamps to Unix epoch microseconds. |
| [`epoch_ns`](#epoch_ns) | Converts timestamps to Unix epoch nanoseconds. |
| [`make_timestamp`](#make_timestamp) | Creates a timestamp from microseconds since epoch. |
| [`make_timestamptz`](#make_timestamptz) | Creates a timestamp with timezone from microseconds since epoch. |

## DuckDB Administration Functions

| Name | Description |
| :--- | :---------- |
| [`duckdb.install_extension`](#install_extension) | Installs a DuckDB extension. |
| [`duckdb.load_extension`](#load_extension) | Loads a DuckDB extension for the current session. |
| [`duckdb.autoload_extension`](#autoload_extension) | Configures whether an extension should be auto-loaded. |
| [`duckdb.query`](#query) | Runs a `SELECT` query directly against DuckDB. |
| [`duckdb.raw_query`](#raw_query) | Runs any query directly against DuckDB (for debugging). |
| [`duckdb.recycle_ddb`](#recycle_ddb) | Resets the DuckDB instance in the current connection (for debugging). |

## Secrets Management Functions

| Name | Description |
| :--- | :---------- |
| [`duckdb.create_simple_secret`](#create_simple_secret) | Creates a simple secret for cloud storage access. |
| [`duckdb.create_azure_secret`](#create_azure_secret) | Creates an Azure secret using a connection string. |

## Motherduck Functions

| Name | Description |
| :--- | :---------- |
| [`duckdb.enable_motherduck`](#enable_motherduck) | Enables MotherDuck integration with a token. |
| [`duckdb.is_motherduck_enabled`](#is_motherduck_enabled) | Checks if MotherDuck integration is enabled. |
| [`duckdb.force_motherduck_sync`](#force_motherduck_sync) | Forces a full resync of MotherDuck databases and schemas to Postgres (for debugging). |

## Detailed Descriptions

#### <a name="read_parquet"></a>`read_parquet(path TEXT or TEXT[], ...)` -> `SETOF duckdb.row`

Reads a parquet file, either from a remote location (via httpfs) or a local file.

This returns DuckDB rows, you can expand them using `*` or you can select specific columns using the `r['mycol']` syntax. If you want to select specific columns you should give the function call an easy alias, like `r`. For example:

```sql
SELECT * FROM read_parquet('file.parquet');
SELECT r['id'], r['name'] FROM read_parquet('file.parquet') r WHERE r['age'] > 21;
SELECT COUNT(*) FROM read_parquet('file.parquet');
```

Further information:

* [DuckDB Parquet documentation](https://duckdb.org/docs/data/parquet/overview)
* [DuckDB httpfs documentation](https://duckdb.org/docs/extensions/httpfs/https.html)

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| path | text or text[] | The path, either to a remote httpfs file or a local file (if enabled), of the parquet file(s) to read. The path can be a glob or array of files to read. |

##### Optional Parameters

Optional parameters mirror [DuckDB's read_parquet function](https://duckdb.org/docs/data/parquet/overview.html#parameters). To specify optional parameters, use `parameter := 'value'`.

#### <a name="read_csv"></a>`read_csv(path TEXT or TEXT[], ...)` -> `SETOF duckdb.row`

Reads a CSV file, either from a remote location (via httpfs) or a local file.

This returns DuckDB rows, you can expand them using `*` or you can select specific columns using the `r['mycol']` syntax. If you want to select specific columns you should give the function call an easy alias, like `r`. For example:

```sql
SELECT * FROM read_csv('file.csv');
SELECT r['id'], r['name'] FROM read_csv('file.csv') r WHERE r['age'] > 21;
SELECT COUNT(*) FROM read_csv('file.csv');
```

Further information:

* [DuckDB CSV documentation](https://duckdb.org/docs/data/csv/overview)
* [DuckDB httpfs documentation](https://duckdb.org/docs/extensions/httpfs/https.html)

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| path | text or text[] | The path, either to a remote httpfs file or a local file (if enabled), of the CSV file(s) to read. The path can be a glob or array of files to read. |

##### Optional Parameters

Optional parameters mirror [DuckDB's read_csv function](https://duckdb.org/docs/data/csv/overview.html#parameters). To specify optional parameters, use `parameter := 'value'`.

Compatibility notes:

* `columns` is not currently supported.
* `nullstr` must be an array (`TEXT[]`).

#### <a name="read_json"></a>`read_json(path TEXT or TEXT[], ...)` -> `SETOF duckdb.row`

Reads a JSON file, either from a remote location (via httpfs) or a local file.

This returns DuckDB rows, you can expand them using `*` or you can select specific columns using the `r['mycol']` syntax. If you want to select specific columns you should give the function call an easy alias, like `r`. For example:

```sql
SELECT * FROM read_json('file.json');
SELECT r['id'], r['name'] FROM read_json('file.json') r WHERE r['age'] > 21;
SELECT COUNT(*) FROM read_json('file.json');
```

Further information:

* [DuckDB JSON documentation](https://duckdb.org/docs/data/json/overview)

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| path | text or text[] | The path, either to a remote httpfs file or a local file (if enabled), of the JSON file(s) to read. The path can be a glob or array of files to read. |

##### Optional Parameters

Optional parameters mirror [DuckDB's read_json function](https://duckdb.org/docs/data/json/loading_json#json-read-functions). To specify optional parameters, use `parameter := 'value'`.

Compatibility notes:

* `columns` is not currently supported.

#### <a name="iceberg_scan"></a>`iceberg_scan(path TEXT, ...)` -> `SETOF duckdb.row`

Reads an Iceberg table, either from a remote location (via httpfs) or a local directory.

To use `iceberg_scan`, you must enable the `iceberg` extension:

```sql
SELECT duckdb.install_extension('iceberg');
```

This returns DuckDB rows, you can expand them using `*` or you can select specific columns using the `r['mycol']` syntax. If you want to select specific columns you should give the function call an easy alias, like `r`. For example:

```sql
SELECT * FROM iceberg_scan('data/iceberg/table');
SELECT r['id'], r['name'] FROM iceberg_scan('data/iceberg/table') r WHERE r['age'] > 21;
SELECT COUNT(*) FROM iceberg_scan('data/iceberg/table');
```

Further information:

* [DuckDB Iceberg extension documentation](https://duckdb.org/docs/extensions/iceberg.html)

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| path | text | The path, either to a remote httpfs location or a local location (if enabled), of the Iceberg table to read. |

##### Optional Arguments

Optional parameters mirror DuckDB's `iceberg_scan` function based on the DuckDB source code. However, documentation on these parameters is limited. To specify optional parameters, use `parameter := 'value'`.

| Name | Type | Default | Description |
| :--- | :--- | :------ | :---------- |
| allowed_moved_paths | boolean | false | Ensures that some path resolution is performed, which allows scanning Iceberg tables that are moved. |
| mode | text | `''` | |
| metadata_compression_codec | text | `'none'` | |
| skip_schema_inference | boolean | false | |
| version | text | `'version-hint.text'` | |
| version_name_format | text | `'v%s%s.metadata.json,%s%s.metadata.json'` | |

#### <a name="iceberg_metadata"></a>`iceberg_metadata(path TEXT, ...)` -> `SETOF iceberg_metadata_record`

To use `iceberg_metadata`, you must enable the `iceberg` extension:

```sql
SELECT duckdb.install_extension('iceberg');
```

Return metadata about an iceberg table. Data is returned as a set of `icerberg_metadata_record`, which is defined as:

```sql
CREATE TYPE duckdb.iceberg_metadata_record AS (
  manifest_path TEXT,
  manifest_sequence_number NUMERIC,
  manifest_content TEXT,
  status TEXT,
  content TEXT,
  file_path TEXT
);
```

Further information:

* [DuckDB Iceberg extension documentation](https://duckdb.org/docs/extensions/iceberg.html)

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| path | text | The path, either to a remote httpfs location or a local location (if enabled), of the Iceberg table to read. |

##### Optional Arguments

Optional parameters mirror DuckDB's `iceberg_metadata` function based on the DuckDB source code. However, documentation on these parameters is limited. To specify optional parameters, use `parameter := 'value'`.

| Name | Type | Default | Description |
| :--- | :--- | :------ | :---------- |
| allowed_moved_paths | boolean | false | Ensures that some path resolution is performed, which allows scanning Iceberg tables that are moved. |
| metadata_compression_codec | text | `'none'` | |
| skip_schema_inference | boolean | false | |
| version | text | `'version-hint.text'` | |
| version_name_format | text | `'v%s%s.metadata.json,%s%s.metadata.json'` | |

#### <a name="iceberg_snapshots"></a>`iceberg_snapshots(path TEXT, ...)` -> `SETOF iceberg_snapshot_record`

Reads Iceberg snapshot information from an Iceberg table.

To use `iceberg_snapshots`, you must enable the `iceberg` extension:

```sql
SELECT duckdb.install_extension('iceberg');
```

This function returns snapshot metadata for an Iceberg table, which can be useful for time travel queries and understanding table history.

```sql
SELECT * FROM iceberg_snapshots('data/iceberg/table');
```

Further information:

* [DuckDB Iceberg extension documentation](https://duckdb.org/docs/extensions/iceberg.html)

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| path | text | The path, either to a remote httpfs location or a local location (if enabled), of the Iceberg table to read. |

##### Optional Arguments

Optional parameters mirror DuckDB's `iceberg_snapshots` function. To specify optional parameters, use `parameter := 'value'`.

| Name | Type | Default | Description |
| :--- | :--- | :------ | :---------- |
| metadata_compression_codec | text | `'none'` | |
| skip_schema_inference | boolean | false | |
| version | text | `'version-hint.text'` | |
| version_name_format | text | `'v%s%s.metadata.json,%s%s.metadata.json'` | |

#### <a name="delta_scan"></a>`delta_scan(path TEXT)` -> `SETOF duckdb.row`

Reads a delta dataset, either from a remote (via httpfs) or a local location.

To use `delta_scan`, you must enable the `delta` extension:

```sql
SELECT duckdb.install_extension('delta');
```

This returns DuckDB rows, you can expand them using `*` or you can select specific columns using the `r['mycol']` syntax. If you want to select specific columns you should give the function call an easy alias, like `r`. For example:

```sql
SELECT * FROM delta_scan('/path/to/delta/dataset');
SELECT r['id'], r['name'] FROM delta_scan('/path/to/delta/dataset') r WHERE r['age'] > 21;
SELECT COUNT(*) FROM delta_scan('/path/to/delta/dataset');
```


Further information:

* [DuckDB Delta extension documentation](https://duckdb.org/docs/extensions/delta)

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| path | text | The path, either to a remote httpfs location or a local location (if enabled) of the delta dataset to read. |

#### <a name="install_extension"></a>`duckdb.install_extension(extension_name TEXT, repository TEXT DEFAULT 'core')` -> `bool`

Installs a DuckDB extension and configures it to be loaded automatically in
every session that uses pg_duckdb.

```sql
SELECT duckdb.install_extension('iceberg');
SELECT duckdb.install_extension('avro', 'community');
```

##### Security

Since this function can be used to install and download any extensions it can
only be executed by a superuser by default. To allow execution by some other
admin user, such as `my_admin`, you can grant such a user the following
permissions:

```sql
GRANT ALL ON FUNCTION duckdb.install_extension(TEXT, TEXT) TO my_admin;
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| extension_name | text | The name of the extension to install |

#### <a name="load_extension"></a>`duckdb.load_extension(extension_name TEXT)` -> `void`

Loads a DuckDB extension for the current session only. Unlike `install_extension`, this doesn't configure the extension to be loaded automatically in future sessions.

```sql
SELECT duckdb.load_extension('iceberg');
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| extension_name | text | The name of the extension to load |

#### <a name="autoload_extension"></a>`duckdb.autoload_extension(extension_name TEXT, autoload BOOLEAN)` -> `void`

Configures whether an installed extension should be automatically loaded in new sessions.

```sql
-- Disable auto-loading for an extension
SELECT duckdb.autoload_extension('iceberg', false);

-- Enable auto-loading for an extension
SELECT duckdb.autoload_extension('iceberg', true);
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| extension_name | text | The name of the extension to configure |
| autoload | boolean | Whether the extension should be auto-loaded |

#### <a name="query"></a>`duckdb.query(query TEXT)` -> `SETOF duckdb.row`

Executes the given SELECT query directly against DuckDB. This can be useful if DuckDB syntax makes the query easier to write or if you want to use a function that is not exposed by pg_duckdb yet. If you use it because of a missing function in pg_duckdb, please also open an issue on the GitHub repository so that we can add support. For example the below query shows a query that puts `FROM` before `SELECT` and uses a list comprehension. Both of those features are not supported in Postgres.

```sql
SELECT * FROM duckdb.query('FROM range(10) as a(a) SELECT [a for i in generate_series(0, a)] as arr');
```

#### <a name="raw_query"></a>`duckdb.raw_query(query TEXT)` -> `void`

Runs an arbitrary query directly against DuckDB. Compared to `duckdb.query`, this function can execute any query, not just SELECT queries. The main downside is that it doesn't return its result as rows, but instead sends the query result to the logs. So the recommendation is to use `duckdb.query` when possible, but if you need to run e.g. some DDL you can use this function.

#### <a name="recycle_ddb"></a>`duckdb.recycle_ddb()` -> `void`

pg_duckdb keeps the DuckDB instance open inbetween transactions. This is done
to save session level state, such as manually done `SET` commands. If you want
to clear this session level state for some reason you can close the currently
open DuckDB instance using:

```sql
CALL duckdb.recycle_ddb();
```

#### <a name="enable_motherduck"></a>`duckdb.enable_motherduck(token TEXT, database_name TEXT)` -> `void`

Enables MotherDuck integration with the provided authentication token.

```sql
-- Enable MotherDuck with default database
SELECT duckdb.enable_motherduck('your_token_here');

-- Enable MotherDuck with specific database
SELECT duckdb.enable_motherduck('your_token_here', 'my_database');
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| token | text | Your MotherDuck authentication token |

##### Optional Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| database_name | text | Specific MotherDuck database to connect to |

#### <a name="is_motherduck_enabled"></a>`duckdb.is_motherduck_enabled()` -> `boolean`

Checks whether MotherDuck integration is currently enabled for this session.

```sql
SELECT duckdb.is_motherduck_enabled();
```

#### <a name="create_simple_secret"></a>`duckdb.create_simple_secret(type TEXT, key_id TEXT, secret TEXT, region TEXT, ...)` -> `void`

Creates a simple secret for accessing cloud storage services like S3, GCS, or R2.

```sql
-- Create an S3 secret
SELECT duckdb.create_simple_secret(
    type := 'S3',
    key_id := 'your_access_key',
    secret := 'your_secret_key',
    region := 'us-east-1'
);

-- Create an S3 secret with session token
SELECT duckdb.create_simple_secret(
    type := 'S3',
    key_id := 'your_access_key',
    secret := 'your_secret_key',
    region := 'us-east-1',
    session_token := 'your_session_token'
);
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| type | text | The type of secret ('S3', 'GCS', 'R2', etc.) |
| key_id | text | The access key ID or equivalent |
| secret | text | The secret key or equivalent |
| region | text | The region for the service |

##### Optional Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| session_token | text | Session token for temporary credentials |
| endpoint | text | Custom endpoint URL |
| url_style | text | URL style ('vhost' or 'path') |
| use_ssl | text | Whether to use SSL ('true' or 'false') |
| scope | text | Scope for the secret (default: '') |

#### <a name="create_azure_secret"></a>`duckdb.create_azure_secret(connection_string TEXT, scope TEXT DEFAULT '')` -> `TEXT`

Creates an Azure secret using an Azure Blob Storage connection string.

```sql
-- Create an Azure secret
SELECT duckdb.create_azure_secret(
    'DefaultEndpointsProtocol=https;AccountName=myaccount;AccountKey=mykey;EndpointSuffix=core.windows.net'
);

-- Create an Azure secret with specific scope
SELECT duckdb.create_azure_secret(
    'DefaultEndpointsProtocol=https;AccountName=myaccount;AccountKey=mykey;EndpointSuffix=core.windows.net',
    'my_scope'
);
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| connection_string | text | The Azure Blob Storage connection string |

##### Optional Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| scope | text | Scope for the secret (default: '') |

#### <a name="force_motherduck_sync"></a>`duckdb.force_motherduck_sync(drop_with_cascade BOOLEAN DEFAULT false)`

> **Warning**: There are known issues with this function. To re-trigger a sync, it is recommended to use the following command instead:
>
> ```sql
> SELECT * FROM pg_terminate_backend((
>   SELECT pid FROM pg_stat_activity WHERE backend_type = 'pg_duckdb sync worker'
> ));
> ```

`pg_duckdb` will normally automatically synchronize your MotherDuck tables with Postgres using a Postgres background worker. Sometimes this synchronization fails. This can happen for various reasons, but often this is due to permission issues or users having created dependencies on MotherDuck tables that need to be updated. In those cases this function can be helpful for a few reasons:

1. To show the ERRORs that happen during syncing
2. To retrigger a sync after fixing the issue
3. To drop the MotherDuck tables with `CASCADE` to drop all objects that depend on it.

For the first two usages you can simply call this procedure like follows:

```sql
CALL duckdb.force_motherduck_sync();
```

But for the third usage you need to run pass it the `drop_with_cascade` parameter:

```sql
CALL duckdb.force_motherduck_sync(drop_with_cascade := true);
```

NOTE: Dropping with cascade will drop all objects that depend on the MotherDuck tables. This includes all views, functions, and tables that depend on the MotherDuck tables. This can be a destructive operation, so use with caution.

#### <a name="time_bucket"></a>`time_bucket(bucket_width INTERVAL, timestamp_col TIMESTAMP, origin TIMESTAMP)` -> `TIMESTAMP`

Buckets timestamps into time intervals for time-series analysis. This function is compatible with TimescaleDB's `time_bucket` function, allowing for easier migration and interoperability.

```sql
-- Group events by hour
SELECT time_bucket(INTERVAL '1 hour', created_at) as hour_bucket, COUNT(*)
FROM events
GROUP BY hour_bucket
ORDER BY hour_bucket;

-- Group by 15-minute intervals
SELECT time_bucket(INTERVAL '15 minutes', timestamp_col), AVG(value)
FROM sensor_data
WHERE timestamp_col >= '2024-01-01'
GROUP BY 1
ORDER BY 1;
```

Further information:
* [DuckDB date_trunc function](https://duckdb.org/docs/sql/functions/dateformat.html#date_trunc)
* [TimescaleDB time_bucket function](https://docs.timescale.com/api/latest/hyperfunctions/time_bucket/)

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| bucket_width | interval | The interval size for bucketing (e.g., '1 hour', '15 minutes') |
| timestamp_col | timestamp | The timestamp column to bucket |

##### Optional Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| origin | timestamp | The origin point for bucketing. Buckets are aligned to this timestamp. |

**Note**: The `time_bucket` function also supports timezone and time offset parameters for more advanced time bucketing scenarios.

#### <a name="strftime"></a>`strftime(timestamp_expr, format_string)` -> `TEXT`

Formats timestamps as strings using standard format codes. This function provides flexible timestamp formatting for display and export purposes.

```sql
-- Format current timestamp
SELECT strftime(NOW(), '%Y-%m-%d %H:%M:%S') AS formatted_time;

-- Format timestamps in different formats
SELECT
    order_id,
    strftime(created_at, '%Y-%m-%d') AS order_date,
    strftime(created_at, '%H:%M') AS order_time,
    strftime(created_at, '%A, %B %d, %Y') AS readable_date
FROM orders;

-- Use for partitioning file exports
COPY (SELECT * FROM events WHERE event_date = '2024-01-01')
TO 's3://bucket/events/' || strftime('2024-01-01'::timestamp, '%Y/%m/%d') || '/events.parquet';
```

Common format codes:
- `%Y` - 4-digit year (2024)
- `%m` - Month as number (01-12)
- `%d` - Day of month (01-31)
- `%H` - Hour (00-23)
- `%M` - Minute (00-59)
- `%S` - Second (00-59)
- `%A` - Full weekday name (Monday)
- `%B` - Full month name (January)

Further information:
* [DuckDB strftime documentation](https://duckdb.org/docs/sql/functions/dateformat.html#strftime)

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| timestamp_expr | timestamp | The timestamp value to format |
| format_string | text | The format string with format codes |

#### <a name="strptime"></a>`strptime(string_expr, format_string)` -> `TIMESTAMP`

Parses strings into timestamps using format codes. This is the inverse of `strftime` and is useful for parsing timestamps from various string formats.

```sql
-- Parse date strings
SELECT strptime('2024-01-15 14:30:00', '%Y-%m-%d %H:%M:%S') AS parsed_timestamp;

-- Parse different formats
SELECT
    strptime('Jan 15, 2024', '%b %d, %Y') AS date1,
    strptime('15/01/2024', '%d/%m/%Y') AS date2,
    strptime('2024-01-15T14:30:00Z', '%Y-%m-%dT%H:%M:%SZ') AS iso_date;

-- Parse log timestamps
SELECT
    log_id,
    strptime(timestamp_string, '%Y-%m-%d %H:%M:%S') AS parsed_time,
    message
FROM raw_logs;
```

Further information:
* [DuckDB strptime documentation](https://duckdb.org/docs/sql/functions/dateformat.html#strptime)

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| string_expr | text | The string to parse as a timestamp |
| format_string | text | The format string describing the input format |

#### <a name="epoch"></a>`epoch(timestamp_expr)` -> `BIGINT`

Converts timestamps to Unix epoch seconds (seconds since 1970-01-01 00:00:00 UTC).

```sql
-- Get current epoch time
SELECT epoch(NOW()) AS current_epoch;

-- Convert timestamps for API usage
SELECT
    event_id,
    epoch(event_timestamp) AS epoch_seconds
FROM events;

-- Filter using epoch time
SELECT * FROM events
WHERE epoch(created_at) > 1640995200; -- After 2022-01-01
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| timestamp_expr | timestamp | The timestamp to convert to epoch seconds |

#### <a name="map_extract"></a>`map_extract(map_col duckdb.map, key duckdb.unresolved_type) -> duckdb.unresolved_type`

Extracts a value from a map using the specified key. If the key doesn't exist, returns an empty array.

```sql
-- Extract value from a map
SELECT map_extract(r['map_col'], 'a') as value 
FROM duckdb.query($$ SELECT MAP(['a', 'b'], [1, 2]) as map_col $$) r;
-- Returns: {1}

-- Extract non-existent key
SELECT map_extract(r['map_col'], 'c') as value 
FROM duckdb.query($$ SELECT MAP(['a', 'b'], [1, 2]) as map_col $$) r;
-- Returns: {}
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| map_col | duckdb.map | The map to extract from |
| key | duckdb.unresolved_type | The key to look up in the map |

#### <a name="map_keys"></a>`map_keys(map_col duckdb.map) -> duckdb.unresolved_type`

Returns all keys from a map as an array.

```sql
-- Get all keys from a map
SELECT map_keys(r['map_col']) as keys 
FROM duckdb.query($$ SELECT MAP(['a', 'b', 'c'], [1, 2, 3]) as map_col $$) r;
-- Returns: {a,b,c}

-- Empty map
SELECT map_keys(r['map_col']) as keys 
FROM duckdb.query($$ SELECT MAP([], []) as map_col $$) r;
-- Returns: {}
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| map_col | duckdb.map | The map to extract keys from |

#### <a name="map_values"></a>`map_values(map_col duckdb.map) -> duckdb.unresolved_type`

Returns all values from a map as an array.

```sql
-- Get all values from a map
SELECT map_values(r['map_col']) as values 
FROM duckdb.query($$ SELECT MAP(['a', 'b', 'c'], [1, 2, 3]) as map_col $$) r;
-- Returns: {1,2,3}

-- Empty map
SELECT map_values(r['map_col']) as values 
FROM duckdb.query($$ SELECT MAP([], []) as map_col $$) r;
-- Returns: {}
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| map_col | duckdb.map | The map to extract values from |

#### <a name="cardinality"></a>`cardinality(map_col duckdb.map) -> numeric`

Returns the size of the map (number of key-value pairs).

```sql
-- Get the number of entries in a map
SELECT cardinality(r['map_col']) as size 
FROM duckdb.query($$ SELECT MAP(['a', 'b', 'c'], [1, 2, 3]) as map_col $$) r;
-- Returns: 3

-- Empty map
SELECT cardinality(r['map_col']) as size 
FROM duckdb.query($$ SELECT MAP([], []) as map_col $$) r;
-- Returns: 0
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| map_col | duckdb.map | The map to get the size of |

#### <a name="element_at"></a>`element_at(map_col duckdb.map, key duckdb.unresolved_type) -> duckdb.unresolved_type`

Returns the value for a given key as an array.

```sql
-- Get value for a specific key
SELECT element_at(r['map_col'], 'a') as value 
FROM duckdb.query($$ SELECT MAP(['a', 'b'], [1, 2]) as map_col $$) r;
-- Returns: {1}

-- Non-existent key
SELECT element_at(r['map_col'], 'c') as value 
FROM duckdb.query($$ SELECT MAP(['a', 'b'], [1, 2]) as map_col $$) r;
-- Returns: {}
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| map_col | duckdb.map | The map to extract from |
| key | duckdb.unresolved_type | The key to look up in the map |

#### <a name="map_concat"></a>`map_concat(map_col duckdb.map, map_col2 duckdb.map) -> duckdb.map`

Merges multiple maps. On key collision, the value is taken from the last map.

```sql
-- Merge two maps
SELECT map_concat(r1['map1'], r2['map2']) as merged 
FROM duckdb.query($$ SELECT MAP(['a', 'b'], [1, 2]) as map1 $$) r1, 
     duckdb.query($$ SELECT MAP(['b', 'c'], [3, 4]) as map2 $$) r2;
-- Returns: {a=1, b=3, c=4}

-- Note: 'b' value from map2 (3) overwrites map1's value (2)
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| map_col | duckdb.map | The first map |
| map_col2 | duckdb.map | The second map to merge |

#### <a name="map_contains"></a>`map_contains(map_col duckdb.map, key duckdb.unresolved_type) -> boolean`

Checks if a map contains a given key.

```sql
-- Check if key exists
SELECT map_contains(r['map_col'], 'a') as has_key 
FROM duckdb.query($$ SELECT MAP(['a', 'b'], [1, 2]) as map_col $$) r;
-- Returns: t (true)

-- Check for non-existent key
SELECT map_contains(r['map_col'], 'c') as has_key 
FROM duckdb.query($$ SELECT MAP(['a', 'b'], [1, 2]) as map_col $$) r;
-- Returns: f (false)
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| map_col | duckdb.map | The map to check |
| key | duckdb.unresolved_type | The key to search for |

#### <a name="map_contains_entry"></a>`map_contains_entry(map_col duckdb.map, key duckdb.unresolved_type, value duckdb.unresolved_type) -> boolean`

Checks if a map contains a given key-value pair.

```sql
-- Check if key-value pair exists
SELECT map_contains_entry(r['map_col'], 'a', 1) as has_entry 
FROM duckdb.query($$ SELECT MAP(['a', 'b'], [1, 2]) as map_col $$) r;
-- Returns: t (true)

-- Check with wrong value for existing key
SELECT map_contains_entry(r['map_col'], 'a', 2) as has_entry 
FROM duckdb.query($$ SELECT MAP(['a', 'b'], [1, 2]) as map_col $$) r;
-- Returns: f (false)
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| map_col | duckdb.map | The map to check |
| key | duckdb.unresolved_type | The key to search for |
| value | duckdb.unresolved_type | The value to match with the key |

#### <a name="map_contains_value"></a>`map_contains_value(map_col duckdb.map, value duckdb.unresolved_type) -> boolean`

Checks if a map contains a given value.

```sql
-- Check if value exists
SELECT map_contains_value(r['map_col'], 1) as has_value 
FROM duckdb.query($$ SELECT MAP(['a', 'b'], [1, 2]) as map_col $$) r;
-- Returns: t (true)

-- Check for non-existent value
SELECT map_contains_value(r['map_col'], 3) as has_value 
FROM duckdb.query($$ SELECT MAP(['a', 'b'], [1, 2]) as map_col $$) r;
-- Returns: f (false)
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| map_col | duckdb.map | The map to check |
| value | duckdb.unresolved_type | The value to search for |

#### <a name="map_entries"></a>`map_entries(map_col duckdb.map) -> duckdb.struct[]`

Returns an array of struct(key, value) for each key-value pair in the map.

```sql
-- Get all key-value pairs as structs
SELECT map_entries(r['map_col']) as entries 
FROM duckdb.query($$ SELECT MAP(['a', 'b'], [1, 2]) as map_col $$) r;
-- Returns: {"(a,1)","(b,2)"}

-- Access individual struct fields
SELECT unnest(map_entries(r['map_col'])) as entry 
FROM duckdb.query($$ SELECT MAP(['a', 'b'], [1, 2]) as map_col $$) r;
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| map_col | duckdb.map | The map to extract entries from |

#### <a name="map_extract_value"></a>`map_extract_value(map_col duckdb.map, key duckdb.unresolved_type) -> duckdb.unresolved_type`

Returns the value for a given key or NULL if the key is not contained in the map. 

```sql
-- Extract single value (not as array)
SELECT map_extract_value(r['map_col'], 'a') as value 
FROM duckdb.query($$ SELECT MAP(['a', 'b'], [1, 2]) as map_col $$) r;
-- Returns: 1

-- Non-existent key returns NULL
SELECT map_extract_value(r['map_col'], 'c') as value 
FROM duckdb.query($$ SELECT MAP(['a', 'b'], [1, 2]) as map_col $$) r;
-- Returns: NULL
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| map_col | duckdb.map | The map to extract from |
| key | duckdb.unresolved_type | The key to look up in the map |

#### <a name="map_from_entries"></a>`map_from_entries(entries duckdb.struct[]) -> duckdb.map`

Creates a map from an array of struct(k, v).

```sql
-- Create map from array of structs
SELECT map_from_entries(r['entries']) as new_map 
FROM duckdb.query($$ 
    SELECT [{'k': 'a', 'v': 1}, {'k': 'b', 'v': 2}] as entries 
$$) r;
-- Returns: {a=1, b=2}

-- This is the inverse operation of map_entries
SELECT map_from_entries(map_entries(r['map_col'])) as reconstructed 
FROM duckdb.query($$ SELECT MAP(['x', 'y'], [10, 20]) as map_col $$) r;
-- Returns: {x=10, y=20}
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| entries | duckdb.struct[] | Array of structs with 'k' (key) and 'v' (value) fields |

#### <a name="epoch_ms"></a>`epoch_ms(timestamp_expr)` -> `BIGINT`

Converts timestamps to Unix epoch milliseconds.

```sql
-- High-precision timestamp for JavaScript
SELECT epoch_ms(NOW()) AS timestamp_ms;

-- For time-series data
SELECT
    sensor_id,
    epoch_ms(reading_time) AS timestamp_ms,
    value
FROM sensor_readings;
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| timestamp_expr | timestamp | The timestamp to convert to epoch milliseconds |

#### `epoch_ms(milliseconds)` -> `TIMESTAMP`

Converts Unix epoch milliseconds to a timestamp. This is the inverse of the above function.

```sql
-- Convert epoch milliseconds to timestamp
SELECT epoch_ms(1640995200000) AS timestamp_from_ms; -- 2022-01-01 00:00:00

-- Convert stored milliseconds back to timestamps
SELECT
    event_id,
    epoch_ms(timestamp_ms) AS event_time
FROM events;
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| milliseconds | bigint | Milliseconds since Unix epoch |

#### <a name="epoch_us"></a>`epoch_us(timestamp_expr)` -> `BIGINT`

Converts timestamps to Unix epoch microseconds.

```sql
-- Microsecond precision timestamps
SELECT epoch_us(NOW()) AS timestamp_us;
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| timestamp_expr | timestamp | The timestamp to convert to epoch microseconds |

#### <a name="epoch_ns"></a>`epoch_ns(timestamp_expr)` -> `BIGINT`

Converts timestamps to Unix epoch nanoseconds.

```sql
-- Nanosecond precision timestamps
SELECT epoch_ns(NOW()) AS timestamp_ns;
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| timestamp_expr | timestamp | The timestamp to convert to epoch nanoseconds |

#### <a name="make_timestamp"></a>`make_timestamp(microseconds)` -> `TIMESTAMP`

Creates a timestamp from microseconds since Unix epoch (1970-01-01 00:00:00 UTC).

```sql
-- Create timestamp from current epoch microseconds
SELECT make_timestamp(epoch_us(NOW())) AS reconstructed_timestamp;

-- Create specific timestamps
SELECT make_timestamp(1640995200000000) AS new_years_2022; -- 2022-01-01 00:00:00
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| microseconds | bigint | Microseconds since Unix epoch |

#### <a name="make_timestamptz"></a>`make_timestamptz(microseconds)` -> `TIMESTAMPTZ`

Creates a timestamp with timezone from microseconds since Unix epoch.

```sql
-- Create timestamptz from current epoch microseconds
SELECT make_timestamptz(epoch_us(NOW())) AS reconstructed_timestamptz;

-- Create specific timestamptz
SELECT make_timestamptz(1640995200000000) AS new_years_2022_tz;
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| microseconds | bigint | Microseconds since Unix epoch |

#### <a name="tablesample"></a>`TABLESAMPLE (sampling_method(percentage | rows))`

Samples a subset of rows from a table or query result. This is useful for analyzing large datasets by working with representative samples, improving query performance for exploratory data analysis.

```sql
-- Sample 10% of rows from a table
SELECT * FROM large_table TABLESAMPLE SYSTEM(10);

-- Sample approximately 1000 rows
SELECT * FROM events TABLESAMPLE SYSTEM(1000 ROWS);

-- Sample from data lake files
SELECT * FROM read_parquet('s3://datalake/**/*.parquet') TABLESAMPLE SYSTEM(5);

-- Use sampling for quick data profiling
SELECT
    region,
    COUNT(*) as sample_count,
    AVG(revenue) as avg_revenue
FROM sales_data TABLESAMPLE SYSTEM(2)
GROUP BY region;

-- Sample from joins for performance
SELECT c.name, COUNT(o.id) as order_count
FROM customers c
JOIN orders o TABLESAMPLE SYSTEM(10) ON c.id = o.customer_id
GROUP BY c.name;
```

**Sampling Methods:**

- **SYSTEM**: Random sampling at the storage level (faster, approximate percentage)
- **BERNOULLI**: Row-by-row random sampling (slower, exact percentage)

```sql
-- System sampling (recommended for large tables)
SELECT * FROM huge_table TABLESAMPLE SYSTEM(1);

-- Bernoulli sampling (exact percentage)
SELECT * FROM medium_table TABLESAMPLE BERNOULLI(5);
```

**Use Cases:**

- **Data exploration**: Quick analysis of large datasets
- **Performance testing**: Test queries on sample data
- **Data profiling**: Understand data distribution patterns
- **ETL development**: Develop pipelines on sample data
- **Quality checks**: Validate data quality on samples

Further information:
* [DuckDB TABLESAMPLE documentation](https://duckdb.org/docs/sql/query_syntax/sample.html)

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| sampling_method | keyword | Either `SYSTEM` or `BERNOULLI` |
| percentage | numeric | Percentage of rows to sample (0-100) |

##### Optional Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| rows | integer | Approximate number of rows to sample (use with `ROWS` keyword) |

#### <a name="union_extract"></a>`union_extract(union_col, tag)` -> `duckdb.unresolved_type`

Extracts a value from a union type by specifying the tag name of the member you want to access.

```sql
-- Extract the string value if the union contains a string
SELECT union_extract(my_union_column, 'string') FROM my_table;

-- Extract integer value from union
SELECT union_extract(data_field, 'integer') AS extracted_int FROM mixed_data;
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| union_col | duckdb.union or duckdb.unresolved_type | The union column to extract from |
| tag | text | The tag name of the union member to extract |

#### <a name="union_tag"></a>`union_tag(union_col)` -> `duckdb.unresolved_type`

Returns the tag name of the currently active member in a union type.

```sql
-- Get the active tag for each row
SELECT union_tag(my_union_column) AS active_type FROM my_table;

-- Filter rows based on union tag
SELECT * FROM my_table WHERE union_tag(data_field) = 'string';
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| union_col | duckdb.union or duckdb.unresolved_type | The union column to get the tag from |

#### <a name="approx_count_distinct"></a>`approx_count_distinct(expression)` -> `BIGINT`

Approximates the count of distinct elements using the HyperLogLog algorithm. This is much faster than `COUNT(DISTINCT ...)` for large datasets, with a small error rate.

```sql
-- Approximate distinct count of customer IDs
SELECT approx_count_distinct(customer_id) FROM orders;

-- Compare with exact count
SELECT
    approx_count_distinct(customer_id) AS approx_distinct,
    COUNT(DISTINCT customer_id) AS exact_distinct
FROM orders;
```

##### Required Arguments

| Name | Type | Description |
| :--- | :--- | :---------- |
| expression | any | The expression to count distinct values for |
