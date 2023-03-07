---
title: What's new in YugabyteDB Voyager
linkTitle: What's new
description: YugabyteDB Voyager release notes.
headcontent: New features, key enhancements, and bug fixes
image: /images/section_icons/index/quick_start.png
menu:
  preview:
    identifier: release-notes
    parent: voyager
    weight: 106
type: docs
---

Included here are the release notes for the YugabyteDB Voyager v1 release series. Content will be added as new notable features and changes are available in the patch releases of the YugabyteDB v1 series.

## v1.1 - March 7, 2023

### Key enhancements

* In the 1.0 release, for the `import data` file command with CSV files, YB Voyager only supported the double quote (") as a quote character and an escape character. From the 1.1 release, YB Voyager supports any character as an escape character and a quote character in the `--file-opts` flag such as, single quote (') as a `quote_char` and backslash (\) as an `escape_char`, and so on.

* By default, creating the Orafce extension on the target database for Oracle migrations is available.

* Enhanced user creation steps for Oracle to not require `EXECUTE` permissions on `PROCEDURE`, `FUNCTION`, `PACKAGE`, and `PACKAGE BODY` objects.

* The precision and scale of numeric data types from Oracle are migrated to the target database.

* For PostgreSQL migrations, YB Voyager is not using a password in the `pg_dump` command running in the background, instead, it is internally set as an environment variable to be used by `pg_dump`.

* For any syntax error in the data file or CSV file, complete error details such as line number, column, and data is displayed in the output of the import data or import data file commands.

* In this release, for the export data command, the list of table names passed in the `--table-list` and `--exclude-table-list` are, by default, case insensitive. Enclose each name in double quotes to make it case-sensitive.

<!-- For the import data command,the behavior remains unchanged from the previous release. These names are, by default, case-sensitive. No need to enclose them in double-quotes. -->

* In the 1.0 release, the schema details in the report generated via `analyze-schema` are sent with diagnostics when the `--send-diagnostics flag` is on. However, from the 1.1 release, schema details are removed before sending diagnostics.

* The object types which YB Voyager can't categorize are placed in a separate file as `uncategorized.sql`, and the information regarding this file is available as a note under the **Notes** section in the report generated via `analyze-schema`.

### Bug fixes

* [[765](https://github.com/yugabyte/yb-voyager/issues/765)] Fixed function parsing issue when the complete body of the function is in a single line.
* [[757](https://github.com/yugabyte/yb-voyager/issues/757)] Fixed the issue of migrating tables with names as reserved keywords in the target.
