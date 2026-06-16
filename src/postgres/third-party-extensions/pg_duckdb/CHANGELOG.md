# 1.1.1 (2025-12-16)

## Changed

- Update to DuckDB v1.4.3. ([#985])
- Include `application_name` DuckDB its `custom_user_agent` setting.

[#985]: https://github.com/duckdb/pg_duckdb/pull/985

# 1.1.0 (2025-12-11)

## Added

- Add support for DuckDB MAP functions: `map_extract`, `map_keys`, `map_values`, `cardinality`, `element_at`, `map_concat`, `map_contains`, `map_contains_entry`, `map_contains_value`, `map_entries`, `map_extract_value`, and `map_from_entries`. ([#902])
- Add `use_ssl` parameter to `duckdb.create_simple_secret()` function. ([#956])
- Add support for arrays of DuckDB-only types (`STRUCT[]`, `UNION[]`, `MAP[]`). ([#922])
- Support reading from PostgreSQL tables with RLS enabled. ([#986])
- Add `duckdb.custom_user_agent` setting. ([#989])
- Support multiple users with different MotherDuck tokens (read-write and read-only) within the same Postgres database. ([#984])

## Changed

- Update to DuckDB v1.4.2. ([#933], [#961], [#973])
- Mark `duckdb.force_execution` as `GUC_REPORT` for better PgBouncer integration. When using PgBouncer in transaction pooling mode, you should configure `track_extra_parameters` to include `duckdb.force_execution` to ensure it syncs correctly across connections. ([#982])
- Make PostgreSQL 18 the default version in Docker images. ([#959])

## Fixed

- Check Postgres user privileges when querying through views. Previously a user could bypass access restrictions by querying a view that the user had access to, even if the user did not have access to the underlying tables. ([#949])
- Fix NULL ordering behavior to align with PostgreSQL: `ORDER BY col DESC` now uses `NULLS FIRST` semantics (matching Postgres), instead of `NULLS LAST` which was the previous DuckDB default. ([#935])
- Fix crash in the timezone assign hook when run outside of a transaction context. ([#981])
- Fix type mismatch where DuckDB `VARCHAR[]` was incorrectly mapped to PostgreSQL `VARCHAR[]` instead of `TEXT[]`. This caused assertion failures during statistics analysis. ([#979])
- Fix syncing of MotherDuck views that only contain columns with types not supported by pg_duckdb. Such views are now skipped instead of generating invalid SQL. ([#990])
- Fix error that blocked pg_duckdb usage when `duckdb.disabled_filesystems` was set to `LocalFileSystem` explicitly. ([#937])
- Fix undefined symbol error when building with GCC 8 on RHEL 8 and other older systems. ([#920])
- Fix cases where unsupported Postgres types (like too-wide decimals) used in expressions could cause DuckDB internal errors instead of a clear error message. ([#921])
- Make MotherDuck setup script in Dockerfile work correctly with PostgreSQL 14. ([#932])
- Fix crash that could happen when DuckDB threads would receive a signal. ([#936])

[#902]: https://github.com/duckdb/pg_duckdb/pull/902
[#956]: https://github.com/duckdb/pg_duckdb/pull/956
[#922]: https://github.com/duckdb/pg_duckdb/pull/922
[#986]: https://github.com/duckdb/pg_duckdb/pull/986
[#949]: https://github.com/duckdb/pg_duckdb/pull/949
[#933]: https://github.com/duckdb/pg_duckdb/pull/933
[#961]: https://github.com/duckdb/pg_duckdb/pull/961
[#973]: https://github.com/duckdb/pg_duckdb/pull/973
[#982]: https://github.com/duckdb/pg_duckdb/pull/982
[#935]: https://github.com/duckdb/pg_duckdb/pull/935
[#959]: https://github.com/duckdb/pg_duckdb/pull/959
[#936]: https://github.com/duckdb/pg_duckdb/pull/936
[#981]: https://github.com/duckdb/pg_duckdb/pull/981
[#979]: https://github.com/duckdb/pg_duckdb/pull/979
[#937]: https://github.com/duckdb/pg_duckdb/pull/937
[#920]: https://github.com/duckdb/pg_duckdb/pull/920
[#921]: https://github.com/duckdb/pg_duckdb/pull/921
[#932]: https://github.com/duckdb/pg_duckdb/pull/932
[#984]: https://github.com/duckdb/pg_duckdb/pull/984
[#989]: https://github.com/duckdb/pg_duckdb/pull/989
[#990]: https://github.com/duckdb/pg_duckdb/pull/990

# 1.0.0 (2025-09-04)

## Added

- Add support for statically compiling the duckdb library into the pg_duckdb extension. ([#618])
- Add support for `DOMAIN`, `VARINT`, `TIME`, `TIMETZ`, `BIT`, `VARBIT`, `UNION`, `MAP`, `STRUCT` types. ([#532], [#626], [#627], [#628], [#636], [#678], [#689], [#669])
- Add support for installing community extensions. ([#647])
- Add support for DDL on DuckDB tables in transactions. ([#632])
- Make `duckdb.unresolved_type` support `min`, `date_trunc`, `length`, `regexp_replace`, `LIKE`, `ILIKE`, `SIMILAR TO`. ([#643])
- Add cast from `duckdb.unresolved_type` to `bytea` and `text`. ([#643], [#915])
- Add support for the DuckDB date/time functions: `strftime`, `strptime`, `epoch`, `epoch_ms`, `epoch_us`, `epoch_ns`, `time_bucket`, `make_timestamp`, `make_timestamptz`. ([#643])
- Add support for using MotherDuck in multiple Postgres databases. ([#544], [#545])
- Add ALTER TABLE support for DuckDB tables. ([#652])
- Add support to `COPY ... TO` and `COPY ... FROM` for DuckDB tables. ([#665])
- Add support for `EXPLAIN (FORMAT JSON)` for DuckDB queries. ([#654])
- Add support for single dimension `ARRAY` types from DuckDB, before only `LIST` was supported. ([#655])
- Add support for `TABLESAMPLE`. ([#559])
- Add `duckdb.extension_directory`, `duckdb.temporary_directory` and `duckdb.max_temporary_directory_size` settings. ([#704])
- Add source locations to error messages. ([#758])
- Add basic collation support by allowing users to configure `duckdb.default_collation`. ([#814])
- Add support for converting Postgres table values in parallel. This can speed up large scans a lot. ([#762])
- Add support for MotherDuck views. You can now create views inside MotherDuck and query views that are already stored in MotherDuck. ([#822])
- Add support for Postgres 18 Release Candidate 1. Since Postgres 18 has not had a final release yet, this is still considered an experimental feature. ([#788])
- Add support for UUIDs in prepared statement arguments. ([#863])
- Add `duckdb.azure_transport_option_type` setting to configure Azure extension transport options, which can be used to workaround [issue #882](https://github.com/duckdb/pg_duckdb/issues/882). ([#910])

## Changed

- Update to DuckDB 1.3.2. ([#754], [#858])
- Change the way MotherDuck is configured. It's not done anymore through the Postgres configuration file. Instead, you should now enable MotherDuck using `CALL duckdb.enable_motherduck(...)` or equivalent `CREATE SERVER` and `CREATE USER MAPPING` commands. ([#668])
- Change the way secrets are added to DuckDB. You'll need to recreate your secrets using the new method `duckdb.create_simple_secret` or `duckdb.create_azure_secret` functions. Internally secrets are now stored `SERVER` and `USER MAPPING` for the `duckdb` foreign data wrapper. ([#697])
- Disallow DuckDB execution inside functions by default. This feature can cause crashes in rare cases and is intended to be re-enabled in a future release. For now you can use `duckdb.unsafe_allow_execution_inside_functions` to allow functions anyway. ([#764], [#884])
- Don't convert Postgres NUMERICs with a precision that's unsupported in DuckDB to double by default. Instead it will throw an error. If you want the lossy conversion to DOUBLE to happen, you can enable `duckdb.convert_unsupported_numeric_to_double`. ([#795])
- Remove custom HTTP caching logic. ([#644])
- When creating a table in a `ddb$` schema that table now uses the `duckdb` table access method by default. ([#650])
- Do not allow creating non-`duckdb` tables in a `ddb$` schema. ([#650])
- When creating MotherDuck tables from Postgres, automatically make them be created by the table creation. Before you had to set the ROLE manually before issuing the CREATE TABLE command. ([#650])
- Add automated tests for MotherDuck integration. ([#649])
- Sync the Postgres timezone to DuckDB when initializing the DuckDB connection. This makes some date parsing/formatting behave better. ([#643], [#853])
- Support `FORMAT JSON` for `COPY` commands. ([#665])
- Force `COPY` to use DuckDB execution when using `duckdb.force_execution`. ([#665])
- Automatically use DuckDB execution for COPY when file extensions are used for filetypes that DuckDB understands (`.parquet`, `.json`, `.ndjson`, `jsonl`, `.gz`, `.zst`). ([#665])
- Automatically use DuckDB execution for `COPY` when copying from Azure and HTTP locations. ([#872])
- Return `TEXT` columns instead of `VARCHAR` columns when using DuckDB execution. ([#583])
- Extensions in `duckdb.extensions` now get automatically installed before running any DuckDB query if `duckdb.autoinstall_known_extensions` is set to `true`. This helps with read-replica setups, where the extension gets installed on the primary and but the replica is queried. ([#801])
- By default `duckdb.disabled_filesystems` is now empty. To keep the default installation secure, `LocalFileSystem` will now be appended for any user that does not have the `pg_read_server_files` and `pg_write_server_files` privileges. ([#802])
- Push down `LIKE` expressions and `upper()`/`lower()` calls to Postgres storage. These expressions can sometimes be pushed down to the index. ([#808])
- Changed `duckdb.max_memory`/`duckdb.memory_limit` to accept integer values instead of a string, to avoid users entering values that DuckDB does not understand. This breaks backwards compatibility slightly: `MiB`, `GiB` etc suffixes are now not supported anymore,  only `MB`, `GB` etc suffixes are now allowed. ([#883])
- Add support for sub-extensions. This allows other Postgres extensions to build on top of pg_duckdb. ([#893])

## Fixed

- Fix possible crash when querying two Postgres tables in the same query. ([#604])
- Fix crash when loading the `postgres` extension for DuckDB  (a.k.a. postgres_scanner) into pg_duckdb ([#607])
- Do not set the `max_memory` in Postgres if `duckdb.max_memory`/`duckdb.memory_limit` is set to the empty string. ([#614])
- Handle PG columns with arrays with 0 dimensions correctly. We now assume such an array has a single dimension. ([#616])
- Fix valgrind issue in `DatumToString`. ([#639])
- Fix read of uninitialized memory when using DuckDB functions. ([#638])
- Fix escaping of MotherDuck schema names when syncing them. ([#650])
- Fix crash that could happen when EXPLAINing a prepared statement in certain cases. ([#660])
- Fix memory leak that could happen on query failure. ([#663])
- Add boundary checks when converting DuckDB date/timestamps to PG date/timestamps. DuckDB and Postgres don't support the exact same range of dates/timestamps, so now pg_duckdb only supports the intersection of these two ranges. ([#653])
- Fail nicely when syncing MotherDuck tables result in too long names being synced. ([#680]) TODO: FIX FOR TABLES CURRENTLY ONLY DONE FOR SCHEMAS
- Disallow installing `pg_duckdb` in databases with different encoding than `UTF8`. ([#703])
- Fix crashes or data corruption that could occur when using `CREATE TABLE AS` and materialized views if DuckDB execution and Postgres execution did not agree on the types that a query would return. ([#706])
- Fix various issues when using functions that returned `duckdb.row` (like `read_csv` & `read_parquet`) in a CTE. ([#718])
- Fix a crash when using a `CREATE TABLE AS` statement in a `plpgqsl` function ([#735])
- Throw error when trying to change DuckDB settings after the DuckDB connection has been initialized. ([#743])
- Fix crash for CREATE TABLE ... AS EXECUTE ([#757])
- Handle issues when DuckDB query would return different types between planning and execution phase. ([#759])
- Disallow DuckDB tables as a partition. This wasn't supported, and would fail in weird ways when attempted. Now a clear error is thrown. ([#778])
- Fix memory leak when reading LIST/JSON/JSONB columns from Postgres tables. ([#784])
- Fix dropping the Postgres side of a MotherDuck table, when the table does not exist anymore in MotherDuck. ([#784])
- Don't show hints about misuse of functions that return `duckdb.row` for queries that don't use those those functions. ([#811])
- Fix LIKE expressions involving a backslash (`\`) or `LIKE ... ESCAPE` expressions. ([#815])
- Fix errors for transactions that use `SET TRANSACTION ISOLATION`. ([#834])
- Fix compatibility issue with TimescaleDB extension. ([#846])
- Fix potential infinite loop during query cancelation ([#875])
- Fix do not allow relative path in DuckDB COPY statements. This is to provide the same protections as vanilla Postgres, so users don't accidentally overwrite database files. ([#827])
- Fix crashes involving postgres tables, by always materializing the entire DuckDB result set if the query involves Postgres tables. ([#877])

[#618]: https://github.com/duckdb/pg_duckdb/pull/618
[#532]: https://github.com/duckdb/pg_duckdb/pull/532
[#626]: https://github.com/duckdb/pg_duckdb/pull/626
[#627]: https://github.com/duckdb/pg_duckdb/pull/627
[#628]: https://github.com/duckdb/pg_duckdb/pull/628
[#636]: https://github.com/duckdb/pg_duckdb/pull/636
[#678]: https://github.com/duckdb/pg_duckdb/pull/678
[#689]: https://github.com/duckdb/pg_duckdb/pull/689
[#669]: https://github.com/duckdb/pg_duckdb/pull/669
[#647]: https://github.com/duckdb/pg_duckdb/pull/647
[#632]: https://github.com/duckdb/pg_duckdb/pull/632
[#643]: https://github.com/duckdb/pg_duckdb/pull/643
[#544]: https://github.com/duckdb/pg_duckdb/pull/544
[#545]: https://github.com/duckdb/pg_duckdb/pull/545
[#652]: https://github.com/duckdb/pg_duckdb/pull/652
[#665]: https://github.com/duckdb/pg_duckdb/pull/665
[#654]: https://github.com/duckdb/pg_duckdb/pull/654
[#655]: https://github.com/duckdb/pg_duckdb/pull/655
[#559]: https://github.com/duckdb/pg_duckdb/pull/559
[#704]: https://github.com/duckdb/pg_duckdb/pull/704
[#758]: https://github.com/duckdb/pg_duckdb/pull/758
[#814]: https://github.com/duckdb/pg_duckdb/pull/814
[#762]: https://github.com/duckdb/pg_duckdb/pull/762
[#822]: https://github.com/duckdb/pg_duckdb/pull/822
[#788]: https://github.com/duckdb/pg_duckdb/pull/788
[#754]: https://github.com/duckdb/pg_duckdb/pull/754
[#858]: https://github.com/duckdb/pg_duckdb/pull/858
[#668]: https://github.com/duckdb/pg_duckdb/pull/668
[#697]: https://github.com/duckdb/pg_duckdb/pull/697
[#764]: https://github.com/duckdb/pg_duckdb/pull/764
[#884]: https://github.com/duckdb/pg_duckdb/pull/884
[#795]: https://github.com/duckdb/pg_duckdb/pull/795
[#644]: https://github.com/duckdb/pg_duckdb/pull/644
[#650]: https://github.com/duckdb/pg_duckdb/pull/650
[#649]: https://github.com/duckdb/pg_duckdb/pull/649
[#853]: https://github.com/duckdb/pg_duckdb/pull/853
[#583]: https://github.com/duckdb/pg_duckdb/pull/583
[#801]: https://github.com/duckdb/pg_duckdb/pull/801
[#802]: https://github.com/duckdb/pg_duckdb/pull/802
[#808]: https://github.com/duckdb/pg_duckdb/pull/808
[#883]: https://github.com/duckdb/pg_duckdb/pull/883
[#893]: https://github.com/duckdb/pg_duckdb/pull/893
[#604]: https://github.com/duckdb/pg_duckdb/pull/604
[#607]: https://github.com/duckdb/pg_duckdb/pull/607
[#614]: https://github.com/duckdb/pg_duckdb/pull/614
[#616]: https://github.com/duckdb/pg_duckdb/pull/616
[#639]: https://github.com/duckdb/pg_duckdb/pull/639
[#638]: https://github.com/duckdb/pg_duckdb/pull/638
[#660]: https://github.com/duckdb/pg_duckdb/pull/660
[#663]: https://github.com/duckdb/pg_duckdb/pull/663
[#653]: https://github.com/duckdb/pg_duckdb/pull/653
[#680]: https://github.com/duckdb/pg_duckdb/pull/680
[#703]: https://github.com/duckdb/pg_duckdb/pull/703
[#706]: https://github.com/duckdb/pg_duckdb/pull/706
[#718]: https://github.com/duckdb/pg_duckdb/pull/718
[#735]: https://github.com/duckdb/pg_duckdb/pull/735
[#743]: https://github.com/duckdb/pg_duckdb/pull/743
[#757]: https://github.com/duckdb/pg_duckdb/pull/757
[#759]: https://github.com/duckdb/pg_duckdb/pull/759
[#778]: https://github.com/duckdb/pg_duckdb/pull/778
[#784]: https://github.com/duckdb/pg_duckdb/pull/784
[#811]: https://github.com/duckdb/pg_duckdb/pull/811
[#815]: https://github.com/duckdb/pg_duckdb/pull/815
[#834]: https://github.com/duckdb/pg_duckdb/pull/834
[#846]: https://github.com/duckdb/pg_duckdb/pull/846
[#875]: https://github.com/duckdb/pg_duckdb/pull/875
[#827]: https://github.com/duckdb/pg_duckdb/pull/827
[#872]: https://github.com/duckdb/pg_duckdb/pull/872
[#877]: https://github.com/duckdb/pg_duckdb/pull/877
[#863]: https://github.com/duckdb/pg_duckdb/pull/863
[#910]: https://github.com/duckdb/pg_duckdb/pull/910
[#915]: https://github.com/duckdb/pg_duckdb/pull/915

# 0.3.1 (2025-02-13)

## Fixed

- Fixed CI so docker images are built and pushed to Docker Hub for tags. ([#589])

[#589]: https://github.com/duckdb/pg_duckdb/pull/589

# 0.3.0 (2025-02-13)

## Added

- Support using Postgres indexes and reading from partitioned tables. ([#477])
- The `AS (id bigint, name text)` syntax is no longer supported when using `read_parquet`, `iceberg_scan`, etc. The new syntax is as follows: ([#531])

  ```sql
  SELECT * FROM read_parquet('file.parquet');
  SELECT r['id'], r['name'] FROM read_parquet('file.parquet') r WHERE r['age'] > 21;
  ```

- Add a `duckdb.query` function which allows using DuckDB query syntax in Postgres. ([#531])
- Support the `approx_count_distinct` DuckDB aggregate. ([#499])
- Support the `bytea` (aka blob), `uhugeint`,`jsonb`, `timestamp_ns`, `timestamp_ms`, `timestamp_s` & `interval` types. ([#511], [#525], [#513], [#534], [(#573)])
- Support DuckDB [json functions and aggregates](https://duckdb.org/docs/data/json/json_functions.html). ([#546])
- Add support for the `duckdb.allow_community_extensions` setting.
- We have an official logo! 🎉 ([#575])

## Changed

- Update to DuckDB 1.2.0. ([#548])
- Allow executing `duckdb.raw_query`, `duckdb.cache_info`, `duckdb.cache_delete` and `duckdb.recycle_ddb` as non-superusers. ([#572])
- Only sync MotherDuck catalogs when there is DuckDB query activity. ([#582])

## Fixed

- Correctly parse parameter lists in `COPY` commands. This allows using `PARTITION_BY` as one of the `COPY` options. ([#465])
- Correctly read cache metadata for files larger than 4GB. ([#494])
- Fix bug in parameter handling for prepared statements and PL/pgSQL functions. ([#491])
- Fix comparisons and operators on the `timestamp with timezone` field by enabling DuckDB its `icu` extension by default. ([#512])
- Allow using `read_parquet` functions when not using superuser privileges. ([#550])
- Fix some case insensitivity issues when reading from Postgres tables. ([#563])
- Fix case where cancel requests (e.g. triggered by pressing Ctrl+C in `psql`) would be ignored ([#548], [#584], [#587])

[#477]: https://github.com/duckdb/pg_duckdb/pull/477
[#531]: https://github.com/duckdb/pg_duckdb/pull/531
[#499]: https://github.com/duckdb/pg_duckdb/pull/499
[#511]: https://github.com/duckdb/pg_duckdb/pull/511
[#525]: https://github.com/duckdb/pg_duckdb/pull/525
[#513]: https://github.com/duckdb/pg_duckdb/pull/513
[#534]: https://github.com/duckdb/pg_duckdb/pull/534
[#573]: https://github.com/duckdb/pg_duckdb/pull/573
[#546]: https://github.com/duckdb/pg_duckdb/pull/546
[#575]: https://github.com/duckdb/pg_duckdb/pull/575
[#548]: https://github.com/duckdb/pg_duckdb/pull/548
[#572]: https://github.com/duckdb/pg_duckdb/pull/572
[#582]: https://github.com/duckdb/pg_duckdb/pull/582
[#465]: https://github.com/duckdb/pg_duckdb/pull/465
[#494]: https://github.com/duckdb/pg_duckdb/pull/494
[#491]: https://github.com/duckdb/pg_duckdb/pull/491
[#512]: https://github.com/duckdb/pg_duckdb/pull/512
[#550]: https://github.com/duckdb/pg_duckdb/pull/550
[#563]: https://github.com/duckdb/pg_duckdb/pull/563
[#584]: https://github.com/duckdb/pg_duckdb/pull/584
[#587]: https://github.com/duckdb/pg_duckdb/pull/587

# 0.2.0 (2024-12-10)

## Added

- Support for reading Delta Lake storage using the `duckdb.delta_scan(...)` function. ([#403])
- Support for reading JSON using the `duckdb.read_json(...)` function. ([#405])
- Support for multi-statement transactions. ([#433])
- Support reading from Azure Blob storage. ([#478])
- Support many more array types, such as `float` , `numeric` and `uuid` arrays. ([#282])
- Support for PostgreSQL 14. ([#397])
- Manage cached files using the `duckdb.cache_info()` and `duckdb.cache_delete()` functions. ([#434])
- Add `scope` column to `duckdb.secrets` table. ([#461])
- Allow configuring the default MotherDuck database using the `duckdb.motherduck_default_database` setting. ([#470])
- Automatically install and load known DuckDB extensions when queries use them. So, `duckdb.install_extension()` is usually not necessary anymore. ([#484])

## Changed

- Improve performance of heap reading. ([#366])
- Bump DuckDB version to 1.1.3. ([#400])

## Fixed

- Throw a clear error when reading partitioned tables (reading from partitioned tables is not supported yet). ([#412])
- Fixed crash when using `CREATE SCHEMA AUTHORIZATION`. ([#423])
- Fix queries inserting into DuckDB tables with `DEFAULT` values. ([#448])
- Fixed assertion failure involving recursive CTEs. ([#436])
- Only allow setting `duckdb.motherduck_postgres_database` in `postgresql.conf`. ([#476])
- Much better separation between C and C++ code, to avoid memory leaks and crashes (many PRs).

[#403]: https://github.com/duckdb/pg_duckdb/pull/403
[#405]: https://github.com/duckdb/pg_duckdb/pull/405
[#433]: https://github.com/duckdb/pg_duckdb/pull/433
[#478]: https://github.com/duckdb/pg_duckdb/pull/478
[#282]: https://github.com/duckdb/pg_duckdb/pull/282
[#397]: https://github.com/duckdb/pg_duckdb/pull/397
[#434]: https://github.com/duckdb/pg_duckdb/pull/434
[#461]: https://github.com/duckdb/pg_duckdb/pull/461
[#470]: https://github.com/duckdb/pg_duckdb/pull/470
[#366]: https://github.com/duckdb/pg_duckdb/pull/366
[#400]: https://github.com/duckdb/pg_duckdb/pull/400
[#412]: https://github.com/duckdb/pg_duckdb/pull/412
[#423]: https://github.com/duckdb/pg_duckdb/pull/423
[#448]: https://github.com/duckdb/pg_duckdb/pull/448
[#436]: https://github.com/duckdb/pg_duckdb/pull/436
[#476]: https://github.com/duckdb/pg_duckdb/pull/476
[#484]: https://github.com/duckdb/pg_duckdb/pull/484

# 0.1.0 (2024-10-24)

Initial release.
