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

## Commands

The following command line options specify the migration steps.

### export schema

[Export the schema](../../migrate-steps/#export-and-analyze-schema) from the source database.

#### Syntax

```sh
yb-voyager export schema [ <arguments> ... ]
```

The valid *arguments* for export schema are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the directory where the data files will be exported. |
| [-h, --help](#command-line-help) | Command line help. |
| [--oracle-db-sid](#oracle-db-sid) <SID> | Oracle System Identifier. Oracle migrations only.|
| [--oracle-home](#oracle-home) <path> | Path to set `$ORACLE_HOME` environment variable. Oracle migrations only.|
| [--oracle-tns-alias](#ssl-connectivity) <alias> | TNS (Transparent Network Substrate) alias configured to establish a secure connection with the server. Oracle migrations only. |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. |
| [--source-db-type](#source-db-type) <databaseType> | One of `postgresql`, `mysql`, or `oracle`. |
| [--source-db-host](#source-db-host) <hostname> | Hostname of the source database server. |
| [--source-db-name](#source-db-name) <name> | Source database name. |
| [--source-db-password](#source-db-password) <password>| Source database password. |
| [--source-db-port](#source-db-port) <port> | Port number of the source database machine. |
| [--source-db-schema](#source-db-schema) <schemaName> | Schema of the source database. |
| [--source-db-user](#source-db-user) <username> | Name of the source database user (typically `ybvoyager`). |
| [--source-ssl-cert](#ssl-connectivity) <certificateName> | Name of the certificate which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-key](#ssl-connectivity) <keyName> | Name of the key which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-crl](#ssl-connectivity) <path> | Path to a file containing the SSL certificate revocation list (CRL).|
| [--source-ssl-mode](#ssl-connectivity) <SSLmode> | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--source-ssl-root-cert](#ssl-connectivity) <path> | Path to a file containing SSL certificate authority (CA) certificate(s). |
| [--start-clean](#start-clean) | Clears the exported schema. |
| [--use-orafce](#use-orafce) | Use the Orafce extension. Oracle migrations only. |
| [-y, --yes](#yes) | Answer yes to all prompts during the export schema operation. |

#### Example

```sh
yb-voyager export schema --export-dir /path/to/yb/export/dir \
        --source-db-type sourceDB \
        --source-db-host localhost \
        --source-db-port port \
        --source-db-user username \
        --source-db-password password \ # Enclose the password in single quotes if it contains special characters.
        --source-db-name dbname \
        --source-db-schema schemaName \ # Not applicable for MySQL
        --start-clean

```

### analyze-schema

[Analyse the PostgreSQL schema](../../migrate-steps/#analyze-schema) dumped in the export schema step.

#### Syntax

```sh
yb-voyager analyze-schema [ <arguments> ... ]
```

The valid *arguments* for analyze schema are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the directory where the data files will be exported. |
| [-h, --help](#command-line-help) | Command line help. |
| [--output-format](#output-format) <format> | One of `html`, `txt`, `json`, or `xml`. |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. |

#### Example

```sh
yb-voyager analyze-schema --export-dir /path/to/yb/export/dir --output-format txt
```

### export data

[Dump](../../migrate-steps/#export-data) the source database to the machine where yb-voyager is installed.

#### Syntax

```sh
yb-voyager export data [ <arguments> ... ]
```

The valid *arguments* for export data are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [--disable-pb](#disable-pb) | Hide progress bars. |
| [--table-list](#table-list) | Comma-separated list of the tables for which data is exported. |
| [--exclude-table-list](#exclude-table-list) <tableNames> | Comma-separated list of tables to exclude while exporting data. |
| [-e, --export-dir](#export-dir) <path> | Path to the directory where the data files will be exported. |
| [-h, --help](#command-line-help) | Command line help. |
| [--oracle-db-sid](#oracle-db-sid) <SID> | Oracle System Identifier. Oracle migrations only. |
| [--oracle-home](#oracle-home) <path> | Path to set `$ORACLE_HOME` environment variable. Oracle migrations only.|
| [--oracle-tns-alias](#ssl-connectivity) <alias> | TNS (Transparent Network Substrate) alias configured to establish a secure connection with the server. Oracle migrations only. |
| [--parallel-jobs](#parallel-jobs) <connectionCount> | Number of parallel jobs to extract data from source database (default 4) |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. |
| [--source-db-type](#source-db-type) <databaseType> | One of `postgresql`, `mysql`, or `oracle`. |
| [--source-db-host](#source-db-host) <hostname> | Hostname of the source database server. |
| [--source-db-name](#source-db-name) <name> | Source database name. |
| [--source-db-password](#source-db-password) <password>| Source database password. |
| [--source-db-port](#source-db-port) <port> | Port number of the source database machine. |
| [--source-db-schema](#source-db-schema) <schemaName> | Schema of the source database. |
| [--source-db-user](#source-db-user) <username> | Username of the source database. |
| [--source-ssl-cert](#ssl-connectivity) <certificateName> | Name of the certificate which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-key](#ssl-connectivity) <keyName> | Name of the key which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-crl](#ssl-connectivity) <path> | Path to a file containing the SSL certificate revocation list (CRL).|
| [--source-ssl-mode](#ssl-connectivity) <SSLmode> | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--source-ssl-root-cert](#ssl-connectivity) <path> | Path to a file containing SSL certificate authority (CA) certificate(s). |
| [--start-clean](#start-clean) | Clears the exported data files. |
| [-y, --yes](#yes) | Answer yes to all prompts during the export schema operation. |

#### Example

```sh
yb-voyager export data --export-dir /path/to/yb/export/dir \
        --source-db-type sourceDB \
        --source-db-host hostname \
        --source-db-port port \
        --source-db-user username \
        --source-db-password password \ # Enclose the password in single quotes if it contains special characters.
        --source-db-name dbname \
        --source-db-schema schemaName # Not applicable for MySQL
```

### export data status

Get the status report of an ongoing or completed data export operation.

#### Syntax

```sh
yb-voyager export data status [ <arguments> ... ]
```

The valid *arguments* for export data status are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the directory where the data files will be exported. |
| [-h, --help](#command-line-help) | Command line help. |

#### Example

```sh
yb-voyager export data status --export-dir /path/to/yb/export/dir
```

### import schema

[Import the schema](../../migrate-steps/#import-schema) to the target YugabyteDB.

During migration, run the import schema command twice, first without the [--post-import-data](#post-import-data) argument and then with the argument. The second invocation creates indexes and triggers in the target schema, and must be done after [import data](../../migrate-steps/#import-data) is complete.

{{< note title="For Oracle migrations" >}}

For Oracle migrations using YugabyteDB Voyager v1.1 or later, the Orafce extension is installed on the target database by default. This enables you to use a subset of predefined functions, operators, and packages from Oracle. The extension is installed in the public schema, and when listing functions or views, extra objects will be visible on the target database which may confuse you. You can remove the extension using the [DROP EXTENSION](../../../api/ysql/the-sql-language/statements/ddl_drop_extension) command.

{{< /note >}}

#### Syntax

```sh
yb-voyager import schema [ <arguments> ... ]
```

The valid *arguments* for import schema are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the directory where the data files will be exported. |
| [-h, --help](#command-line-help) | Command line help. |
|  [--post-import-data](#post-import-data) | Imports indexes and triggers in the target YugabyteDB database after data import is complete. |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. |
| [--start-clean](#start-clean) | Starts a fresh import of the schema on the target YugabyteDB database. |
| [--target-db-host](#target-db-host) <hostname> | Hostname of the target database server. |
| [--target-db-name](#target-db-name) <name> | Target database name. |
| [--target-db-password](#target-db-password) <password>| Target database password. |
| [--target-db-port](#target-db-port) <port> | Port number of the target database machine. |
| [--target-db-schema](#target-db-schema) <schemaName> | Schema of the target database. |
| [--target-db-user](#target-db-user) <username> | Username of the target database. |
| [--target-ssl-cert](#ssl-connectivity) <certificateName> | Name of the certificate which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-key](#ssl-connectivity) <keyName> | Name of the key which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-crl](#ssl-connectivity) <path> | Path to a file containing the SSL certificate revocation list (CRL).|
| [--target-ssl-mode](#ssl-connectivity) <SSLmode> | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--target-ssl-root-cert](#ssl-connectivity) <path> | Path to a file containing SSL certificate authority (CA) certificate(s). |
| [--verbose](#verbose) | Display extra information in the output. |
| [-y, --yes](#yes) | Answer yes to all prompts during the export schema operation. |

<!-- To do : document the following arguments with description
--continue-on-error
--exclude-object-list
--ignore-exist
--object-list string
--refresh-mviews
--straight-order -->
#### Example

```sh
yb-voyager import schema --export-dir /path/to/yb/export/dir \
        --target-db-host hostname \
        --target-db-user username \
        --target-db-password password \ # Enclose the password in single quotes if it contains special characters.
        --target-db-name dbname \
        --target-db-schema schemaName # MySQL and Oracle only
```

### import data

[Import the data](../../migrate-steps/#import-data) to the target YugabyteDB.

#### Syntax

```sh
yb-voyager import data [ <arguments> ... ]
```

The valid *arguments* for import data are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [--batch-size](#batch-size) <number> | Size of batches generated for ingestion during [import data]. |
| [--disable-pb](#disable-pb) | Hide progress bars. |
| [--table-list](#table-list) | Comma-separated list of the tables for which data is exported. |
| [--exclude-table-list](#exclude-table-list) <tableNames> | Comma-separated list of tables to exclude while exporting data. |
| [-e, --export-dir](#export-dir) <path> | Path to the directory where the data files will be exported. |
| [-h, --help](#command-line-help) | Command line help. |
| [--parallel-jobs](#parallel-jobs) <connectionCount> | Number of parallel COPY commands issued to the target database. |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. |
| [--start-clean](#start-clean) | Starts a fresh import of data after clearing the previous state of import on the target YugabyteDB database. |
| [--target-db-host](#target-db-host) <hostname> | Hostname of the target database server. |
| [--target-db-name](#target-db-name) <name> | Target database name. |
| [--target-db-password](#target-db-password) <password>| Target database password. |
| [--target-db-port](#target-db-port) <port> | Port number of the target database machine. |
| [--target-db-schema](#target-db-schema) <schemaName> | Schema of the target database. |
| [--target-db-user](#target-db-user) <username> | Username of the target database. |
| [--target-ssl-cert](#ssl-connectivity) <certificateName> | Name of the certificate which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-key](#ssl-connectivity) <keyName> | Name of the key which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-crl](#ssl-connectivity) <path> | Path to a file containing the SSL certificate revocation list (CRL).|
| [--target-ssl-mode](#ssl-connectivity) <SSLmode> | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--target-ssl-root-cert](#ssl-connectivity) <path> | Path to a file containing SSL certificate authority (CA) certificate(s). |
| [--verbose](#verbose) | Display extra information in the output. |
| [-y, --yes](#yes) | Answer yes to all prompts during the export schema operation. |

<!-- To do : document the following arguments with description
| --continue-on-error |
| --enable-upsert |
| --target-endpoints |
| --use-public-ip | -->
#### Example

```sh
yb-voyager import data --export-dir /path/to/yb/export/dir \
        --target-db-host hostname \
        --target-db-user username \
        --target-db-password password \ # Enclose the password in single quotes if it contains special characters.
        --target-db-name dbname \
        --target-db-schema schemaName \ # MySQL and Oracle only
        --parallel-jobs connectionCount
```

### import data file

[Load all your data files](../../migrate-steps/#import-data-file) in CSV or text format directly to the target YugabyteDB. These data files can be located either on a local filesystem, an [AWS S3 bucket](../../migrate-steps/#import-data-file-from-aws-s3), [GCS bucket](../../migrate-steps/#import-data-file-from-gcs-buckets), or [Azure blob](../../migrate-steps/#import-data-file-from-azure-blob-storage-containers).

#### Syntax

```sh
yb-voyager import data file [ <arguments> ... ]
```

The valid *arguments* for import data file are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [--batch-size](#batch-size) <number> | Size of batches generated for ingestion during [import data]. |
| [--data-dir](#data-dir) <path> | Path to the location of the data files to import; this can be a local directory or a URL for a cloud storage location. |
| [--delimiter](#delimiter) | Character used as delimiter in rows of the table(s). Default: comma (,) for CSV file format and tab (\t) for TEXT file format. |
| [--disable-pb](#disable-pb) | Hide progress bars. |
| [--escape-char](#escape-char) | Escape character (default double quotes `"`) only applicable to CSV file format. |
| [--file-opts](#file-opts) <string> | **[Deprecated]** Comma-separated string options for CSV file format. |
| [--null-string](#null-string) | String that represents null value in the data file. |
| [--file-table-map](#file-table-map) <filename1:tablename1> | Comma-separated mapping between the files in [data-dir](#data-dir) to the corresponding table in the database. Multiple files can be imported in one table; for example, `foo1.csv:foo,foo2.csv:foo` or `foo*.csv:foo`. |
| [--format](#format) <format> | One of `CSV` or `text` format of the data file. |
| [--has-header](#has-header) | Applies only to CSV file type. |
| [-e, --export-dir](#export-dir) <path> | Path to the directory where the data files will be exported. |
| [-h, --help](#command-line-help) | Command line help. |
| [--parallel-jobs](#parallel-jobs) <connectionCount> | Number of parallel COPY commands issued to the target database. |
| [--quote-char](#quote-char) | Character used to quote the values (default double quotes `"`) only applicable to CSV file format. |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. |
| [--start-clean](#start-clean) | Starts a fresh import of a data file into a table in the target YugabyteDB database. |
| [--target-db-host](#target-db-host) <hostname> | Hostname of the target database server. |
| [--target-db-name](#target-db-name) <name> | Target database name. |
| [--target-db-password](#target-db-password) <password>| Target database password. |
| [--target-db-port](#target-db-port) <port> | Port number of the target database machine. |
| [--target-db-schema](#target-db-schema) <schemaName> | Schema of the target database. |
| [--target-db-user](#target-db-user) <username> | Username of the target database. |
| [--target-ssl-cert](#ssl-connectivity) <certificateName> | Name of the certificate which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-key](#ssl-connectivity) <keyName> | Name of the key which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-crl](#ssl-connectivity) <path> | Path to a file containing the SSL certificate revocation list (CRL).|
| [--target-ssl-mode](#ssl-connectivity) <SSLmode> | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--target-ssl-root-cert](#ssl-connectivity) <path> | Path to a file containing SSL certificate authority (CA) certificate(s). |
| [--verbose](#verbose) | Display extra information in the output. |
| [-y, --yes](#yes) | Answer yes to all prompts during the export schema operation. |

<!-- To do : document the following arguments with description
| --continue-on-error |
| --enable-upsert |
| --target-endpoints |
| --use-public-ip | -->

#### Example

```sh
yb-voyager import data file --export-dir /path/to/yb/export/dir \
        --target-db-host hostname \
        --target-db-port port \
        --target-db-user username \
        --target-db-password password \ # Enclose the password in single quotes if it contains special characters.
        --target-db-name dbname \
        --target-db-schema schemaName \ # MySQL and Oracle only
        --data-dir "/path/to/files/dir/" \
        --file-table-map "filename1:table1,filename2:table2" \
        --delimiter "|" \
        --has-header \
        --file-opts "escape_char=\",quote_char=\"" \
        --format format
```

### import data status

Get the status report of an ongoing or completed data import operation. The report contains migration status of tables, number of rows or bytes imported, and percentage completion.

#### Syntax

```sh
yb-voyager import data status [ <arguments> ... ]
```

The valid *arguments* for import data status are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the directory where the data files will be exported. |
| [-h, --help](#command-line-help) | Command line help. |

#### Example

```sh
yb-voyager import data status --export-dir /path/to/yb/export/dir
```

## Arguments

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

If the password contains special characters that are interpreted by the shell (for example, # and $), enclose it in single quotes.

### --source-db-name

Specifies the name of the source database.

### --source-db-schema

Specifies the schema of the source database. Not applicable for MySQL.

For Oracle, you can specify only one schema name using this option.

For PostgreSQL, you can specify a list of comma-separated schema names.

Case-sensitive schema names are not yet supported. Refer to [Import issue with case-sensitive schema names](../../known-issues/#import-issue-with-case-sensitive-schema-names) for more details.

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

If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password.

If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes.

### --target-db-name

Specifies the name of the target database.

Default: yugabyte

### --target-db-schema

Specifies the schema of the target database. MySQL and Oracle migrations only.

### --parallel-jobs

#### For export data

Specifies the number of tables to be exported in parallel from the source database at a time.

Default: 4; exports 4 tables at a time by default.

If you use [BETA_FAST_DATA_EXPORT](../../migrate-steps/#accelerate-data-export-for-mysql-and-oracle) to accelerate data export, yb-voyager exports only one table at a time and the --parallel-jobs argument is ignored.

#### For import data

Specifies the number of parallel COPY commands issued to the target database.

Depending on the target YugabyteDB configuration, the value of --parallel-jobs should be tweaked such that at most 50% of target cores are utilised.

Default: If yb-voyager can determine the total number of cores N in the target YugabyteDB cluster, it uses N/2 as the default. Otherwise, it defaults to twice the number of nodes in the cluster.

### --batch-size

Specifies the size of batches generated for ingestion during [import data](../../migrate-steps/#import-data).

Default: 20,000

### --data-dir

Path to the directory containing the data files to import. You can also provide an [AWS S3 bucket](../../migrate-steps/#import-data-file-from-aws-s3), [GCS bucket](../../migrate-steps/#import-data-file-from-gcs-buckets), and [Azure blob](../../migrate-steps/#import-data-file-from-azure-blob-storage-containers) as a path to the data directory.

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

Run this argument with [import schema](#import-schema) command to import indexes and triggers in the target YugabyteDB database after data import is complete.
The `--post-import-data` argument assumes that data import is already done and imports only indexes and triggers in the target YugabyteDB database.

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

For the export phase, cleans the exported schema/data in the export-dir.

For the import phase, starts a fresh import of schema during import schema and clears the previous state of import data, and starts a fresh import of data on the target database.

For import data file, starts a fresh import of a data file into a table in the target database.

Note: Because Voyager uses upsert mode during data import, if the target tables are not empty and they have a Primary Key, you should have no problems when using this flag, as column values will be updated. For tables with no Primary Key, you should exclude them using `--exlcude-table-list` to avoid duplicate data, if any.

### --table-list

Comma-separated list of the tables for which data needs to be migrated. Do not use in conjunction with [--exclude-table-list](#exclude-table-list).

### --exclude-table-list

Comma-separated list of tables to exclude while migrating data.

{{< note title="Note" >}}

For `export data` command, the list of table names passed in the `--table-list` and `--exclude-table-list` are, by default, case insensitive. Enclose each name in double quotes to make it case sensitive.

For `import data` command, the list of table names passed in the `--table-list` and `--exclude-table-list` are, by default, case sensitive. You don't need to enclose them in double quotes.

{{< /note >}}

### --send-diagnostics

Controls whether to send diagnostics information to Yugabyte.

Default: true

### --verbose

Displays extra information in the output.

Default: false

### --disable-pb

Use this argument to not display progress bars.

Default: false

---

## SSL connectivity

You can instruct yb-voyager to connect to the source or target database over an SSL connection. Connecting securely to PostgreSQL, MySQL, and YugabyteDB requires you to pass a similar set of arguments to yb-voyager. Oracle requires a different set of arguments.

Note that the SSL arguments are unsupported with [BETA_FAST_DATA_EXPORT](../../migrate-steps/#accelerate-data-export-for-mysql-and-oracle).

The following table summarizes the arguments and options you can pass to yb-voyager to establish an SSL connection.

| Database | Arguments | Description |
| :------- | :------------ | :----------- |
| PostgreSQL <br /> MySQL | `--source-ssl-mode`| Value of this argument determines whether an encrypted connection is established between yb-voyager and the database server; and whether the certificate of the database server is verified from a CA. <br /> **Options**<ul><li>disable: Only try a non-SSL connection.</li><li>allow: First try a non-SSL connection; if that fails, try an SSL connection. (Not supported for MySQL.)</li><li> prefer (default): First try an SSL connection; if that fails, try a non-SSL connection.</li><li>require: Only try an SSL connection. If a root CA file is present, verify the certificate in the same way as if verify-ca was specified.</li><li> verify-ca: Only try an SSL connection, and verify that the server certificate is issued by a trusted certificate authority (CA).</li><li>verify-full: Only try an SSL connection, verify that the server certificate is issued by a trusted CA and that the requested server host name matches that in the certificate.</li></ul> |
| | `--source-ssl-cert` <br /> `--source-ssl-key` | These two arguments specify names of the files containing SSL certificate and key, respectively. The `<cert, key>` pair forms the identity of the client. Note: If using [accelerated data export](../../migrate-steps/#accelerate-data-export-for-mysql-and-oracle), ensure that the keys are in the PKCS8 standard PEM format. |
| | `--source-ssl-root-cert` | Specifies the path to a file containing SSL certificate authority (CA) certificate(s). If the file exists, the server's certificate will be verified to be signed by one of these authorities.
| | `--source-ssl-crl` | Specifies the path to a file containing the SSL certificate revocation list (CRL). Certificates listed in this file, if it exists, will be rejected while attempting to authenticate the server's certificate. If using [accelerated data export](../../migrate-steps/#accelerate-data-export-for-mysql-and-oracle), this is not supported. |
| Oracle | `--oracle-tns-alias` | A TNS (Transparent Network Substrate) alias that is configured to establish a secure connection with the server is passed to yb-voyager. When you pass [`--oracle-tns-alias`](#ssl-connectivity), you cannot use any other arguments to connect to your Oracle instance including [`--source-db-schema`](#source-db-schema) and [`--oracle-db-sid`](#oracle-db-sid). Note: By default, the expectation is that the wallet files (.sso, .pk12, and so on) are in the TNS_ADMIN directory (the one containing tnsnames.ora). If the wallet files are in a different directory, ensure that you update the wallet location in the `sqlnet.ora` file. If using [accelerated data export](../../migrate-steps/#accelerate-data-export-for-mysql-and-oracle), to specify a different wallet location, also create a `ojdbc.properties` file in the TNS_ADMIN directory, and add the following: `oracle.net.wallet_location=(SOURCE=(METHOD=FILE)(METHOD_DATA=(DIRECTORY=/path/to/wallet)))`. |
| YugabyteDB | `--target-ssl-mode` | Value of this argument determines whether an encrypted connection is established between yb-voyager and the database server; and whether the certificate of the database server is verified from a CA. <br /> **Options**<ul><li>disable: Only try a non-SSL connection.</li><li>allow: First try a non-SSL connection; if that fails, try an SSL connection. (Not supported for MySQL.)</li><li> prefer (default): First try an SSL connection; if that fails, try a non-SSL connection.</li><li>require: Only try an SSL connection. If a root CA file is present, verify the certificate in the same way as if verify-ca was specified.</li><li> verify-ca: Only try an SSL connection, and verify that the server certificate is issued by a trusted certificate authority (CA).</li><li>verify-full: Only try an SSL connection, verify that the server certificate is issued by a trusted CA and that the requested server host name matches that in the certificate.</li></ul>
| | `--target-ssl-cert` <br /> `--target-ssl-key` | These two arguments specify names of the files containing SSL certificate and key, respectively. The `<cert, key>` pair forms the identity of the client. |
| | `--target-ssl-root-cert` | Specifies the path to a file containing SSL certificate authority (CA) certificate(s). If the file exists, the server's certificate will be verified to be signed by one of these authorities. |
| | `--target-ssl-crl` | Specifies the path to a file containing the SSL certificate revocation list (CRL). Certificates listed in this file, if it exists, will be rejected while attempting to authenticate the server's certificate. |
