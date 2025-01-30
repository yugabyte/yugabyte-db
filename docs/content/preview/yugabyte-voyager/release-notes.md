---
title: What's new in YugabyteDB Voyager
linkTitle: What's new
description: YugabyteDB Voyager release notes.
headcontent: New features, key enhancements, and bug fixes
menu:
  preview_yugabyte-voyager:
    identifier: release-notes
    parent: yugabytedb-voyager
    weight: 106
type: docs
---

What follows are the release notes for the YugabyteDB Voyager v1 release series. Content will be added as new notable features and changes are available in the patch releases of the YugabyteDB v1 series.

## v1.8.10 - January 28, 2025

### Enhancements

- **Air-gapped Installation:** Improved air-gapped installation to no longer require `gcc` on the client machine running `yb-voyager`.
- **Enhanced Assessment and Schema Analysis reports:**
  - Enhanced the **Assessment Report** with a single section summarizing all issues in a table. Each issue includes a summary and an expandable section for more details. You can now sort issues based on criteria such as category, type, or impact.
  - Detects the following unsupported features from PostgreSQL -
    - SQL Body in Create Function
    - Common table expressions (WITH queries) that have MATERIALIZED clause.
    - Non-decimal integer literals
  - When running assess-migration/analyze-schema against YugabyteDB {{<release "2.25.0.0">}} and later, the following issues are no longer reported, as they are fixed:
    - Stored generated columns
    - Before Row triggers on partitioned tables
    - Multi-range datatypes
    - Security invoker issues
    - Deterministic attribute with COLLATION
    - Foreign key references to partitioned tables
    - SQL body in function
    - Unique Nulls Not distinct
    - Regex functions
    - Range aggregate functions
    - JSONB subscripting
    - Copy FROM .. WHERE
    - CTE with Materialized clause
- **yugabyted control plane:** Enhanced the information sent to yugabyted for the migration assessment phase to also include the Migration Complexity Explanation.

### Bug fixes

- Fixed an [issue](https://github.com/yugabyte/yb-voyager/issues/2155) where data migration for the `hstore` datatype in live migration from source PostgreSQL was failing with errors and causing panics during the `import-data` step.
- Fixed an [issue](https://github.com/yugabyte/yb-voyager/issues/2200) in `import schema` where SQL bodies in `FUNCTION` DDL statements were not being parsed correctly.

## v1.8.9.1 - January 20, 2025

- Fixed a [regression](https://github.com/yugabyte/yb-voyager/issues/2204) introduced in v1.8.8, for password authentication in the `import data` command, where the command fails with error `failed to connect to target-host server error (FATAL: password authentication failed for user (SQLSTATE 28P01)`.

## v1.8.9 - January 14, 2025

### New features

- Implemented a new algorithm to determine migration complexity that accounts for all potential issues, including unsupported query constructs, PL/pgSQL objects, and incompatible data types.
- Introduced explanations of migration complexity in the PostgreSQL assessment report, summarizing high-impact issues and illustrating how the overall complexity level is determined.

### Enhancements

- Enhanced Assessment and Schema Analysis Reports to detect unsupported PostgreSQL features from PG 12 up to PG 17, including:
  - Regexp functions (regexp_count, regexp_instr, regexp_like).
  - Security Invoker Views.
  - JSON constructor and JSON Query functions.
  - IS_JSON predicate clauses (IS_JSON, IS JSON SCALAR, IS JSON OBJECT, IS JSON ARRAY).
  - Aggregate functions, such as anyvalue, range_agg, range_intersect_agg.
  - COPY command syntax, such as COPY FROM ... WHERE and COPY ... ON_ERROR.
  - Multirange datatypes, like int4multirange, int8multirange, datemultirange, and so on.
  - FETCH FIRST … WITH TIES subclause in SELECT statement.
  - Foreign Key referencing a partitioned table.
  - JSONB subscripting in DML, DDL, or PL/PGSQL.
  - UNIQUE NULLS NOT DISTINCT in CREATE/ALTER TABLE statement.
  - The deterministic attribute in CREATE COLLATION.
  - MERGE statements.

### Bug fixes

- Fixed an issue where import data failed for tables whose datafile paths exceeded 250 characters. The fix is backward compatible, allowing migrations started using older Voyager versions to continue seamlessly.
- Fixed an issue where logic for detecting unsupported PostgreSQL versions was giving false positives.

## v1.8.8 - December 24, 2024

### Enhancements

- Assessment and Schema Analysis Reports
  - You can now specify the target version of YugabyteDB when running [assess-migration](../reference/assess-migration/) and [analyze-schema](../reference/schema-migration/analyze-schema/). Specify the version using the flag `--target-db-version`. The default is the latest stable release (currently {{<yb-version version="stable">}}).
  - Assessment and schema analysis now detect and report the presence of advisory locks, XML functions, and system columns in DDLs.
  - Assessment and schema analysis now detect the presence of [large objects](https://www.postgresql.org/docs/current/largeobjects.html) (and their functions) in DDLs/DMLs.
  - In the Schema analysis report (html/text), changed the following field names to improve readability: Invalid Count to Objects with Issues; Total Count to Total Objects; and Valid Count to Objects without Issues. The logic determining when an object is considered to have issues or not has also been improved.
  - Stop reporting [Unlogged tables](../known-issues/postgresql/#unlogged-table-is-not-supported) as an issue in assessment and schema analysis reports by default, as UNLOGGED no longer results in a syntax error in YugabyteDB {{<release "2024.2.0.0">}}.
  - Stop reporting [ALTER PARTITIONED TABLE ADD PRIMARY KEY](https://github.com/yugabyte/yb-voyager/issues/612) as an issue in assessment and schema analysis reports, as [the issue](../known-issues/postgresql/#adding-primary-key-to-a-partitioned-table-results-in-an-error) has been fixed in YugabyteDB {{<release "2024.1.0.0">}} and later.
  - In the assessment report, only statements from `pg_stat_statements` that belong to the schemas provided by the user will be processed for detecting and reporting issues.
- Data Migration
  - `import data file` and `import data to source replica` now accept a new flag `truncate-tables` (in addition to `import data`), which, when used with `start-clean true`, truncates all the tables in the target/source-replica database before importing data into the tables.
- Miscellaneous
  - Enhanced guardrail checks in import-schema for YugabyteDB Aeon.

### Bug Fixes

- Skip Unsupported Query Constructs detection if `pg_stat_statements` is not loaded via `shared_preloaded_libraries`.
- Prevent Voyager from panicking/erroring out in case of `analyze-schema` and `import data` when `export-dir` is empty.

## v1.8.7 - December 10, 2024

### New Features

- Introduced a framework in the `assess-migration` and `analyze-schema` commands to accept the target database version (`--target-db-version` flag) as input and use it for reporting issues not supported in that target version for the source schema.

### Enhancements

- Improved permission grant script (`yb-voyager-pg-grant-migration-permissions.sql`) by internally detecting table owners, eliminating the need to specify the `original_owner_of_tables` flag.
- Enhanced reporting of "Unsupported Query Constructs" in the `assess-migration` command by filtering queries to include only those that match user-specified schemas, provided schema information is present in the query.
- Enhanced the `assess-migration` and `analyze-schema` commands to report issues in Functions or Procedures for variables declared with reference types (%TYPE) in the "Unsupported PL/pgSQL Objects" section.
- Added support to report DDL issues present in the PL/pgSQL blocks of objects listed in the "Unsupported PL/pgSQL Objects" section of the `assess-migration` and `analyze-schema` commands.
- Allow yb-voyager upgrades during migration from the recent breaking release (v1.8.5) to later versions.
- Modified the internal HTTP port to dynamically use an available free port instead of defaulting to 8080, avoiding conflicts with commonly used services.
- Added a guardrail check to the `assess-migration` command to verify that the `pg_stat_statements` extension is properly loaded in the source database.

### Bug fixes

- Fixed an [issue](https://github.com/yugabyte/yb-voyager/issues/1895) where NOT VALID constraints in the schema could cause constraint violation errors during data imports; these constraints are now created during the post-snapshot import phase.
- Fixed formatting issues in the assessment HTML report, where extra spaces or characters appeared after the "Unsupported PL/pgSQL Objects" heading, depending on the browser used for viewing.
- Fixed an [issue](https://github.com/yugabyte/yb-voyager/issues/1913) of segmentation faults when certain commands are executed before migration initialization.

## v1.8.6 - November 26, 2024

### New Features

- Unsupported PL/pgSQL objects detection. Migration assessment and schema analysis commands can now detect and report SQL features and constructs in PL/pgSQL objects in the source schema that are not supported by YugabyteDB. This includes detecting advisory locks, system columns, and XML functions. Voyager reports individual queries in these objects that contain unsupported constructs, such as queries in PL/pgSQL blocks for functions and procedures, or select statements in views and materialized views.

### Enhancements

- Using the arguments `--table-list` and `--exclude-table-list` in guardrails now checks for PostgreSQL export to determine which tables require permission checks.
- Added a check for Java as a dependency in guardrails for PostgreSQL export during live migration.
- Added check to verify if [pg_stat_statements](../../explore/ysql-language-features/pg-extensions/extension-pgstatstatements/) is in a schema not included in the specified `schema_list` and if the migration user has access to queries in the pg_stat_statements view. This is part of the guardrails for assess-migration for PostgreSQL.
- Introduced the `--version` flag in the voyager installer script, which can be used to specify the version to install.
- Added argument [--truncate-tables](../reference/data-migration/import-data/#arguments) to import data to target for truncating tables, applicable only when --start-clean is true.
- Added support in the assess-migration command to detect the `XMLTABLE()` function under unsupported query constructs.
- Added support for reporting unsupported indexes on some data types, such as daterange, int4range, int8range, tsrange, tstzrange, numrange, and interval, in analyze-schema and assess-migration.
- Added support for reporting unsupported primary and unique key constraints on various data types in assess-migration and analyze-schema.

### Bug fixes

- Fixed an [issue](https://github.com/yugabyte/yb-voyager/issues/1920) where export-data errors out if background metadata queries (count*) are still running after pg_dump completes.
- Fixed a bug where the assess-migration command fails when gathering metadata for unsupported query constructs if the pg_stat_statements extension was installed in a non-public schema.
- Fixed nil pointer exceptions and index-out-of-range issues when running export data status and get data-migration-report commands before export data is properly started.
- Fixed a bug in export data status command for accurate status reporting of partition tables during PostgreSQL data export.

## v1.8.5 - November 12, 2024

### Enhancements

- The guardrail checks to validate source/target database permissions, verify binary dependencies, and check database version compatibility for PostgreSQL in all voyager commands are now enabled by default.
- UI/UX improvements in the PostgreSQL permission grant script (`yb-voyager-pg-grant-migration-permissions.sql`) and new checks are added for replication slots, foreign keys, and triggers in PostgreSQL guardrails.
- Object names are scrollable in the analyze schema HTML report for improved navigation.
- Added constraint names and their corresponding table names when reporting unsupported features related to deferrable and exclusion constraints.
- Added reporting for the REFERENCING clause for triggers and BEFORE ROW triggers on partitioned tables in the analyze-schema and assess-migration reports.
- Added documentation links for unsupported query constructs in the assessment report.
- Standardized the format of data sent to the yugabyted control plane via the assess-migration command, ensuring consistent presentation across various sections of the report, such as Unsupported Features, Unsupported Datatypes, and Unsupported Query Constructs.

### Bug fixes

- Fixed the import-schema DDL parsing issue for functions and procedures, where extra spaces before the DDL caused it to be treated as normal DDL, preventing the PLPGSQL parsing logic from triggering.
- Fixed an issue which resulted in "token too long" errors in export-data-from-target when log level was set to DEBUG.

### Known issues

- The [assess-migration](../reference/assess-migration/) command will fail if the [pg_stat_statements](../../explore/ysql-language-features/pg-extensions/extension-pgstatstatements/) extension is created in a non-public schema, due to the "Unsupported Query Constructs" feature.
To bypass this issue, set the environment variable `REPORT_UNSUPPORTED_QUERY_CONSTRUCTS=false`, which disables the "Unsupported Query Constructs" feature and proceeds with the command execution.

## v1.8.4 - October 29, 2024

### New features

- Adaptive parallelism. Introduced the ability to dynamically adjust the number of parallel jobs in the [import data](../reference/data-migration/import-data/) command based on real-time CPU and memory use of the YugabyteDB cluster. This prevents resource under- and over-utilization, optimizes import speeds, and enhances efficiency. It also ensures stability across both snapshot and live migration phases without the need for manual intervention. Available in YugabyteDB versions {{<release "2.20.8.0">}}, v2024.1.4.0, v2024.2.0.0, or later.

- Detect unsupported query constructs. The [assess-migration](../reference/assess-migration/) command can now detect and report SQL queries containing unsupported features and constructs, such as advisory locks, system columns, and XML functions. This helps identify potential issues early in the migration process.

- Guardrails for YugabyteDB Voyager commands. Added checks to validate source/target database permissions, verify binary dependencies, and check database version compatibility for PostgreSQL in all [yb-voyager](../reference/yb-voyager-cli/) commands. This feature is currently disabled by default; to enable it, use the `--run-guardrails-checks` flag during assess-migration, export data, and import data phases.

### Enhancements

- Added support in [assess-migration](../reference/assess-migration/) and [analyze-schema](../reference/schema-migration/analyze-schema/) commands to report unsupported datatypes for live migration during fall-forward/fall-back, such as user-defined types, array of enums, and so on.
- Improved coverage of reporting PostGIS datatypes by including BOX2D, BOX3D, and TOPOGEOMETRY in the assess-migration and analyze-schema report.
- Included Voyager version details in assess-migration and analyze-schema reports.
- Added a confirmation prompt (yes/no) for start clean during data export, allowing users to confirm before proceeding.
- Enhanced export and import command output to display exported/imported table list by row count, with the largest tables first.

### Bug fixes

- Fixed [import data](../reference/data-migration/import-data/) and `import data file` commands to ensure batches created from data files don't exceed the default batch size limit (200MB), preventing "RPC message too long" errors with large rows. Also, added an immediate check to error out for single row size over 200MB.
- Fixed an issue in `analyze-schema` where partitioned tables were incorrectly reported as having insufficient columns in the primary key constraint due to a regex misidentifying the CONSTRAINT clause before primary key definition.
- Fixed the OPERATOR object names in the schema summary of `assess-migration` and `analyze-schema` reports, and renamed the INDEX, TRIGGER, and POLICY objects by appending the table name to the objects for unique identification (using the format `<object_name> ON <table_name>`).

## v1.8.3 - October 15, 2024

### Enhancements

- analyze-schema now reports the [unsupported datatypes](../known-issues/postgresql/#unsupported-datatypes-by-yugabytedb) by YugabyteDB (such as PostGIS types, geometry native types, and so on) and the [datatypes not supported during live migration](../known-issues/postgresql/#unsupported-datatypes-by-voyager-during-live-migration).
- The assessment report now reports the PostGIS datatypes (geometry and geography) under "Unsupported datatypes".
- The assess-migration command now reports the unsupported BRIN and SPGIST index methods from PostgreSQL source.
- Improved console output of the export schema command in case of MVIEW sizing recommendations.

### Bug fixes

- Fixed an issue in the streaming phase of the import data command that only occurred in the case of unique key conflict events. The producer [goroutine](https://go.dev/tour/concurrency/1) (which checks for conflicts, calculates the event's hash, and places them on a channel for consumption) was blocked because the channel was full and, after processing these events, the consumer goroutine couldn't clear the channel due to a lock held by the producer, causing a deadlock.
- Fixed a syntax error in the data import streaming phase caused by update events with a uuid column and a JSON column containing single quotes.

## v1.8.2 - October 1, 2024

### Enhancements

- The [assess-migration](../reference/assess-migration/) and [analyze schema](../reference/schema-migration/analyze-schema/) commands now report issues with indexes on UDT columns under the [Index on complex data types](../known-issues/postgresql/#indexes-on-some-complex-data-types-are-not-supported) PostgreSQL unsupported feature.
- Improved logic for cluster sizing recommendations in assessment reports:

  - Tables with more than two indexes will now be recommended for sharding.
  - Materialized views and their associated indexes are now considered when sizing clusters from a PostgreSQL source.

- The analyze-schema command now generates both HTML and JSON format reports by default unless the `output-format` flag is used, in which case only the specified format will be generated.
- Voyager's [diagnostic service](../reference/diagnostics-report/) (callhome) now uses a secure port for enhanced data security.
- The installer script now installs the `jq` package on all supported operating systems and verifies that the installed Java version is between 17 and 19 (inclusive).

### Bug fix

- Fixed an issue during the streaming phase of the import data command, where the process would hang on resumption if conflicts arose due to unique constraints between events.

## v1.8.1 - September 17, 2024

### Enhancements

- The installation methods (including RHEL, Ubuntu, Brew, and the installer script) now install PostgreSQL 16 instead of PostgreSQL 14.
- Added support in [analyze schema](../reference/schema-migration/analyze-schema/) and [assess-migration](../reference/assess-migration/) to report issues with indexes on columns with complex data types, such as INET, CITEXT, JSONB, TSVECTOR, TSQUERY, and ARRAYs. assess-migration categorizes these issues under the [Index on complex data types](../known-issues/postgresql/#indexes-on-some-complex-data-types-are-not-supported) PostgreSQL unsupported feature.
- Both [analyze schema](../reference/schema-migration/analyze-schema/) and [assess-migration](../reference/assess-migration/) now detect and report UNLOGGED tables, which are unsupported in YugabyteDB.
- Changed assessment report file names from "assessmentReport" to "migration_assessment_report", and "bulkAssessmentReport" to "bulk_assessment_report" .
- Improved UI output of [assess-migration](../reference/assess-migration/) by removing the progress bar from ora2pg when fetching the assessment report.

### Bug fixes

- Fixed an issue in streaming phase of [import data](../reference/data-migration/import-data/) where conflicts between events that could cause unique constraint errors were not being detected properly.
- Fixed an issue in [analyze schema](../reference/schema-migration/analyze-schema/) where multi-column GIN indexes were incorrectly reported as an issue due to a regex misidentifying expression indexes that combine two columns.
- Fixed an issue in [analyze schema](../reference/schema-migration/analyze-schema/) where table or column names containing the keyword "cluster" were incorrectly reported as issues of the type `ALTER TABLE CLUSTER ON`.
- Fixed an issue in [export-data-from-target](../reference/data-migration/export-data/#export-data-from-target) involving the new YugabyteDB CDC connector from Oracle sources.
- Fixed a schema registry issue in [import-data](../reference/data-migration/import-data/#import-data) where a "no such file or directory" error occurred.
- Fixed a bug where Voyager was unable to send details to YugabyteDB UI when upgrading from version 1.7.2 to 1.8 using the same yugabyted cluster.

## v1.8 - September 3, 2024

### New Features

- Introduced the notion of Migration complexity in assessment and analyze-schema reports, which range from LOW to MEDIUM to HIGH. For PostgreSQL source, this depends on the number and complexity of the PostgreSQL features present in the schema that are unsupported in YugabyteDB.
- Introduced a bulk assessment command (`assess-migration-bulk`) for Oracle which allows you to assess multiple schemas in one or more database instances simultaneously.
- Streamlined the airgapped installation process. This includes clearer instructions for installing dependencies; providing a script to verify dependencies are installed; and providing a bundle that includes all the Voyager packages and scripts.
- Support fall-forward/fall-back live migration workflows using the PostgreSQL replication protocol-based CDC connector.

### Enhancements

- Enhanced assessment and analyze reports to detect, and specify workarounds for PostgreSQL features that are incompatible with YugabyteDB. For example, Multi-column GIN indexes, GENERATED ALWAYS AS STORED columns, ALTER TABLE variants, Deferrable constraints, ALTER TABLE ADD PRIMARY KEY on a partitioned table, and so on.
- Enhanced [Manual Review Guidelines for PostgreSQL](../known-issues/postgresql/) source to list known issues that are unsupported by YugabyteDB, with workarounds.
- Enhanced export schema for categorizing the FOREIGN TABLE, POLICY, CONVERSION, and OPERATOR object types from PostgreSQL source.
- Improved cluster sizing recommendation logic in assessment reports.
  - Tables with more than 5 indexes will be recommended to be sharded.
  - Improved data load time estimates by also considering row counts, presence of indexes, number of columns, sharded/colocated properties, and so on.
- Added a Migration Caveats section to Migration Assessment Reports that mentions features that are not supported by Voyager or can cause issues during the migration process.
- Enhanced the information sent to the YugabyteD control plane to include source and target IP addresses, ports, version details, as well as Voyager client machine specifics like IP, OS, and remaining disk space.
- Added schema file path in the `failed.sql` file for all failed SQL statements in import schema.

### Bug fixes

- Do not send call-home diagnostics if proper `export-dir` is not provided.
- Validate Schema existence before assess-migration/export-schema/export-data.
- Fixed a bug where export-schema/export-data was failing on PG version 9 because `pg_sequences` table was not found.
- Fixed a bug in assessment report where certain data types (which were only unsupported in live migration workflows) were incorrectly being reported as unsupported in YugabyteDB.
- Fixed a bug in the assessment report where indexes on different tables with the same names were only listed once.
- Fixed a bug in analyze-schema where the presence of JSON_ARRAYAGG in an MVIEW definition was causing a nil pointer exception.
- Fixed a bug where unsupported data types were not being correctly detected in export-data-from-target.

## v1.7.2 - July 4, 2024

### New features

- Support for [Oracle Assess Migration Assessment](../migrate/assess-migration/) using assess-migration command.

### Enhancements

- In all migration workflows, indexes and triggers will now be imported to the target YugabyteDB by default during the `import-schema` step, as opposed to `import-schema --post-snapshot-import true`. This is because experiments have shown that pre-creating indexes and then importing data is faster than importing data and then backfilling the indexes.

- Modified and enhanced the call-home diagnostic functionality. More details are collected, such as source database size, migration type (offline/live), assessment report details (unsupported features/datatypes, source index/table statistics, and so on). Furthermore, call-home diagnostic details are also sent in bulk data import and live migration workflows.

- More details are being sent to [yugabyted](../../reference/configuration/yugabyted/) control plane from voyager. For example, source database size details (such as total database size, total index size, total table size, total table row count), assessment report target recommendations (total colocated size, total sharded size), analyze-schema issues, and so on.

- Enhanced the cluster sizing recommendation logic in the assessment report:

  - Minimum recommendation for node cores is now 4.
  - Improved recommendation logic based on experiments.

- Optimized execution time of [get data-migration-report](../reference/data-migration/import-data/#get-data-migration-report) by 90% for large databases.

### Bug fixes

- Fixed a bug where `import-schema --continue-on-error true` could miss listing certain failed statements in `failed.sql`.

## v1.7.1 - May 28, 2024

### Bug Fixes

- Fixed a bug where [export data](../reference/data-migration/export-data/) command ([live migration](../migrate/live-migrate/)) from Oracle source fails with a "table already exists" error, when stopped and re-run (resuming CDC phase of export-data).
- Fixed a known issue in the dockerized version of yb-voyager where commands [get data-migration-report](../reference/data-migration/import-data/#get-data-migration-report) and [end migration](../reference/end-migration/) did not work if you had previously passed ssl-cert/ssl-key/ssl-root-cert in [export data](../reference/data-migration/export-data/) or [import data](../reference/data-migration/import-data/) or [import data to source replica](../reference/data-migration/import-data/#import-data-to-source-replica) commands.

## v1.7 - May 16, 2024

### New features

- [Assess Migration](../migrate/assess-migration/) {{<tags/feature/tp>}} (for PostgreSQL source only): Introduced the Voyager Migration Assessment feature specifically designed to optimize the database migration process from various source databases, currently supporting PostgreSQL to YugabyteDB. Voyager conducts a thorough analysis of the source database by capturing essential metadata and metrics, and generates a comprehensive assessment report.
  - The report is created in HTML/JSON formats.
  - When [export schema](../reference/schema-migration/export-schema/) is run, voyager automatically modifies the CREATE TABLE DDLs to incorporate the recommendations.
  - Assessment can be done via plain bash/psql scripts for cases where source database connectivity is not available to the client machine running voyager.
- Support for [live migration](../migrate/live-migrate/) with the option to [fall-back](../migrate/live-fall-back/) for PostgreSQL source databases.
- Support for [live migration](../migrate/live-migrate/) of partitioned tables and multiple schemas from PostgreSQL source databases.
- Support for migration of case sensitive table/column names from PostgreSQL databases.
  - As a result, the table-list flags in [import data](../reference/data-migration/import-data/)/[export data](../reference/data-migration/export-data/) can accept table names in any form (case sensitive/insensitive/quoted/unquoted).

### Enhancements

- Detect and skip (with user confirmation) the unsupported data types before starting live migration from PostgreSQL databases.
- When migrating partitioned tables in PostgreSQL source databases, Voyager can now import data via the root table name. This makes it possible to change the names or partitioning logic of the leaf tables.

### Bug fixes

- Workaround for a bug in YugabyteDB where batched queries in a transaction were internally retried partially without respecting transaction/atomicity semantics.
- Fixed a bug in [export data](../reference/data-migration/export-data/) (from PostgreSQL source databases), where voyager was ignoring a partitioned table if only the root table name was specified in the `--table-list` argument.
- Fixed an issue Voyager was not dropping and recreating invalid indexes in case of restarts of 'post-snapshot-import' flow of import-schema.
- Fixed a bug in [analyze schema](../reference/schema-migration/analyze-schema/) that reports false-positive unsupported cases for "FETCH CURSOR".
- Changed the [datatype mapping](../reference/datatype-mapping-oracle/) of `DATE:date` to `DATE:timestamp` in Oracle to avoid time data loss for such columns.
- Increased maximum retry count of event batch to 50 for import data streaming.
- Fixed a bug where schema analysis report has an incorrect value for invalid count of objects in summary.

### Known issue

- If you use dockerized version of yb-voyager, commands [get data-migration-report](../reference/data-migration/import-data/#get-data-migration-report) and [end migration](../reference/end-migration/) do not work if you have previously passed ssl-cert/ssl-key/ssl-root-cert in [export data](../reference/data-migration/export-data/) or [import data](../reference/data-migration/import-data/) or [import data to source replica](../reference/data-migration/import-data/#import-data-to-source-replica) commands.

## v1.6.5 - February 13, 2024

### New features

- Support for [live migration](../migrate/live-migrate/) from PostgreSQL databases with the option of [fall-forward](../migrate/live-fall-forward/), using which you can switch to a source-replica PostgreSQL database if an issue arises during migration {{<tags/feature/tp>}}.

### Enhancements

- The live migration workflow has been optimized for [Importing indexes and triggers](../migrate/live-migrate/#import-indexes-and-triggers) on the target YugabyteDB. Instead of creating indexes on target after cutover, they can now be created concurrently with the CDC phase of `import-data-to-target`. This ensures that the time consuming task of creating indexes on the target YugabyteDB is completed before the cutover process.

- The `--post-import-data` flag of import schema has been renamed to `--post-snapshot-import` to incorporate live migration workflows.

- Enhanced [analyze schema](../reference/schema-migration/analyze-schema/) to report the unsupported extensions on YugabyteDB.

- Improved UX of `yb-voyager get data-migration-report` for large set of tables by adding pagination.

- The YugabyteDB Debezium connector version is upgraded to v1.9.5.y.33.2 to leverage support for precise decimal type handling with YugabyteDB versions 2.20.1.1 and later.

- Enhanced [export data status](../reference/data-migration/export-data/#export-data-status) command to report number of rows exported for each table in case of offline migration.

- Reduced default value of `--parallel-jobs` for import data to target YugabyteDB to 0.25 of total cores (from 0.5), to improve stability of target YugabyteDB.

### Bug fixes

- Fixed a bug in the CDC phase of [import data](../reference/data-migration/import-data/) where parallel ingestion of events with different primary keys having same unique keys was leading to unique constraint errors.

- Fixed an issue in [yb-voyager initiate cutover to target](../reference/cutover-archive/cutover/#cutover-to-target) where fallback intent is stored even if you decide to abort the process in the confirmation prompt.

- Fixed an issue in [yb-voyager end migration](../reference/end-migration/) where the source database is not cleaned up if `--save-migration-reports` flag is set to false.

- yb-voyager now gracefully shuts down all child processes on exit, to prevent orphan processes.

- Fixed a bug in live migration where "\r\n" in text data was silently converted to "\n". This was affecting snapshot phase of live migration as well as offline migration with BETA_FAST_DATA_EXPORT.

## v1.6.1 - December 14, 2023

### Bug fixes

- Fixed an issue that occurs in the CDC phase of live migration (including fall-back/fall-forward workflows), leading to transaction conflict errors or bad data in the worst case.
- Fixed an issue where end migration fails when using dockerized yb-voyager.
- Fixed an issue where export data from PostgreSQL fails when a single case-sensitive table name is provided to the `--table-list` argument.

## v1.6 - November 30, 2023

### New Features

- Live migration

  - Support for [live migration](../migrate/live-migrate/) from Oracle databases (with the option of [fall-back](../migrate/live-fall-back/)) {{<tags/feature/tp>}}, using which you can fall back to the original source database if an issue arises during live migration.

  - Various commands that are used in live migration workflows (including [fall-forward](../migrate/live-fall-forward/)) have been modified. YugabyteDB Voyager is transitioning from the use of the term "fall-forward database" to the more preferred "source-replica database" terminology. The following table includes the list of modified commands.

      | Old command | New command |
      | :---------- | :---------- |
      | yb-voyager fall-forward setup ... | yb-voyager import data to source-replica ... |
      | yb-voyager fall-forward synchronize ... | yb-voyager export data from target ... |
      | yb-voyager fall-forward switchover ... | yb-voyager initiate cutover to source-replica ... |
      | yb-voyager cutover initiate ... | yb-voyager initiate cutover to target ... |

  - A new command `yb-voyager get data-migration-report` has been added to display table-wise statistics during and post live migration.

- End migration

  A new command `yb-voyager end migration` has been added to complete migration by cleaning up metadata on all databases involved in migration, and backing up migration reports, schema, data, and log files.

### Enhancements

- Boolean arguments in yb-voyager commands have been standardized as string arguments for consistent CLI usage.
In all yb-voyager commands, there is no need to explicitly use `=` while setting boolean flags to false; a white-space would work (just like arguments of other types). As a side effect of this action, you cannot use boolean flag names without any value. For example, use `--send-diagnostics true` instead of `--send-diagnostics`. The boolean values can now be specified as `true/false, yes/no, 1/0`.
- For yb-voyager export/import data, the argument `--table-list` can now be provided via a file using the arguments `--table-list-file-path` or `exclude-table-list-file-path`. The table-list arguments now support glob wildcard characters `?` (matches one character) and `*` (matches zero or more characters). Furthermore, the `table-list` and `exclude-table-list` arguments can be used together in a command, which can be beneficial with glob support.
- Object types in `yb-voyager export schema` can now be filtered via the arguments `--object-type-list` or `--exclude-object-type-list`.
- In yb-voyager import-data, table names provided via any `table-list` argument are now by default, case-insensitive. To make it case-sensitive, enclose each name in double quotes.
- The `--verbose` argument has been removed from all yb-voyager commands.
- The `--delete` argument in `yb-voyager archive-changes` has been renamed to `--delete-changes-without-archiving`.
- `yb-voyager analyze-schema` now provides additional details in the report, indicating indices that don't get exported, such as reverse indexes, which are unsupported in YugabyteDB.

### Bug fix

Removed redundant ALTER COLUMN DDLs present in the exported schema for certain cases.

### Known issues

- Compared to earlier releases, Voyager v1.6 uses a different and incompatible structure to represent the import data state. As a result, Voyager v1.6 can't "continue" a data import operation that was started using Voyager v1.5 or earlier.

- If you are using [dockerized yb-voyager](../install-yb-voyager/#install-yb-voyager):

  - export schema and export data from Oracle database with SSL (via --oracle-tns-alias) fails. Use a non-docker version of yb-voyager to work around this limitation.

  - end migration command fails. This issue will be addressed in an upcoming release.

## v1.5 - September 11, 2023

### New feature

- Support for [live migration](../migrate/live-migrate/) from Oracle databases (with the option of [fall-forward](../migrate/live-fall-forward/)) {{<tags/feature/tp>}}.

Note that as the feature in Tech Preview, there are some known limitations. For details, refer to [Live migration limitations](../migrate/live-migrate/#limitations), and [Live migration with fall-forward limitations](../migrate/live-fall-forward/#limitations).

{{< youtube id="TJX7OlgPyUM" title="YugabyteDB Voyager 1.5 Demo" >}}

### Key enhancements

- The yb-voyager [export data](../reference/data-migration/export-data/) and [export schema](../reference/schema-migration/export-schema/) commands now support overriding the `pg_dump` arguments internally. The arguments are present at `/etc/yb-voyager/pg_dump-args.ini`. Any additions or modifications to this file will be honoured by yb-voyager.
- All yb-voyager commands that require a password now support providing passwords using environment variables such as `SOURCE_DB_PASSWORD` and `TARGET_DB_PASSWORD`. This addresses the security concern of a password being leaked via the `ps` command output. In addition, the password will not be present in any configuration or log files on the disk.

## v1.4 - June 30, 2023

### Key enhancements

- The `import data file` command now supports importing multiple files to the same table. Moreover, glob expressions can be provided in the `--file-table-map` argument to specify multiple files to be imported into the same table.

- In addition to AWS S3, `import data file` now supports directly importing objects (CSV/TEXT files) stored in GCS and Azure Blob Storage. You can specify GCS and Azure Blob Storage "directories" by prefixing them with `gs://` and `https://`.

- When using the [accelerated data export](../migrate/migrate-steps/#accelerate-data-export-for-mysql-and-oracle), Voyager can now connect to the source databases using SSL.

- The `analyze-schema` command now reports unsupported data types.

- The `--file-opts` CLI argument is now deprecated. Use the new [--escape-char](../reference/bulk-data-load/import-data-file/#arguments) and [--quote-char](../reference/bulk-data-load/import-data-file/#arguments) options.

### Bug fixes

- Fixed the issue where, if a CSV file had empty lines, `import data status` would continue reporting the import status as MIGRATING even though the import was completed and successful.

- yb-voyager now explicitly closes the source/target database connections when exiting.

- The `import data file` command now uses tab (\t) instead of comma (,) as the default delimiter when importing TEXT formatted files.

### Known issues

- Compared to earlier releases, Voyager v1.4 uses a different and incompatible structure to represent the import data state. As a result, Voyager v1.4 can't "continue" a data import operation that was started using Voyager v1.3 or earlier.

## v1.3 - May 30, 2023

### Key enhancements

- Export data for MySQL and Oracle is now 2-4x faster. To leverage this performance improvement, set the environment variable `BETA_FAST_DATA_EXPORT=1`. Most features, such as migrating partitioned tables, sequences, and so on, are supported in this mode. Refer to [Export data](../migrate/migrate-steps/#export-data) for more details.

- Added support for characters such as backspace(\b) in quote and escape character with [--file-opts](../reference/bulk-data-load/import-data-file/#arguments) in import data file.

- Added ability to specify null value string in import data file.

- During export data, yb-voyager can now explicitly inform you of any unsupported data types, and requests for permission to ignore them.

### Bug fixes

- yb-voyager can now parse `CREATE TABLE` statements that have complex check constraints.

- Import data file with AWS S3 now works when yb-voyager is installed via Docker.

## v1.2 - April 3, 2023

### Key enhancements

- When using the `import data file` command with the `--data-dir` option, you can provide an AWS S3 bucket as a path to the data directory.

- Added support for rotation of log files in a new logs directory found in `export-dir/logs`.

### Known issues

- [16658](https://github.com/yugabyte/yugabyte-db/issues/16658) The `import data file` command may not recognise the data directory being provided, causing the  step to fail for Dockerized yb-voyager.

## v1.1 - March 7, 2023

### Key enhancements

- When using the `import data file` command with CSV files, YB Voyager now supports any character as an escape character and a quote character in the `--file-opts` flag, such as single quote (`'`) as a `quote_char` and backslash (`\`) as an `escape_char`, and so on. Previously, YB Voyager only supported double quotes (`"`) as a quote character and an escape character.

- Creating the Orafce extension on the target database for Oracle migrations is now available by default.

- User creation for Oracle no longer requires `EXECUTE` permissions on `PROCEDURE`, `FUNCTION`, `PACKAGE`, and `PACKAGE BODY` objects.

- The precision and scale of numeric data types from Oracle are migrated to the target database.

- For PostgreSQL migrations, YB Voyager no longer uses a password in the `pg_dump` command running in the background. Instead, the password is internally set as an environment variable to be used by `pg_dump`.

- For any syntax error in the data file or CSV file, complete error details such as line number, column, and data are displayed in the output of the `import data` or `import data file` commands.

- For the `export data` command, the list of table names passed in the `--table-list` and `--exclude-table-list` are, by default, case insensitive. Enclose each name in double quotes to make it case-sensitive.

<!-- For the import data command,the behavior remains unchanged from the previous release. These names are, by default, case-sensitive. No need to enclose them in double-quotes. -->

- In the 1.0 release, the schema details in the report generated via `analyze-schema` are sent with diagnostics when the `--send-diagnostics` flag is on. These schema details are now removed before sending diagnostics.

- The object types which YB Voyager can't categorize are placed in a separate file as `uncategorized.sql`, and the information regarding this file is available as a note under the **Notes** section in the report generated via `analyze-schema`.

### Bug fixes

- [[765](https://github.com/yugabyte/yb-voyager/issues/765)] Fixed function parsing issue when the complete body of the function is in a single line.
- [[757](https://github.com/yugabyte/yb-voyager/issues/757)] Fixed the issue of migrating tables with names as reserved keywords in the target.
