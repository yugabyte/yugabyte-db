---
title: What's new in YugabyteDB Voyager
linkTitle: What's new
description: YugabyteDB Voyager release notes.
headcontent: New features, key enhancements, and bug fixes
menu:
  stable_yugabyte-voyager:
    identifier: release-notes
    parent: yugabytedb-voyager
    weight: 106
type: docs
---

What follows are the release notes for the YugabyteDB Voyager v1 release series. Content will be added as new notable features and changes are available in the patch releases of the YugabyteDB v1 series.

## Versioning

Voyager releases (starting with v2025.5.2) use the numbering format `YYYY.M.N`, where `YYYY` is the release year, `M` is the month, and `N` is the number of the release in that month.

## v2026.2.2 - February 17, 2026

### Enhancements

- TIMETZ (time with time zone) and pgvector columns are now flagged as unsupported for live migration in the assessment report, and the corresponding columns are automatically excluded from live migration.

### Bug fixes

- Fixed an issue where `export schema` could hang while fetching redundant index information during schema export.
- Fixed a potential data loss issue in live migration where events could be marked as processed before being durably written to disk.
- Corrected the default value of the `use-yb-grpc-connector` parameter in configuration templates from true to false.

## v2026.2.1 - February 3, 2026

{{< note title="Important: Breaking change" >}}

This release includes breaking changes for Voyager migrations. Migrations started with earlier Voyager versions cannot be continued with this version. To proceed, either continue the migration using the same Voyager version you started with, or start a new migration using v2026.2.1.

{{< /note >}}

### New feature

- Support for case-sensitive schema names in PostgreSQL migrations. PostgreSQL schemas that use quoted identifiers (for example, `"pg-schema"`, `"Schema"`) are now correctly handled in all migration workflows: assessment, export schema, export data, live migration, and also the grant-migration-permissions script. The `--source-db-schema` or `schema_list` parameter accepts schema names with or without quotes. Quoted names are preserved and matched correctly.

### Enhancement

- Voyager now automatically re-runs the internal assessment during export schema with the `--start-clean` flag if the `assess-migration` command was skipped. This ensures metadata remains in sync after a clean start and avoids stale assessment data.

## v2026.1.1 - January 20, 2026

### Enhancements

- Improved assessment validation. When replica endpoints are provided during a migration assessment, Voyager now validates that they belong to the same cluster as the primary by comparing the system identifier. Misconfigurations are detected and flagged early in the migration process.

- When importing snapshot data to the target YugabyteDB cluster, `import-data` now schedules sharded table imports in descending order of size (as opposed to a random order). This ensures that the import of larger tables starts sooner, avoiding the scenario where the largest tables are imported towards the end, which can cause an uneven load on the cluster and become a bottleneck during snapshot import.

### Bug fixes

- Fixed an issue in live migration (fall-back or fall-forward) where INTERVAL columns were not being handled correctly during value conversion, causing import-data to error out.

- Fixed an issue in live migration where prepared statement name collisions occurred when schema or table identifiers exceeded PostgreSQL's 63 character limit. The fix ensures distinct statement names are generated for different tables and operations, even those with long identifiers.

## v2025.12.2 - December 30, 2025

### Highlight

- Live migration for PostgreSQL source database (with fall-forward or fall-back using [YugabyteDB Connector](../../additional-features/change-data-capture/using-logical-replication/yugabytedb-connector/)) is {{<tags/feature/ga>}}.

### New features

- The assess-migration command now collects metadata from PostgreSQL primary and replica nodes in parallel, significantly improving assessment performance for multi-node deployments.
- Added support for User-Defined Types (UDTs) and Array types (including arrays of enums, hstore, and tsvector) with [YugabyteDB Connector](../../additional-features/change-data-capture/using-logical-replication/yugabytedb-connector/) in live migration with fall-back or fall-forward workflows.

### Enhancements

- Enhanced guardrail checks to validate all required permissions in the source database before starting export data. Missing permissions are now clearly reported with specific details, preventing migration failures due to insufficient privileges.
- Assessment topology in reports. The assessment report now includes topology information about primary and replica nodes assessed, providing better visibility into the scope of the assessment. This information is also sent with call-home diagnostics.
- Added a new `--primary-only` flag to the assess-migration command for PostgreSQL sources. When specified, the assessment skips read replica discovery and assessment, allowing you to assess only the primary database.
- Updated the default target YugabyteDB version to `2025.2.0.0`.
- SAVEPOINT usage detection in migration assessment. The assess-migration command now detects and reports SAVEPOINT usage (SAVEPOINT, ROLLBACK TO SAVEPOINT, and RELEASE SAVEPOINT) in source database transactions. This is important because YugabyteDB CDC has a known limitation where DML operations rolled back via ROLLBACK TO SAVEPOINT are incorrectly emitted as CDC events, which can cause data inconsistencies during fall-forward or fall-back workflows.

### Bug fixes

- Fixed an issue where resumption of live migration could fail for events larger than 20MB with the error string length exceeds the maximum length. The object mapper limits have been increased to 500MB to handle large-sized events during resumption.
- Fixed a performance issue with large BYTEA type columns during live migration streaming. The value converter now uses StringBuilder instead of string concatenation, improving performance when processing rows with large binary data.
- Fixed an issue with expression-based unique indexes that are present only on partitions and not on the root table. These indexes are now properly detected and handled, with events for such tables being executed sequentially to prevent conflicts.
- Fixed an issue with conflict detection cache for partitioned tables where a unique constraint or index is present on leaf partitions but not on the root table. The unique columns from all partitions are now correctly aggregated to the root table for proper conflict detection.
- Fixed handling of LTREE datatype in live migration streaming changes for all workflows (live migration, fall-forward, and fall-back).
- Fixed an issue where multi-range type columns (INT4MULTIRANGE, INT8MULTIRANGE, NUMMULTIRANGE, TSMULTIRANGE, TSTZMULTIRANGE, DATEMULTIRANGE) and custom range type columns caused errors during live migration. These column types are now appropriately skipped with a warning.

## v2025.12.1 - December 9, 2025

{{< note title="Important: Breaking change" >}}

This release introduces breaking changes for Voyager migrations. Migrations started with previous Voyager versions cannot continue with this version. To proceed, either continue the migration using an older Voyager version, or restart the migration using the new version.

{{< /note >}}

### Enhancements

- When importing data to YugabyteDB, batches are now ingested in random order. This helps avoid hotspots and ensures writes are distributed uniformly across cluster nodes, particularly when importing sequentially-ordered data into range-sharded tables.

- Auto-discovery of read replicas during assessment. When assessing the migration for PostgreSQL databases, physical read replicas are automatically detected and incorporated into the various aspects of assessment (sizing, detecting incompatibilities in queries, detecting object usage when reporting performance issues, comparing performance, and so on).

- Aggregate query statistics from all source nodes. The [compare-performance](../reference/compare-performance/) command now aggregates query statistics from all source database nodes (primary and replicas) when PostgreSQL read replicas are used. This provides a more comprehensive and accurate workload performance comparison by merging execution counts, total execution times, and preserving minimum or maximum execution times across all nodes.

### Bug fixes

- Unique partial index handling. Fixed an issue in live migration where ingesting events from tables having unique partial indexes could lead to unique index conflicts because of parallel processing of events.
- Expression-based unique index handling. Fixed an issue in live migration where tables having expression-based unique indexes could encounter conflicts when events were processed in parallel. Events for such tables are now executed sequentially to avoid potential unique key conflicts.

## v2025.11.2 - November 25, 2025

### New feature

- Assess migration now supports fetching metadata from PostgreSQL read replicas in addition to the primary source database using the replica endpoints passed in the `--source-read-replica-endpoints` argument. This provides a more comprehensive and accurate workload assessment by aggregating metrics across both primary and replica nodes.

### Enhancement

- Added ability to sort the Object Usage column in the Performance Optimizations table of the Assessment Report.

### Bug fixes

- Fixed a nil pointer error when a table was missing in the target database during live migration.
- Schema Optimization Report now excludes parent partitioned tables from "Colocated" recommendations, as colocation applies only to leaf partitions.
- Fixed an issue in the Schema Optimization Report where Colocation Recommendations were incorrectly marked as "Applied" when assess-migration could not provide sizing recommendations.
- Fixed an issue with sorting options in the "Assessment Issues" and "Performance Optimizations" sections of the Assessment Report.

## v2025.11.1 - November 11, 2025

### Enhancements

- Added an Object Usage column in the Performance optimization section of the assessment report to categorize objects involved in optimizations as FREQUENT, MODERATE, RARE, or UNUSED based on workload usage.

- Optimized per-table queries involved in startup (for export data from target) and post-processing (for import data to target), cutting database round trips from N (number of tables) to 1, reducing cutover-to-target time.

- The [YugabyteDB Logical Replication Connector](../../additional-features/change-data-capture/using-logical-replication/) is now the default for cutover to target in fall-forward and fall-back workflows. Set `--use-yb-grpc-connector=true` to use the gRPC connector instead.

- Removed unsupported datatype issues for hstore, tsvector, and array of enum datatypes from the assessment report, as these are now supported with the default YugabyteDB Logical Replication Connector.

- Enhanced export schema console output with improved UI/UX, including color-coded messages.

- Improved performance of sizing calculations in assess-migration.

### Bug fixes

- Fixed an issue where performance optimizations were being reported on both partitioned tables and their partitions, causing duplicate reports.
- Fixed an issue where `assess-migration` was not fetching read IOPS for tables without an index or primary key.

### Known issue

- Parent partitioned tables are correctly excluded from colocation recommendations during assessment, but are incorrectly listed under "Colocated" in the schema optimization report. This behavior will be made consistent across reports in a future release. (Issue [3173](https://github.com/yugabyte/yb-voyager/issues/3173))

## v2025.10.2 - October 28, 2025

### Enhancements

- Tables having the first column of primary key as timestamp/date are now configured to be range-sharded by default.
Use the [skip-performance-recommendations](../reference/schema-migration/export-schema/#arguments) flag to skip this automatic change.
- Added support for columns `tsvector`, `array of ENUMs` when the [YugabyteDB Connector](../../additional-features/change-data-capture/using-logical-replication/yugabytedb-connector/) is used in `export-data-from-target` in the fall-forward/fall-back workflows of live migration.

### Bug fix

- Fixed an issue where import-data fails if a table that has a sequence is part of the exclude-table-list, and is not created on the target YugabyteDB.

## v2025.10.1 - October 14, 2025

### New feature

- Export schema automatically exports all primary key constraints as hash-sharded for better distribution of data, and unique key constraints as range-sharded to avoid any potential hotspots.
Use the [skip-performance-recommendations](../reference/schema-migration/export-schema/#arguments) flag to skip this automatic change.

### Enhancements

- Added support for all supported YugabyteDB versions to the [compare-performance](../reference/compare-performance/) command.
- Enhanced the compare-performance HTML report for the "Slowdown ratio" and "Impact" of each query in the **All Queries** tab.
- Enhanced Oracle assessment report to mention the `--allow-oracle-clob-data-export` flag for the support of CLOB datatype export.

### Bug fixes

- Fixed import schema to properly handle session variables during connection retries, ensuring DDL state consistency.
- Fixed import data or import data file to allow users to run without the `--start-clean` flag, after creating missing tables following guardrail failures.
- Fixed a scenario where compare-performance fails to generate a JSON report if there are some entries in `pg_stat_statements` having zero calls.

## v2025.9.3 - September 30, 2025

### New feature

- {{<tags/feature/tp>}} Added the ability to analyze, compare, and summarize workload query performance between the source database and the target YugabyteDB database using the [compare-performance](../reference/compare-performance/) command. The command generates both HTML and JSON reports for easy query comparison.

  Note that this feature is supported for YugabyteDB release {{<release "2025.1">}} and later.

### Enhancements

- Improved import data to skip retrying errors that the `pgx` driver identifies as non‑retryable, enhancing stability.
- Added the ability to manage adaptive parallelism using the `--adaptive-parallelism` flag. Options include `disabled`, `balanced` (default), or `aggressive`. Replaces the `--enable-adaptive-parallelism` flag.
- Added an `--output-format` flag to the `export data status`, `import data status`, and `get data-migration-report` commands to generate structured JSON reports. `end migration` now saves the JSON versions of these reports as well.

### Bug fixes

- Fixed offline import data failing with accelerated data export mode when some tables are not created in the target database and excluded via table-list flags.
- Fixed an issue where data exported using older versions (before v2025.9.2) could not be imported after upgrading Voyager to v2025.9.2. Import data would previously fail with the error `failed to prepare table to column`.
- Fixed a nil pointer error when an unknown table name is included in the table list passed to the `import data` command.

## v2025.9.2 - September 16, 2025

### Enhancements

- Enhanced primary key recommendation logic to consider both unique constraints and unique indexes when suggesting primary keys, and added support for generating recommendations for partitioned tables that don't have primary keys.

- Enhanced assessment report:
  - Removed low cardinality performance optimization recommendation and updated descriptions for NULL and particular value column indexes to clarify unnecessary writes for these values.
  - Improved "Notes" section organization by categorizing them according to their types for better readability.
  - Added explanatory notes about redundant indexes in the sizing recommendation section to help users understand the impact on estimated data import time.
  - Renamed "Sharding Recommendations" to "Colocation Recommendations" for better clarity.
  - Enhanced the colocation recommendations by suggesting only to colocate tables if it provides an overall benefit in the required number of cores or nodes.
  - Removed a suggestion note to create range-sharded secondary indexes as they are now automatically created during export schema.

- Added console messages to show resumption progress when importing from large files, keeping users informed during long resumption processes.
- Introduced a flag [--max-retries-streaming](../reference/data-migration/import-data/#arguments) in the import data commands to configure the number of retries for the streaming phase in live migration.

### Bug fixes

- Fixed a bug in offline migration during import data. The command now correctly honors the table list allowing users to continue their migration by excluding tables that are not present in the target database.
- Fixed issue where rows skipped due to size or transformation errors were not being counted in the errored row count during data import, providing more accurate error statistics.

## v2025.9.1 - September 2, 2025

### New feature

- Export schema automatically exports all secondary indexes as range-sharded by default to avoid potential hash hotspots. Use the [skip-performance-recommendations](../reference/schema-migration/export-schema/#arguments) flag to skip this automatic change.

### Enhancements

- The assessment report now includes primary key recommendations for tables having no primary key but with UNIQUE NOT NULL columns, improving schema optimization guidance.

- Improved the estimated time for import calculations in the assessment report by additionally estimating the time without considering redundant indexes, which are now automatically excluded in the export schema step.

- End migration now backs up the schema optimization report produced by export schema.

- Modified flag names in export schema from "skip-performance-optimizations" to "skip-performance-recommendations" and from "skip-recommendations" to "skip-colocation-recommendations" for consistency and clarity.

### Bug fix

- Fixed a bug where the source database password was not correctly configured when passed to assess-migration when it was run automatically during a schema export.

## v2025.8.2 - August 19, 2025

### New feature

- Introduced the `--allow-oracle-clob-data-export` flag for the export data command, to enable exporting data from CLOB datatype columns in Oracle offline migrations.

### Enhancements

- Improved sizing calculations in `assess-migration` by factoring in throughput gains based on the recommended number of nodes for more accurate import time estimation.
- Enhanced PostgreSQL permissions grant script by adding an option for live migrations to either transfer table ownership to the migration user or grant the original owner's permissions to it.
- Improved import data retry logic to skip non-retryable errors such as data exceptions and integrity violations.
- Removed redundant index performance optimization reports from the `assess-migration` report as `export schema` now automatically removes redundant indexes.
- Enhanced schema optimization report in `export schema` to list all recommendations, whether they are applied or skipped, even when the `--skip-performance-recommendations` / `--skip-colocation-recommendations` flags are used.

### Bug fix

- Fixed an [issue](https://github.com/yugabyte/yb-voyager/issues/2968) where import schema fails while parsing some unnecessary statements on all Voyager installs done after August 14, 2025. Refer to [TA-2968](/stable/releases/techadvisories/ta-2968/).

## v2025.8.1 - August 5, 2025

### New feature

- Automatically apply performance optimizations recommended by the migration assessment (such as removing redundant indexes) during the export schema phase. Voyager also generates a schema optimization report detailing the optimizations that were applied. To turn automatic optimization off, set the new `--skip-performance-recommendations` flag in the `export schema` command to true.
- Introduced the ability to stash errors and continue when importing snapshot data in `import data` and `import-data-file` commands by using the flags `error-policy-snapshot` or `error-policy`.

### Enhancements

- Migration assessment now detects missing indexes on foreign‑key columns. This provides more comprehensive performance optimization recommendations in the migration assessment report.
- Updated the latest stable YugabyteDB version to v2025.1.0.0, ensuring compatibility with the newest features and fixes. Advisory locks are now supported in YugabyteDB and will no longer be reported from this target version onwards.
- Improved the performance of `import data` by increasing the max CPU threshold for adaptive parallelism to 80.
- Improved ANALYZE checks in the `assess-migration` command to honor auto‑analyze settings and gracefully handle user prompts.
- Improved error handling by throwing clear errors when connection to the source database fails, making troubleshooting easier.

### Bug fixes

- Addressed nil pointer exceptions in `end migration` when the database configuration is not set, preventing unexpected errors during migration processes.
- Fixed an issue where `import data` with `--on-primary-key-conflict ignore` failed if import to source replica started earlier.

## v2025.7.2 - July 15, 2025

### New feature

- Introduced the `--on-primary-key-conflict` flag for the import data to target and import data file command, supporting two modes:

  - ERROR: Fails the import if a primary key conflict is encountered.
  - IGNORE: Ignores rows that have a primary key conflict.

### Enhancements

- Foreign keys with mismatched datatypes are now detected and reported in the Migration Assessment Report under **Performance Optimizations**.
- Automatically clean up leftover metadata when `assess-migration` is aborted via a prompt, eliminating the need to rerun with `--start-clean`.
- Added a warning in import data when existing rows are detected in the target table to help prevent primary key conflicts during import.
- Improved error messages when multiple Debezium processes or active replication slots are detected, so that you can identify and kill orphaned processes and retry the command.
- Upgraded the [YugabyteDB gRPC Connector](/stable/additional-features/change-data-capture/using-yugabytedb-grpc-replication/debezium-connector-yugabytedb/) for live migration with fall-back and fall-forward workflows to support new YugabyteDB releases.

### Bug fixes

- Fixed incorrect error message shown during schema export failures; now correctly reports "schema export unsuccessful".
- Fixed import report output for import data file when the same file is mapped to multiple tables; all target tables are now correctly listed in the report.
- Fixed broken status reporting in import data status when multiple files are imported to the same table; all imported files are now listed in the status report.
- Fixed misclassification of performance optimization issues inside PL/pgSQL blocks; these are now correctly reported under **Performance Optimizations** in the Migration Assessment Report.

## v2025.7.1 - July 1, 2025

### New feature

- You can now use [configuration files](../reference/configuration-file/) with the [import-data-file](../reference/bulk-data-load/import-data-file/) command for [bulk data load](../migrate/bulk-data-load/) from files.

### Enhancements

- Added a check to prompt users to run ANALYZE on schemas if it hasn't been executed, ensuring more accurate performance optimization assessment.
- The assessment no longer reports partial indexes as redundant in performance optimizations.
- The assessment no longer reports any partial indexes filtering NULL values in the "Indexes with high percentage of NULL values" case, and filtering a particular value in the "Indexes with high percentage of a particular value" case.

### Bug fixes

- Fixed the `export data from source` and `export data from target` commands to prevent multiple internal processes from running concurrently in cases where a previous run may have left an orphaned process.
- Fixed a bug where DDLs containing `DEFAULT CURRENT_TIMESTAMP AT TIME ZONE` clauses were generated with a syntax error during export schema.

## v2025.6.2 - June 17, 2025

### New feature

- [Configuration files](../reference/configuration-file/) can now be used for all commands in live migration workflows.

### Enhancements

- The migration assessment report for the [performance optimization](../known-issues/postgresql/#performance-optimizations) "Hotspots with indexes on timestamp/date as first column" now includes reporting for primary keys and unique key indexes.
- Improved the readability of the HTML assessment report by moving performance optimizations to a dedicated section.
- Added a guardrail in export data live migration resumption scenarios for PostgreSQL source to prevent multiple export data streaming processes from running at the same time.

### Bug fixes

- Fixed an issue where Oracle Normal Indexes were incorrectly reported as unsupported in the schema analysis report.
- Fixed a bug in the `assess-migration` command that caused it to fail with an error in scenarios where the source database had a column of an array of any unsupported YugabyteDB datatype.
- Fixed a bug in the `import-data` command for a live migration scenario that prevented resuming `import-data` if a failure occurred after cutover was initiated.
- Fixed a bug in yugabyted UI where data migration progress was not shown/updated for tables with a large number of rows (exceeding `int32` range).

## v2025.6.1 - June 3, 2025

### New feature

- Added support for the [YugabyteDB Connector](../../additional-features/change-data-capture/using-logical-replication/yugabytedb-connector/) in live migration with fall-forward and fall-back workflows. The `cutover to target` command now includes a mandatory flag to specify whether to use [YugabyteDB gRPC Connector](../../additional-features/change-data-capture/using-yugabytedb-grpc-replication/debezium-connector-yugabytedb/) or [YugabyteDB Connector](../../additional-features/change-data-capture/using-logical-replication/yugabytedb-connector/) for these workflows.

  This is required for [YugabyteDB Aeon](/stable/yugabyte-cloud/) or restricted environments where only the YugabyteDB Connector is supported.

### Enhancements

- Issues in the assessment report are now displayed in sorted order by issue category.
- Voyager now exits cleanly when an invalid `CONTROL_PLANE_TYPE` is provided, avoiding stack traces on the console.
- Refined the warning message when a load balancer is detected during the import data phase.

### Bug fixes

- Fixed an issue where SSL mode `ALLOW` was supported but incorrectly flagged as invalid by import commands.

## v2025.5.2 - May 20, 2025

### New features

- Added support for using a [configuration file](../reference/configuration-file/) to manage parameters in offline migration using `yb-voyager`.

### Enhancements

- If you run `export schema` without first running `assess-migration`, Voyager will now automatically run assess the migration before exporting the schema for PostgreSQL source databases.
- Performance optimizations are now reported only in assessment reports, not in schema analysis reports.
- Assessment Report
  - The assessment report now includes detailed recommendations related to index design to help you identify potential uneven distribution or hotspot issues in YugabyteDB. This includes:
    - Indexes on low-cardinality columns (for example, `BOOLEAN` or `ENUM`)
    - Indexes on columns with a high percentage of `NULL` values
    - Indexes on columns with a high frequency of a particular value
- Import Data
  - The `import-data` command now monitors replication (CDC/xCluster) only for the target database specified in the migration. This avoids false positives caused by replication streams on other databases.

### Bug fixes

- Fixed an issue where left-padded zeros in PostgreSQL `BIT VARYING` columns were incorrectly omitted during live migration.

## v1.8.17 - May 6, 2025

### New feature

- New Command: `finalize-schema-post-data-import`
    This command is used to re-add NOT VALID constraints and refresh materialized views after import, and replaces the use of `import schema` with the `--post-snapshot-import true` and `--refresh-mviews` flags; both of these flags are now deprecated in import schema.

### Enhancements

- Sizing Recommendations in Assessment Reports
  - Improved the accuracy of the estimated data load time mentioned in the assessment report by incorporating the `target-db-version` specified during the `assess-migration` command.
  - Removed `Parallel jobs` recommendations as the Adaptive Parallelism feature now dynamically adjusts parallelism based on cluster load.
- Schema Recommendations in Assessment and Schema Analysis Reports
  - The assessment and schema analysis reports now include recommendations for performance optimization by identifying and suggesting the removal of redundant indexes present in the source schema.
- Improved Assessment HTML Report
  - The HTML report generated during the `assess-migration` command now features a cleaner, more user-friendly design for better readability and usability.

## v1.8.16 - April 22, 2025

### New features

- Regularly monitor the YugabyteDB cluster during data import to ensure good health and prevent suboptimal configurations.
  - If a YugabyteDB node goes down, the terminal UI notifies the user, and Voyager automatically shifts the load to the remaining nodes.
  - Voyager aborts the import process if replication (CDC/xCluster) is detected. Bulk data loads with replication enabled are not recommended, as they can significantly increase WAL file sizes. To bypass this check, use the `--skip-replication-checks` flag.
- Enhanced assessment and schema analysis reports now include schema change recommendations to optimize performance. Specifically, range-sharded indexes on timestamp columns that can cause hotspots are detected and reported with recommended workarounds.

### Enhancements

- Improved user experience in import schema in case of an error, by making the user aware of the `--continue-on-error` and `ignore-exist` flags.
- Added a prompt to the `yb-voyager-pg-grant-migration-permissions.sql` script for live migrations to notify users about the change in table ownership.

### Bug fixes

- Fixed a bug where certain data types supported in newer YugabyteDB versions (e.g., 2.25) were incorrectly flagged as issues in the assessment report.

## v1.8.15 - April 8, 2025

### Enhancements

- Improved layout for guardrails for the single table list in live migration, with better output for readability, especially for large table lists.
- Improved consistency in table list output by always showing fully qualified table names.
- Added `--pg-only`, `--oracle-only`, and `--mysql-only` flags to the airgapped installation script to check/install dependencies for a specific database.
- Enhanced the assessment report to provide more detailed information about each datatype under Unsupported datatypes.

### Bug fixes

- Fixed an issue in the fall back scenario where restoring sequences was failing due to missing permissions. The necessary SELECT, USAGE, and UPDATE permissions are now correctly granted in `yb-voyager-pg-grant-migration-permissions.sql`.

## v1.8.14 - March 25, 2025

### Enhancements

- Enhanced the import-data ingestion logic to limit the number of batches for colocated tables that can be ingested at a time to improve the performance.
- Modified the PostgreSQL migrations to only migrate the sequences attached or linked to the migrating tables.
- Enhanced the assessment issues for the category Unsupported PL/pgSQL objects to also report the suggestion in the description.
- Enhanced sizing recommendation logic in the assessment report for recommending more accurate vCPUs per node.

### Bug fixes

- Fixed an export-schema [failure](https://github.com/yugabyte/yb-voyager/issues/2402) for the Oracle source databases for certain cases.
- Fixed an [issue](https://github.com/yugabyte/yb-voyager/issues/2321) in the `import data` resumption process to correctly recognize an earlier initiated `cutover to target` .
- Fixed an [issue](https://github.com/yugabyte/yb-voyager/issues/2406) in `export data from source` / `export data from target` for live migration workflow when the tables that are not being migrated and existed in only one of the source or target databases.
- Fixed an [issue](https://github.com/yugabyte/yb-voyager/issues/2386) in `end-migration` where deleting the CDC stream ID on the target failed if the `ssl-root-cert` was only provided during `export data from target` phase.
- Fixed an import data [bug](https://github.com/yugabyte/yb-voyager/issues/2414) for the resumption scenario.

## v1.8.13 - March 11, 2025

### Enhancements

- Merged the ALTER TABLE ADD constraints DDL (Primary Key, Unique Key, and Check Constraints) with the CREATE TABLE statement, reducing the number of DDLs to analyze/review and improving overall import schema performance.
- Introduced a guardrails check to ensure live migration uses a single, fixed table list throughout the migration, preventing any changes to the table list after the migration has started.

### Bug fixes

- Fixed an issue where the `iops-capture-interval` flag in the assess-migration command did not honor the user-defined value and always defaulted to its preset.
- Fixed an issue in the IOPs calculation logic, ensuring it counts the number of scans (both sequential and index) instead of using `seq_tup_read` for read statistics.
- Fixed an issue causing a NumberFormatException during the cutover phase of live migration if a sequence was assigned to a non-integer column.
- Fixed an issue leading to a nil pointer exception when sending the diagnostics for import data to the source/source-replica.
- Fixed an issue where importing data to the target for a new migration (if CDC was enabled in a previous run) failed due to ALTER operations on metadata tables carrying over from the last migration.

## v1.8.12 - February 25, 2025

### Enhancements

- Added support for installing yb-voyager on RHEL 9.
- Installer script now installs the postgres-17-client package on Ubuntu instead of postgresql-17.
- Improved import-data snapshot performance by importing multiple tables at the same time.
- Enhanced the grant permissions script for PostgreSQL 15 and later to grant the SET permission on `session_replication_role` to source-db-user. This eliminates the need for you to disable Foreign Keys and triggers on the source database before running import-data-to-source.

## v1.8.11 - February 11, 2025

### ​​Enhancements

- Updated the Assessment and Schema Analysis reports to detect the following unsupported PostgreSQL features:
  - Listen / Notify events
  - Two-Phase Commit
  - Setting compression method with COMPRESSION clause in CREATE / ALTER TABLE
  - Create Database options for locale, collation, strategy, and OID-related settings
- Enhanced the JSON assessment report to include only the new assessment issue format, removing the old format that used separate fields for each issue category.
- The import data status command now reports tables where the import has not yet started, improving visibility for bulk imports (import data file).
- The assess-migration command now checks the source database IOPS and issues a warning if it is zero.

### Bug fixes

- Fixed the status reporting via import data status when resuming the import for tables from a CSV data file that includes a header row.
- Fixed the guardrail checks for live migration in Oracle and MySQL by removing the ora2pg dependency check.

## v1.8.10 - January 28, 2025

### Enhancements

- **Air-gapped Installation:** Improved air-gapped installation to no longer require `gcc` on the client machine running `yb-voyager`.
- **Enhanced Assessment and Schema Analysis reports:**
  - Enhanced the **Assessment Report** with a single section summarizing all issues in a table. Each issue includes a summary and an expandable section for more details. You can now sort issues based on criteria such as category, type, or impact.
  - Detects the following unsupported features from PostgreSQL -
    - SQL Body in Create Function
    - Common table expressions (WITH queries) that have MATERIALIZED clause.
    - Non-decimal integer literals
  - When running assess-migration/analyze-schema against YugabyteDB [v2.25.0.0](/stable/releases/ybdb-releases/end-of-life/v2.25/#v2.25.0.0) and later, the following issues are no longer reported, as they are fixed:
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

### Bug fixes

- Skip Unsupported Query Constructs detection if `pg_stat_statements` is not loaded via `shared_preloaded_libraries`.
- Prevent Voyager from panicking/erroring out in case of `analyze-schema` and `import data` when `export-dir` is empty.

## v1.8.7 - December 10, 2024

### New features

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

### New features

- Unsupported PL/pgSQL objects detection. Migration assessment and schema analysis commands can now detect and report SQL features and constructs in PL/pgSQL objects in the source schema that are not supported by YugabyteDB. This includes detecting advisory locks, system columns, and XML functions. Voyager reports individual queries in these objects that contain unsupported constructs, such as queries in PL/pgSQL blocks for functions and procedures, or select statements in views and materialized views.

### Enhancements

- Using the arguments `--table-list` and `--exclude-table-list` in guardrails now checks for PostgreSQL export to determine which tables require permission checks.
- Added a check for Java as a dependency in guardrails for PostgreSQL export during live migration.
- Added check to verify if [pg_stat_statements](../../additional-features/pg-extensions/extension-pgstatstatements/) is in a schema not included in the specified `schema_list` and if the migration user has access to queries in the pg_stat_statements view. This is part of the guardrails for assess-migration for PostgreSQL.
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

- The [assess-migration](../reference/assess-migration/) command will fail if the [pg_stat_statements](../../additional-features/pg-extensions/extension-pgstatstatements/) extension is created in a non-public schema, due to the "Unsupported Query Constructs" feature.
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

### New features

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

### Bug fixes

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

- The live migration workflow has been optimized for [Importing indexes and triggers](../migrate/live-migrate/#import-data-to-target) on the target YugabyteDB. Instead of creating indexes on target after cutover, they can now be created concurrently with the CDC phase of `import-data-to-target`. This ensures that the time consuming task of creating indexes on the target YugabyteDB is completed before the cutover process.

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

### New features

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
