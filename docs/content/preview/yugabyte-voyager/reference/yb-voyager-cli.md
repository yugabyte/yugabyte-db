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

[Export the schema](../../migrate/migrate-steps/#export-and-analyze-schema) from the source database.

#### Syntax

```sh
yb-voyager export schema [ <arguments> ... ]
```

The valid *arguments* for export schema are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
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
| [--source-db-schema](#source-db-schema) <schemaName> | Schema name of the source database. |
| [--source-db-user](#source-db-user) <username> | Name of the source database user (typically `ybvoyager`). |
| [--source-ssl-cert](#ssl-connectivity) <certificateName> | Name of the certificate which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-key](#ssl-connectivity) <keyName> | Name of the key which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-crl](#ssl-connectivity) <path> | Path to a file containing the SSL certificate revocation list (CRL).|
| [--source-ssl-mode](#ssl-connectivity) <SSLmode> | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--source-ssl-root-cert](#ssl-connectivity) <path> | Path to a file containing SSL certificate authority (CA) certificate(s). |
| [--start-clean](#start-clean) | Starts a fresh schema export after clearing the `schema` directory. |
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

[Analyse the PostgreSQL schema](../../migrate/migrate-steps/#analyze-schema) dumped in the export schema step.

#### Syntax

```sh
yb-voyager analyze-schema [ <arguments> ... ]
```

The valid *arguments* for analyze schema are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help. |
| [--output-format](#output-format) <format> | One of `html`, `txt`, `json`, or `xml`. |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. |

#### Example

```sh
yb-voyager analyze-schema --export-dir /path/to/yb/export/dir --output-format txt
```

### export data

For offline migration, export data [dumps](../../migrate/migrate-steps/#export-data) the source database to the machine where yb-voyager is installed.

For [live migration](../../migrate/live-migrate/#export-data) (and [fall-forward](../../migrate/live-fall-forward/#export-data)), export data [dumps](../../migrate/live-migrate/#export-data) the snapshot in the `data` directory and starts capturing the new changes made to the source database.

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
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [--export-type](#export-type) | Choose migration type between `snapshot-only` (default) or `snapshot-and-changes` |
| [-h, --help](#command-line-help) | Command line help. |
| [--oracle-db-sid](#oracle-db-sid) <SID> | Oracle System Identifier. Oracle migrations only. |
| [--oracle-home](#oracle-home) <path> | Path to set `$ORACLE_HOME` environment variable. Oracle migrations only.|
| [--oracle-tns-alias](#ssl-connectivity) <alias> | TNS (Transparent Network Substrate) alias configured to establish a secure connection with the server. Oracle migrations only. |
| [--parallel-jobs](#parallel-jobs) <connectionCount> | Number of parallel jobs to extract data from source database. (Default: 4) |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. |
| [--source-db-type](#source-db-type) <databaseType> | One of `postgresql`, `mysql`, or `oracle`. |
| [--source-db-host](#source-db-host) <hostname> | Hostname of the source database server. |
| [--source-db-name](#source-db-name) <name> | Source database name. |
| [--source-db-password](#source-db-password) <password>| Source database password. |
| [--source-db-port](#source-db-port) <port> | Port number of the source database machine. |
| [--source-db-schema](#source-db-schema) <schemaName> | Schema name of the source database. |
| [--source-db-user](#source-db-user) <username> | Username of the source database. |
| [--source-ssl-cert](#ssl-connectivity) <certificateName> | Name of the certificate which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-key](#ssl-connectivity) <keyName> | Name of the key which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-crl](#ssl-connectivity) <path> | Path to a file containing the SSL certificate revocation list (CRL).|
| [--source-ssl-mode](#ssl-connectivity) <SSLmode> | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--source-ssl-root-cert](#ssl-connectivity) <path> | Path to a file containing SSL certificate authority (CA) certificate(s). |
| [--start-clean](#start-clean) | Starts a fresh data export after clearing all data from the `data` directory.  |
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

For offline migration, get the status report of an ongoing or completed data export operation.

For live migration (and fall-forward), get the report of the ongoing export phase which includes metrics such as the number of rows exported in the snapshot phase, the total number of change events exported from the source, the number of `INSERT`/`UPDATE`/`DELETE` events, and the final row count exported.

#### Syntax

```sh
yb-voyager export data status [ <arguments> ... ]
```

The valid *arguments* for export data status are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help. |

#### Example

```sh
yb-voyager export data status --export-dir /path/to/yb/export/dir
```

### import schema

[Import the schema](../../migrate/migrate-steps/#import-schema) to the YugabyteDB database.

During migration, run the import schema command twice, first without the [--post-import-data](#post-import-data) argument and then with the argument. The second invocation creates indexes and triggers in the target schema, and must be done after [import data](../../migrate/migrate-steps/#import-data) is complete.

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
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help. |
|  [--post-import-data](#post-import-data) | Imports indexes and triggers in the YugabyteDB database after data import is complete. |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. |
| [--start-clean](#start-clean) | Starts a fresh schema import on the target yugabyteDB database for the schema present in the `schema` directory |
| [--target-db-host](#target-db-host) <hostname> | Hostname of the target database server. |
| [--target-db-name](#target-db-name) <name> | Target database name. |
| [--target-db-password](#target-db-password) <password>| Target database password. |
| [--target-db-port](#target-db-port) <port> | Port number of the target database machine. |
| [--target-db-schema](#target-db-schema) <schemaName> | Schema name of the target database. |
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

For offline migration, [Import the data](../../migrate/migrate-steps/#import-data) to the YugabyteDB database.

For live migration (and fall-forward), the command [imports the data](../../migrate/migrate-steps/#import-data) from the `data` directory to the target database, and starts ingesting the new changes captured by `export data` to the target database.

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
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help. |
| [--parallel-jobs](#parallel-jobs) <connectionCount> | Number of parallel COPY commands issued to the target database. |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. |
| [--start-clean](#start-clean) | Starts a fresh import with data files present in the `data` directory. If any table on the YugabyteDB database is not empty, you are prompted to continue the import without truncating those tables. If you continue, yb-voyager starts ingesting the data present in the data files with upsert mode, and for cases where a table doesn't have a primary key, it may duplicate the data. In this case, you should use the `--exclude-table-list` flag to exclude such tables, or truncate those tables manually before using the `start-clean` flag. |
| [--target-db-host](#target-db-host) <hostname> | Hostname of the target database server. |
| [--target-db-name](#target-db-name) <name> | Target database name. |
| [--target-db-password](#target-db-password) <password>| Target database password. |
| [--target-db-port](#target-db-port) <port> | Port number of the target database machine. |
| [--target-db-schema](#target-db-schema) <schemaName> | Schema name of the target database. |
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

Load data from files in CSV or text format directly to the YugabyteDB database. These data files can be located either on a local filesystem, an AWS S3 bucket, GCS bucket, or an Azure blob. For more details, see [Bulk data load from files](../../migrate/bulk-data-load/).

#### Syntax

```sh
yb-voyager import data file [ <arguments> ... ]
```

The valid *arguments* for import data file are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [--batch-size](#batch-size) <number> | Size of batches generated for ingestion during import data. |
| [--data-dir](#data-dir) <path> | Path to the location of the data files to import; this can be a local directory or a URL for a cloud storage location. |
| [--delimiter](#delimiter) | Character used as delimiter in rows of the table(s). Default: comma (,) for CSV file format and tab (\t) for TEXT file format. |
| [--disable-pb](#disable-pb) | Hide progress bars. |
| [--escape-char](#escape-char) | Escape character (default double quotes `"`) only applicable to CSV file format. |
| [--file-opts](#file-opts) <string> | **[Deprecated]** Comma-separated string options for CSV file format. |
| [--null-string](#null-string) | String that represents null value in the data file. |
| [--file-table-map](#file-table-map) <filename1:tablename1> | Comma-separated mapping between the files in [data-dir](#data-dir) to the corresponding table in the database. Multiple files can be imported in one table; for example, `foo1.csv:foo,foo2.csv:foo` or `foo*.csv:foo`. |
| [--format](#format) <format> | One of `CSV` or `text` format of the data file. |
| [--has-header](#has-header) | Applies only to CSV file type. |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help. |
| [--parallel-jobs](#parallel-jobs) <connectionCount> | Number of parallel COPY commands issued to the target database. |
| [--quote-char](#quote-char) | Character used to quote the values (default double quotes `"`) only applicable to CSV file format. |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. |
| [--start-clean](#start-clean) | Starts a fresh import with data files present in the `data` directory and if any table on YugabyteDB database is non-empty, it prompts whether you want to continue the import without truncating those tables; if yes, then yb-voyager starts ingesting the data present in the data files with upsert mode and for the cases where a table doesn't have a primary key, it may duplicate the data. In that case, use `--exclude-table-list` flag to exclude such tables or truncate those tables manually before using the `start-clean` flag. |
| [--target-db-host](#target-db-host) <hostname> | Hostname of the target database server. |
| [--target-db-name](#target-db-name) <name> | Target database name. |
| [--target-db-password](#target-db-password) <password>| Target database password. |
| [--target-db-port](#target-db-port) <port> | Port number of the target database machine. |
| [--target-db-schema](#target-db-schema) <schemaName> | Schema name of the target database. |
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

For offline migration, get the status report of an ongoing or completed data import operation. The report contains migration status of tables, number of rows or bytes imported, and percentage completion.

For live migration, get the status report of [import data](#import-data). For live migration with fall forward, the report also includes the status of [fall forward setup](#fall-forward-setup). The report includes the status of tables, the number of rows imported, the total number of changes imported, the number of `INSERT`, `UPDATE`, and `DELETE` events, and the final row count of the target or fall-forward database.

#### Syntax

```sh
yb-voyager import data status [ <arguments> ... ]
```

The valid *arguments* for import data status are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help. |
| [target-db-password](#target-db-password) | Password of the target database. Live migrations only. |
| [ff-db-password](#ff-db-password) | Password of the fall-forward database. Live migration with fall-forward only.  |

#### Example

```sh
yb-voyager import data status --export-dir /path/to/yb/export/dir
```

### fall-forward setup

Imports data to the fall-forward database, and streams new changes from the YugabyteDB database to the fall-forward database.

#### Syntax

```sh
yb-voyager fall-forward setup [ <arguments> ... ]
```

The valid *arguments* for fall-forward setup are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [--batch-size](#batch-size) <number> | Size of batches generated for ingestion during import data. (Default: 10000000) |
| --continue-on-error | Ignores errors and continues data import. |
| [--disable-pb](#disable-pb) | Set to true to disable progress bar during data import. (Default: false) |
| --enable-upsert | Set to true to enable UPSERT mode on target tables, and false to disable UPSERT mode on target tables. (Default: true) |
| [--exclude-table-list](#exclude-table-list) <tableNames> | Comma-separated list of tables to exclude while importing data (ignored if `--table-list` is used). |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [--ff-db-host](#ff-db-host) <hostname> | Host on which the fall-forward database server is running. (Default: "127.0.0.1") |
| [--ff-db-name](#ff-db-name) <name> | Name of the database in the fall-forward database server on which import needs to be done |
| [--ff-db-password](#ff-db-password) <password> | Password to connect to the fall-forward database server. |
| [--ff-db-port](#ff-db-port) <port> | Port number of the fall-forward database server. (Default: 1521) |
| [--ff-db-schema](#ff-db-schema) <schemaName> | Schema name of the fall-forward database. |
| --ff-db-sid <SID> | Oracle System Identifier (SID) that you wish to use while importing data to Oracle instances. Oracle migrations only. |
| [--ff-db-user] <username>| Username to connect to the fall-forward database server. |
| --ff-ssl-cert <path>| Fall-forward database SSL Certificate path. |
| --ff-ssl-crl <list>| Fall-forward database SSL Root Certificate Revocation List (CRL). |
| --ff-ssl-key <Keypath> | Fall-forward database SSL key path |
| --ff-ssl-mode <SSLmode>| One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| --ff-ssl-root-cert | Fall-forward database SSL Root Certificate path. |
| [-h, --help](#command-line-help) | Command line help for setup. |
| [--oracle-home](#oracle-home) <path> | Path to set $ORACLE_HOME environment variable. `tnsnames.ora` is found in `$ORACLE_HOME/network/admin`. Oracle migrations only.|
| [--oracle-tns-alias](#ssl-connectivity) <alias> | TNS (Transparent Network Substrate) alias configured to establish a secure connection with the server. Oracle migrations only. |
| [--parallel-jobs](#parallel-jobs) <connectionCount> | Number of parallel COPY commands issued to the target database. (Default: 1) |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. (Default: true) |
| [--start-clean](#start-clean) | Starts a fresh import with data files present in the `data` directory and if any table on fall- forward database is non-empty, it prompts whether you want to continue the import without the truncating those tables; if yes, then yb-voyager starts ingesting the data present in the data files without upsert mode and for the cases where a table doesn't have a primary key, it may duplicate the data. So, in that case, use `--exclude-table-list` flag to exclude such tables or truncate those tables manually before using the `start-clean` flag. |
| [--table-list](#table-list) | Comma-separated list of the tables to import data. |
| [--verbose](#verbose) | Display extra information in the output. |
| [-y, --yes](#yes) | Answer yes to all prompts during the migration. (Default: false) |

#### Example

```sh
yb-voyager fall-forward setup --export-dir /path/to/yb/export/dir \
        --ff-db-host hostname \
        --ff-db-user username \
        --ff-db-password password \ # Enclose the password in single quotes if it contains special characters.
        --ff-db-name dbname \
        --ff-db-schema schemaName \
        --parallel-jobs connectionCount
```

### fall-forward synchronize

Exports new changes from the YugabyteDB database to import to the fall-forward database so that the fall-forward database can be in sync with the YugabyteDB database after cutover.

#### Syntax

```sh
yb-voyager fall-forward synchronize [ <arguments> ... ]
```

The valid *arguments* for fall-forward synchronize are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| --disable-pb | Set to true to disable progress bar during data import (default false) |
| [--exclude-table-list](#exclude-table-list) <tableNames> | Comma-separated list of tables to exclude while importing data (ignored if `--table-list` is used) |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help for synchronize. |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. (Default: true) |
| [--table-list](#table-list) | Comma-separated list of the tables to export data. |
| [--target-db-host](#target-db-host) <hostname> | Hostname of the target database server. (Default: "127.0.0.1")|
| [--target-db-name](#target-db-name) <name> | Target database name on which import needs to be done.|
| [--target-db-password](#target-db-password) <password>| Target database password to connect to the YugabyteDB database server. |
| [--target-db-port](#target-db-port) <port> | Port number of the target database machine that runs the YugabyteDB YSQL API. |
| [--target-db-schema](#target-db-schema) <schemaName> | Target schema name in YugabyteDB. |
| [--target-db-user](#target-db-user) <username> | Username of the target database. |
| [--target-ssl-cert](#ssl-connectivity) <certificateName> | Name of the certificate which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-key](#ssl-connectivity) <keyName> | Name of the key which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-crl](#ssl-connectivity) <path> | Path to a file containing the SSL certificate revocation list (CRL).|
| [--target-ssl-mode](#ssl-connectivity) <SSLmode> | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--target-ssl-root-cert](#ssl-connectivity) <path> | Path to a file containing SSL certificate authority (CA) certificate(s). |
| [--verbose](#verbose) | Display extra information in the output. |
| [-y, --yes](#yes) | Answer yes to all prompts during migration (Default: false). |

#### Example

```sh
yb-voyager fall-forward synchronize --export-dir /path/to/yb/export/dir \
        --target-db-host hostname \
        --target-db-port port \
        --target-db-user username \
        --target-db-password password \ # Enclose the password in single quotes if it contains special characters.
        --target-db-name dbname \
        --target-db-schema schemaName
```

### cutover initiate

Initiate cutover to the YugabyteDB database.

#### Syntax

```sh
yb-voyager cutover initiate [ <arguments> ... ]
```

The valid *arguments* for cutover initiate are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help for cutover initiate. |

#### Example

```sh
yb-voyager cutover initiate --export-dir /path/to/yb/export/dir
```

### cutover status

Shows the status of the cutover to the YugabyteDB database. Status can be "INITIATED", "NOT INITIATED", or "COMPLETED".

#### Syntax

```sh
yb-voyager cutover status [ <arguments> ... ]
```

The valid *arguments* for cutover status are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help for cutover status. |

#### Example

```sh
yb-voyager cutover status --export-dir /path/to/yb/export/dir
```

### fall-forward switchover

Initiate a switchover to the fall-forward database.

#### Syntax

```sh
yb-voyager fall-forward switchover [ <arguments> ... ]
```

The valid *arguments* for fall-forward switchover are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help for switchover. |

#### Example

```sh
yb-voyager fall-forward switchover --export-dir /path/to/yb/export/dir
```

### fall-forward status

Shows the status of the fall-forward switchover to the fall-forward database. Status can be "INITIATED", "NOT INITIATED", or "COMPLETED".

#### Syntax

```sh
yb-voyager fall-forward status [ <arguments> ... ]
```

The valid *arguments* for fall-forward switchover status are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help for cutover status. |

#### Example

```sh
yb-voyager fall-forward status --export-dir /path/to/yb/export/dir \
```

### archive changes

Archives the streaming data from the source database.

#### Syntax

```sh
yb-voyager archive changes [ <arguments> ... ]
```

The valid *arguments* for archive changes status are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help for archive changes. |
| [--delete](#delete) |  Delete exported data after moving it to the target database. (Default: false) |
| [--move-to](#move-to) <path> | Destination path to move exported data to. |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. (Default: true) |
| [--verbose](#verbose) | Display extra information in the output. |
| [-y, --yes](#yes) | Answer yes to all prompts during migration (Default: false). |

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

Specifies the destination path to which [archive changes](#archive-changes) should move the files to.

---

## SSL connectivity

You can instruct yb-voyager to connect to the source or target database over an SSL connection. Connecting securely to PostgreSQL, MySQL, and YugabyteDB requires you to pass a similar set of arguments to yb-voyager. Oracle requires a different set of arguments.

The following table summarizes the arguments and options you can pass to yb-voyager to establish an SSL connection.

| Database | Arguments | Description |
| :------- | :------------ | :----------- |
| PostgreSQL <br /> MySQL | `--source-ssl-mode`| Value of this argument determines whether an encrypted connection is established between yb-voyager and the database server; and whether the certificate of the database server is verified from a CA. <br /> **Options**<ul><li>disable: Only try a non-SSL connection.</li><li>allow: First try a non-SSL connection; if that fails, try an SSL connection. (Not supported for MySQL.)</li><li> prefer (default): First try an SSL connection; if that fails, try a non-SSL connection.</li><li>require: Only try an SSL connection. If a root CA file is present, verify the certificate in the same way as if verify-ca was specified.</li><li> verify-ca: Only try an SSL connection, and verify that the server certificate is issued by a trusted certificate authority (CA).</li><li>verify-full: Only try an SSL connection, verify that the server certificate is issued by a trusted CA and that the requested server host name matches that in the certificate.</li></ul> |
| | `--source-ssl-cert` <br /> `--source-ssl-key` | These two arguments specify names of the files containing SSL certificate and key, respectively. The `<cert, key>` pair forms the identity of the client. Note: If using [accelerated data export](../../migrate/migrate-steps/#accelerate-data-export-for-mysql-and-oracle), ensure that the keys are in the PKCS8 standard PEM format. |
| | `--source-ssl-root-cert` | Specifies the path to a file containing SSL certificate authority (CA) certificate(s). If the file exists, the server's certificate will be verified to be signed by one of these authorities.
| | `--source-ssl-crl` | Specifies the path to a file containing the SSL certificate revocation list (CRL). Certificates listed in this file, if it exists, will be rejected while attempting to authenticate the server's certificate. If using [accelerated data export](../../migrate/migrate-steps/#accelerate-data-export-for-mysql-and-oracle), this is not supported. |
| Oracle | `--oracle-tns-alias` | A TNS (Transparent Network Substrate) alias that is configured to establish a secure connection with the server is passed to yb-voyager. When you pass [`--oracle-tns-alias`](#ssl-connectivity), you cannot use any other arguments to connect to your Oracle instance including [`--source-db-schema`](#source-db-schema) and [`--oracle-db-sid`](#oracle-db-sid). Note: By default, the expectation is that the wallet files (.sso, .pk12, and so on) are in the TNS_ADMIN directory (the one containing tnsnames.ora). If the wallet files are in a different directory, ensure that you update the wallet location in the `sqlnet.ora` file. If using [accelerated data export](../../migrate/migrate-steps/#accelerate-data-export-for-mysql-and-oracle), to specify a different wallet location, also create a `ojdbc.properties` file in the TNS_ADMIN directory, and add the following: `oracle.net.wallet_location=(SOURCE=(METHOD=FILE)(METHOD_DATA=(DIRECTORY=/path/to/wallet)))`. |
| YugabyteDB | `--target-ssl-mode` | Value of this argument determines whether an encrypted connection is established between yb-voyager and the database server; and whether the certificate of the database server is verified from a CA. <br /> **Options**<ul><li>disable: Only try a non-SSL connection.</li><li>allow: First try a non-SSL connection; if that fails, try an SSL connection. (Not supported for MySQL.)</li><li> prefer (default): First try an SSL connection; if that fails, try a non-SSL connection.</li><li>require: Only try an SSL connection. If a root CA file is present, verify the certificate in the same way as if verify-ca was specified.</li><li> verify-ca: Only try an SSL connection, and verify that the server certificate is issued by a trusted certificate authority (CA).</li><li>verify-full: Only try an SSL connection, verify that the server certificate is issued by a trusted CA and that the requested server host name matches that in the certificate.</li></ul>
| | `--target-ssl-cert` <br /> `--target-ssl-key` | These two arguments specify names of the files containing SSL certificate and key, respectively. The `<cert, key>` pair forms the identity of the client. |
| | `--target-ssl-root-cert` | Specifies the path to a file containing SSL certificate authority (CA) certificate(s). If the file exists, the server's certificate will be verified to be signed by one of these authorities. |
| | `--target-ssl-crl` | Specifies the path to a file containing the SSL certificate revocation list (CRL). Certificates listed in this file, if it exists, will be rejected while attempting to authenticate the server's certificate. |
