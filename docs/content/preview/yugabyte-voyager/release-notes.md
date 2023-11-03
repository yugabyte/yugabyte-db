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

Included here are the release notes for the YugabyteDB Voyager v1 release series. Content will be added as new notable features and changes are available in the patch releases of the YugabyteDB v1 series.

## v1.5 - September 11, 2023

### New feature

* Support for [live migration](../migrate/live-migrate/) from Oracle databases (with the option of [fall-forward](../migrate/live-fall-forward/)) [Tech Preview].

Note that as the feature in Tech Preview, there are some known limitations. For details, refer to [Live migration limitations](../migrate/live-migrate/#limitations), and [Live migration with fall-forward limitations](../migrate/live-fall-forward/#limitations).

### Key enhancements

* The yb-voyager [export data](../reference/data-migration/export-data/) and [export schema](../reference/schema-migration/export-schema/) commands now support overriding the `pg_dump` arguments internally. The arguments are present at `/etc/yb-voyager/pg_dump-args.ini`. Any additions or modifications to this file will be honoured by yb-voyager.
* All yb-voyager commands that require a password now support providing passwords using environment variables such as `SOURCE_DB_PASSWORD` and `TARGET_DB_PASSWORD`. This addresses the security concern of a password being leaked via the `ps` command output. In addition, the password will not be present in any configuration or log files on the disk.

## v1.4 - June 30, 2023

### Key enhancements

* The `import data file` command now supports importing multiple files to the same table. Moreover, glob expressions can be provided in the `--file-table-map` argument to specify multiple files to be imported into the same table.

* In addition to AWS S3, `import data file` now supports directly importing objects (CSV/TEXT files) stored in GCS and Azure Blob Storage. You can specify GCS and Azure Blob Storage "directories" by prefixing them with `gs://` and `https://`.

* When using the [accelerated data export](../migrate/migrate-steps/#accelerate-data-export-for-mysql-and-oracle), Voyager can now connect to the source databases using SSL.

* The `analyze-schema` command now reports unsupported data types.

* The `--file-opts` CLI argument is now deprecated. Use the new [--escape-char](../reference/bulk-data-load/import-data-file/#arguments) and [--quote-char](../reference/bulk-data-load/import-data-file/#arguments) options.

### Bug fixes

* Fixed the issue where, if a CSV file had empty lines, `import data status` would continue reporting the import status as MIGRATING even though the import was completed and successful.

* yb-voyager now explicitly closes the source/target database connections when exiting.

* The `import data file` command now uses tab (\t) instead of comma (,) as the default delimiter when importing TEXT formatted files.

### Known issues

* Compared to earlier releases, Voyager v1.4 uses a different and incompatible structure to represent the import data state. As a result, Voyager v1.4 can't "continue" a data import operation that was started using Voyager v1.3 or earlier.

## v1.3 - May 30, 2023

### Key enhancements

* Export data for MySQL and Oracle is now 2-4x faster. To leverage this performance improvement, set the environment variable `BETA_FAST_DATA_EXPORT=1`. Most features, such as migrating partitioned tables, sequences, and so on, are supported in this mode. Refer to [Export data](../migrate/migrate-steps/#export-data) for more details.

* Added support for characters such as backspace(\b) in quote and escape character with [--file-opts](../reference/bulk-data-load/import-data-file/#arguments) in import data file.

* Added ability to specify null value string in import data file.

* During export data, yb-voyager can now explicitly inform you of any unsupported datatypes, and requests for permission to ignore them.

### Bug fixes

* yb-voyager can now parse `CREATE TABLE` statements that have complex check constraints.

* Import data file with AWS S3 now works when yb-voyager is installed via Docker.

## v1.2 - April 3, 2023

### Key enhancements

* When using the `import data file` command with the `--data-dir` option, you can provide an AWS S3 bucket as a path to the data directory.

* Added support for rotation of log files in a new logs directory found in `export-dir/logs`.

### Known issues

* [16658](https://github.com/yugabyte/yugabyte-db/issues/16658) The `import data file` command may not recognise the data directory being provided, causing the  step to fail for Dockerized yb-voyager.

## v1.1 - March 7, 2023

### Key enhancements

* When using the `import data file` command with CSV files, YB Voyager now supports any character as an escape character and a quote character in the `--file-opts` flag, such as single quote (`'`) as a `quote_char` and backslash (`\`) as an `escape_char`, and so on. Previously, YB Voyager only supported double quotes (`"`) as a quote character and an escape character.

* Creating the Orafce extension on the target database for Oracle migrations is now available by default.

* User creation for Oracle no longer requires `EXECUTE` permissions on `PROCEDURE`, `FUNCTION`, `PACKAGE`, and `PACKAGE BODY` objects.

* The precision and scale of numeric data types from Oracle are migrated to the target database.

* For PostgreSQL migrations, YB Voyager no longer uses a password in the `pg_dump` command running in the background. Instead, the password is internally set as an environment variable to be used by `pg_dump`.

* For any syntax error in the data file or CSV file, complete error details such as line number, column, and data are displayed in the output of the `import data` or `import data file` commands.

* For the `export data` command, the list of table names passed in the `--table-list` and `--exclude-table-list` are, by default, case insensitive. Enclose each name in double quotes to make it case-sensitive.

<!-- For the import data command,the behavior remains unchanged from the previous release. These names are, by default, case-sensitive. No need to enclose them in double-quotes. -->

* In the 1.0 release, the schema details in the report generated via `analyze-schema` are sent with diagnostics when the `--send-diagnostics` flag is on. These schema details are now removed before sending diagnostics.

* The object types which YB Voyager can't categorize are placed in a separate file as `uncategorized.sql`, and the information regarding this file is available as a note under the **Notes** section in the report generated via `analyze-schema`.

### Bug fixes

* [[765](https://github.com/yugabyte/yb-voyager/issues/765)] Fixed function parsing issue when the complete body of the function is in a single line.
* [[757](https://github.com/yugabyte/yb-voyager/issues/757)] Fixed the issue of migrating tables with names as reserved keywords in the target.
