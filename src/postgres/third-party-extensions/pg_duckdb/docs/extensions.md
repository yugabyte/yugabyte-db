# DuckDB Extensions

`pg_duckdb` supports a wide range of DuckDB extensions, allowing you to extend its functionality for various use cases.

| Extension | Description | Status |
| :--- | :--- | :--- |
| `httpfs` | HTTP/S3 file system support | Pre-installed |
| `json` | JSON functions and operators | Pre-installed |
| `iceberg` | Apache Iceberg support | Installable |
| `delta` | Delta Lake support | Installable |
| `azure` | Azure Blob Storage connectivity | Installable |
| Community | Various community extensions | Installable (requires configuration) |

Installing other extensions may work, but they have not been thoroughly tested and are used at your own risk

## Installing Extensions

By default, known extensions are allowed to be automatically installed and loaded when a DuckDB query depends on them. This behavior can be configured using the [`duckdb.autoinstall_known_extensions`](settings.md#duckdbautoinstall_known_extensions) and [`duckdb.autoload_known_extensions`](settings.md#duckdbautoload_known_extensions) settings.

It's also possible to manually install an extension. This can be useful when this auto-install/auto-load behavior is disabled, or when DuckDB fails to realize an extension is necessary to execute the query. Installing an extension requires superuser privileges.

```sql
-- Install the 'iceberg' extension
SELECT duckdb.install_extension('iceberg');
```

## Community Extensions

Community extensions can be installed when `duckdb.allow_community_extensions` is enabled. This requires superuser privileges for security reasons.

```sql
-- Enable community extensions (superuser required)
SET duckdb.allow_community_extensions = true;

-- Install a community extension
SELECT duckdb.install_extension('prql', 'community');
```

**Note**: In some environments, you may also need to enable unsigned extensions:

```sql
SET duckdb.allow_unsigned_extensions = true;
```

## Managing Extensions

Installing an extension causes it to be loaded and installed globally for any connection that uses DuckDB. The current list of installed extensions is maintained in the `duckdb.extensions` table. Superusers can use this table to view and manage extensions:

```sql
-- Install an extension
SELECT duckdb.install_extension('iceberg');
-- view currently installed extensions
SELECT * FROM duckdb.extensions;
-- Change an extension to stop being automatically loaded in new connections
SELECT duckdb.autoload_extension('iceberg', false);
-- For such extensions, you can still load them manually in a session
SELECT duckdb.load_extension('iceberg');
-- You can also install community extensions
SELECT duckdb.install_extension('prql', 'community');
```

## Supported Extensions

You can install any DuckDB extension, but you might run into various issues when trying to use them from PostgreSQL. Often you should be able to work around such issues by using `duckdb.query` or `duckdb.raw_query`. For some extensions, `pg_duckdb` has added dedicated support to PostgreSQL. These extensions are listed below.

### Core Extensions

#### `httpfs` and `json`
The `httpfs` and `json` extensions are pre-installed and loaded by default, providing out-of-the-box support for remote file access and JSON operations.

#### `azure`
Allows reading files from Azure Blob Storage using `az://...` filepaths.

#### `iceberg`
Apache Iceberg support adds functions to read Iceberg tables and metadata. For a complete list of Iceberg functions, see the [Functions documentation](functions.md).

#### `delta`
Delta Lake support adds the ability to read Delta Lake files via [delta_scan](functions.md#delta_scan).

### Extension Usage Examples

```sql
-- Install and use Iceberg
SELECT duckdb.install_extension('iceberg');
SELECT * FROM iceberg_scan('s3://bucket/iceberg-table/');

-- Install and use Delta
SELECT duckdb.install_extension('delta');
SELECT * FROM delta_scan('s3://bucket/delta-table/');
```

## Security Considerations

By default, executing `duckdb.install_extension(...)` and `duckdb.autoload_extension(...)` is only allowed for superusers. This is to prevent users from installing extensions that may have security implications or interfere with the database's operation.

This means that users can only use extensions that DuckDB has marked as "auto-installable". If you want to restrict the use of those extensions to a specific list, you can set [`duckdb.autoinstall_known_extensions`](settings.md#duckdbautoinstall_known_extensions) to `false`. This will prevent users from automatically installing any known extensions. Note that this requires that any extensions you **do** want to allow are already installed by a superuser.
