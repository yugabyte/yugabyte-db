---
title: yb-voyager CLI reference
headcontent: yb-voyager command line interface
linkTitle: yb-voyager CLI
description: YugabyteDB Voyager CLI and SSL connectivity.
menu:
  preview_yugabyte-voyager:
    identifier: yb-voyager-cli
    parent: reference-voyager
    weight: 100
type: docs
rightNav:
  hideH4: true
---

yb-voyager is a command line executable for migrating databases from PostgreSQL, Oracle, and MySQL to a YugabyteDB database.

## Syntax

```sh
yb-voyager [ <migration-step>... ] [ <arguments> ... ]
```

- *migration-step*: See [Commands](#commands)
- *arguments*: See [Arguments](#arguments)

### Command line help

To display the available online help, run:

```sh
yb-voyager --help
```

To display the available online help for any migration step, run:

```sh
yb-voyager [ <migration-step>... ] --help
```

### Version check

To verify the version of yb-voyager installed on your machine, run:

```sh
yb-voyager version
```


<!-- ## Arguments

### --export-dir

Specifies the path to the directory containing the data files to export.

An export directory is a workspace used by yb-voyager to store the following:

- exported schema DDL files
- export data files
- migration state
- log file

### --source-db-type

Specifies the source database type (postgresql, mysql, or oracle).

### --source-db-host

Specifies the domain name or IP address of the machine on which the source database server is running.

### --source-db-port

Specifies the port number of the machine on which the source database server is running.

Default: 5432 (PostgreSQL), 3306 (MySQL), and 1521 (Oracle)

### --source-db-user

Specifies the username of the source database.

### --source-db-password

Specifies the password of the source database.

If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password.

Alternatively, you can also specify the password by setting the environment variable `SOURCE_DB_PASSWORD`.

If the password contains special characters that are interpreted by the shell (for example, # and $), enclose it in single quotes.

### --source-db-name

Specifies the name of the source database.

### --source-db-schema

Specifies the schema name of the source database. Not applicable for MySQL.

For Oracle, you can specify only one schema name using this option.

For PostgreSQL, you can specify a list of comma-separated schema names.

Case-sensitive schema names are not yet supported. Refer to [Importing with case-sensitive schema names](../../known-issues/general-issues/#importing-with-case-sensitive-schema-names) for more details.

### --output-format

Specifies the format in which the report file is generated. It can be in `html`, `txt`, `json`, or `xml`.

### --target-db-host

Specifies the domain name or IP address of the machine on which target database server is running.

### --target-db-port

Specifies the port number of the machine on which the target database server is running.

Default: 5433

### --target-db-user

Specifies the username in the target database to be used for the migration.

### --target-db-password

Specifies the password for the target database user to be used for the migration.

Alternatively, you can also specify the password by setting the environment variable `TARGET_DB_PASSWORD`.

If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password.

If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes.

### --target-db-name

Specifies the name of the target database.

Default: yugabyte

### --target-db-schema

Specifies the schema name of the target database. MySQL and Oracle migrations only.

### --parallel-jobs

#### For export data

Specifies the number of tables to be exported in parallel from the source database at a time.

Default: 4; exports 4 tables at a time by default.

If you use [BETA_FAST_DATA_EXPORT](../../migrate/migrate-steps/#accelerate-data-export-for-mysql-and-oracle) to accelerate data export, yb-voyager exports only one table at a time and the --parallel-jobs argument is ignored.

#### For import data

Specifies the number of parallel COPY commands issued to the target database.

Depending on the YugabyteDB database configuration, the value of --parallel-jobs should be tweaked such that at most 50% of target cores are utilised.

Default: If yb-voyager can determine the total number of cores N in the YugabyteDB database cluster, it uses N/2 as the default. Otherwise, it defaults to twice the number of nodes in the cluster.

### --batch-size

Specifies the number of rows in each batch generated for ingestion during [import data](../../migrate/migrate-steps/#import-data).

Default: 20000; rows.

### --data-dir

Path to the directory containing the data files to import. You can also provide the URI of an AWS S3 bucket, GCS bucket, or an Azure blob. For more details, see [Bulk data load from files](../../migrate/bulk-data-load/).

### --file-table-map

Comma-separated mapping between the files in [data-dir](#data-dir) to the corresponding table in the database.

Example: `filename1:tablename1,filename2:tablename2[,...]`

You can import multiple files in one table either by providing one `<fileName>:<tableName>` entry for each file OR by passing a glob expression in place of the file name. For example, `fileName1:tableName,fileName2:tableName` OR `fileName*:tableName`.

### --delimiter

Character used as delimiter in rows of the table(s).

Default: comma (,) for CSV file format and tab (\t) for TEXT file format.

### --escape-char

Escape character; only applicable to CSV file format.

Default: double quotes (")

### --quote-char

Quote character; only applicable to CSV file format.

Default: double quotes (")

### --has-header

This argument is to be specified only for CSV file type.

Default: false; change to true if the CSV file contains column names as a header.

**Note**: Boolean flags take arguments in the format `--flag-name=[true|false]`, and not `--flag-name [true|false]`.

### --file-opts

**[Deprecated]** Comma-separated string options for CSV file format. The options can include the following:

- `escape_char`: escape character

- `quote_char`: character used to quote the values

Default: double quotes (") for both escape and quote characters

Note that `escape_char` and `quote_char` are only valid and required for CSV file format.

Example: `--file-opts "escape_char=\",quote_char=\""` or `--file-opts 'escape_char=",quote_char="'`

### --null-string

String that represents null value in the data file.

Default: ""(empty string) for CSV, and '\N' for text.

### --format

Specifies the format of your data file with CSV or text as the supported formats.

Default: CSV

### --post-import-data

Run this argument with [import schema](#import-schema) command to import indexes and triggers in the YugabyteDB database after data import is complete.
The `--post-import-data` argument assumes that data import is already done and imports only indexes and triggers in the YugabyteDB database .

### --oracle-db-sid

Oracle System Identifier (SID) you can use while exporting data from Oracle instances. Oracle migrations only.

### --oracle-home

Path to set `$ORACLE_HOME` environment variable. `tnsnames.ora` is found in `$ORACLE_HOME/network/admin`. Not applicable during import phases or analyze schema. Oracle migrations only.

### --use-orafce

Enable using orafce extension in export schema. Oracle migrations only.

Default: true

### --yes

By default, answer yes to all questions during migration.

### --start-clean

For the export phase, starts a fresh schema export during export schema after clearing the `schema` directory,
and a fresh data export during export data after clearing `data` directory.

For the import phase, starts a fresh import of schema during import schema  on the target for the schema present in the `schema` directory.

For import data or import data file, the flag starts a fresh import with data files present in the `data` directory and if any table on the target database is non-empty, it prompts whether you want to continue the import without the truncating those tables. If yes, then yb-voyager starts ingesting the data present in the data files with upsert mode.

For tables with no primary key, you should exclude them using [--exlcude-table-list](#exclude-table-list) to avoid duplicate data, if any, or truncate those tables manually before using the `start-clean` flag.

### --table-list

Comma-separated list of the tables for which data needs to be migrated. Do not use in conjunction with [--exclude-table-list](#exclude-table-list).

### --exclude-table-list

Comma-separated list of tables to exclude while migrating data.

{{< note title="Note" >}}

For `export data` command, the list of table names passed in the `--table-list` and `--exclude-table-list` are, by default, case insensitive. Enclose each name in double quotes to make it case sensitive.

For `import data` command, the list of table names passed in the `--table-list` and `--exclude-table-list` are, by default, case sensitive. You don't need to enclose them in double quotes.

For live migration, during `import data`, the `exclude-table-list` argument is not supported.

{{< /note >}}

### --send-diagnostics

Controls whether to send diagnostics information to Yugabyte.

Default: true

### --verbose

Displays extra information in the output.

Default: false

### --disable-pb

Use this argument to not display progress bars.
For live migration, `--disable-pb` can also be used to hide metrics for [export data](#export-data) or [import data](#import-data).

Default: false

### --ff-db-host

Specifies the domain name or IP address of the machine on which Fall-forward database server is running.

### --ff-db-name

Specifies the name of the fall-forward database.

### --ff-db-password

Specifies the password for the fall-forward database user to be used for the migration. Alternatively, you can also specify the password by setting the environment variable `FF_DB_PASSWORD`.

If you don't provide a password via the CLI or environment variable during any migration phase, yb-voyager will prompt you at runtime for a password.

If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes.

### --ff-db-port

Specifies the port number of the machine on which the fall-forward database server is running.

Default: For Oracle databases, 1521

### --ff-db-schema

Specifies the schema name of the fall-forward database.

### --ff-db-user

Specifies the username in the Fall-forward database to be used for the migration.

### --export-type

Specifies the type of migration [`snapshot-only` (offline), `snapshot-and-changes`(live, and optionally fall-forward) ]

Default: `snapshot-only`

### --delete

Deletes the exported data in the CDC phase after moving it to another location.

Default: false

### --fs-utilization-threshold

Specifies disk utilization percentage after which you can [archive changes](#archive-changes).

### --move-to

Specifies the destination path to which [archive changes](#archive-changes) should move the files to. -->
