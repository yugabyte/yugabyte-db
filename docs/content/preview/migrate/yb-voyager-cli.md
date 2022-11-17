---
title: yb-voyager CLI
linkTitle: yb-voyager CLI
description: YugabyteDB Voyager CLI and SSL connectivity.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview:
    identifier: yb-voyager-cli
    parent: voyager
    weight: 105
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

## Commands

The following command line options specify the migration steps.

### export schema

[Export the schema](../migrate-steps/#export-and-analyze-schema) from the source database.

#### Syntax

```sh
yb-voyager export schema [ <arguments> ... ]
```

- *arguments*: See [Arguments](#arguments)

#### Example

```sh
yb-voyager export schema --export-dir /path/to/yb/export/dir \
        --source-db-type sourceDB \
        --source-db-host localhost \
        --source-db-user username \
        --source-db-password password \
        --source-db-name dbname \
        --source-db-schema schemaName \ # Not applicable for MySQL
        --use-orafce \
        --start-clean

```

### analyze-schema

Analyse the PostgreSQL schema dumped in the export schema step.

#### Syntax

```sh
yb-voyager analyze-schema [ <arguments> ... ]
```

- *arguments*: See [Arguments](#arguments)

#### Example

```sh
yb-voyager analyze-schema --export-dir /path/to/yb/export/dir --output-format txt
```

### export data

Dump the source database to the machine where yb-voyager is installed.

#### Syntax

```sh
yb-voyager export data [ <arguments> ... ]
```

- *arguments*: See [Arguments](#arguments)

#### Example

```sh
yb-voyager export data --export-dir /path/to/yb/export/dir \
        --source-db-type sourceDB \
        --source-db-host hostname \
        --source-db-user username \
        --source-db-password password \
        --source-db-name dbname \
        --source-db-schema schemaName \ # Not applicable for MySQL
        --oracle-home string \ # Oracle only
        --exclude-table-list string \
        --start-clean
```

### export data status

Get the status report of an ongoing or completed data export operation.

#### Syntax

```sh
yb-voyager export data status [ <arguments> ... ]
```

- *arguments*: See [Arguments](#arguments)

#### Example

```sh
yb-voyager export data status --export-dir /path/to/yb/export/dir
```

### import schema

Import schema to the target YugabyteDB.

During migration, run the import schema command twice, first without the [--post-import-data](#post-import-data) argument and then with the argument. The second invocation creates indexes and triggers in the target schema, and must be done after [import data](../migrate-steps/#import-data) is complete.

#### Syntax

```sh
yb-voyager import schema [ <arguments> ... ]
```

- *arguments*: See [Arguments](#arguments)

#### Example

```sh
yb-voyager import schema --export-dir /path/to/yb/export/dir \
        --target-db-host hostname \
        --target-db-user username \
        --target-db-password password \
        --target-db-name dbname \
        --target-db-schema schemaName \ # MySQL and Oracle only
        --start-clean
```

### import data

Import the data objects to the target YugabyteDB.

#### Syntax

```sh
yb-voyager import data [ <arguments> ... ]
```

- *arguments*: See [Arguments](#arguments)

#### Example

```sh
yb-voyager import data --export-dir /path/to/yb/export/dir \
        --target-db-host hostname \
        --target-db-user username \
        --target-db-password password \
        --target-db-name dbname \
        --target-db-schema schemaName \ # MySQL and Oracle only
        --parallel-jobs connectionCount \
        --batch-size size \
        --start-clean
```

### import data file

Load all your data files in CSV or text format directly to the target YugabyteDB.

#### Syntax

```sh
yb-voyager import data file [ <arguments> ... ]
```

- *arguments*: See [Arguments](#arguments)

#### Example

```sh
yb-voyager import data file --export-dir /path/to/yb/export/dir \
        --target-db-host hostname \
        --target-db-port port \
        --target-db-user username \
        --target-db-password password \
        --target-db-name dbname \
        --target-db-schema schemaName \ # MySQL and Oracle only
        --data-dir "/path/to/files/dir/" \
        --file-table-map "filename1:table1,filename2:table2" \
        --delimiter "|" \
        --has-header \
        --file-opts "escape_char=\",quote_char=\"" \
        --format format \
        --start-clean

```

### import data status

Get the status report of an ongoing or completed data import operation. The report contains migration status of tables, number of rows or bytes imported, and percentage completion.

#### Syntax

```sh
yb-voyager import data status [ <arguments> ... ]
```

- *arguments*: See [Arguments](#arguments)

#### Example

```sh
yb-voyager import data status --export-dir /path/to/yb/export/dir
```

## Arguments

### --export-dir

Specifies the path to the directory containing the data files to export.

### --source-db-type

Specifies the source database type (postrgresql, mysql or oracle).

### --source-db-host

Specifies the domain name or IP address of the machine on which the source database server is running.

### --source-db-user

Specifies the username of the source database.

### --source-db-password

Specifies the password of the source database.

### --source-db-name

Specifies the name of the source database.

### --source-db-schema

Specifies the schema of the source database. Not applicable for MySQL.

### --output-format

Specifies the format in which the report file is generated. It can be in `html`, `txt`, `json` or `xml`.

### --target-db-host

Specifies the domain name or IP address of the machine on which target database server is running.

### --target-db-user

Specifies the username of the target database.

### --target-db-password

Specifies the password of the target database.

### --target-db-name

Specifies the name of the target database.

### --target-db-schema

Specifies the schema of the target database. Applicable only for MySQL and Oracle.

### --parallel-jobs

Specifies the count to increase the number of connections.

### --batch-size

Specifies the size of batches generated for ingestion during [import data](../migrate-steps/#import-data).

Default: 20,000

### --data-dir

Path to the directory containing the data files to import.

### --file-table-map

Comma-separated mapping between the files in [data-dir](#data-dir) to the corresponding table in the database.

Default: If this flag isn't used, the `import data file` command imports files from the --data-dir directory matching the `PREFIX_data.csv` pattern, where PREFIX is the name of the table into which the file data is imported.

Example : `filename1:tablename1,filename2:tablename2[,...]`

### --delimiter

Default: '\t' (tab); can be changed to comma(,), pipe(|) or any other character.

### --has-header

This argument is to be specified only for CSV file type.

Default: false; change to true if the CSV file contains column names as a header.

**Note**: Boolean flags take arguments in the format `--flag-name=[true|false]`, and not `--flag-name [true|false]`.

### --file-opts

Comma-separated string options for CSV file format. The options can include the following:

- `escape_char`: escape character

- `quote_char`: character used to quote the values

Default: double quotes (") for both escape and quote characters

Note that `escape_char` and `quote_char` are only valid and required for CSV file format.

Example: `--file-opts "escape_char=\",quote_char=\""` or `--file-opts 'escape_char=",quote_char="'`

### --format

Specifies the format of your data file with CSV or text as the supported formats.

Default: CSV

### --post-import-data

Run this argument with [import schema](#import-schema) command to import indexes and triggers in the target YugabyteDB database after data import is complete.
The `--post-import-data` argument assumes that data import is already done and imports only indexes and triggers in the target YugabyteDB database.

### --oracle-db-sid

Oracle System Identifier (SID) you can use while exporting data from Oracle instances.

### --oracle-home

Path to set `$ORACLE_HOME` environment variable. `tnsnames.ora` is found in `$ORACLE_HOME/network/admin`. Not applicable during import phases or analyze schema.

### --use-orafce

Enable using orafce extension in export schema.

Default: true

### --yes

By default, answer yes to all questions during migration.

### --start-clean

Cleans the data directories for already existing files and is applicable during all phases of migration, except [analyze-schema](../migrate-steps/#analyze-schema). For the export phase, this implies cleaning the schema or data directories depending on the current phase of migration. For the import phase, it implies cleaning the contents of the target YugabyteDB database.

### --table-list

Comma-separated list of the tables for which data is exported. Do not use in conjunction with [--exclude-table-list](#exclude-table-list).

### --exclude-table-list

Comma-separated list of tables to exclude while exporting data.

---

## SSL Connectivity

You can instruct yb-voyager to connect to the source or target database over an SSL connection. Connecting securely to PostgreSQL, MySQL, and YugabyteDB requires you to pass a similar set of arguments to yb-voyager. Oracle requires a different set of arguments.

The following table summarizes the arguments and options you can pass to yb-voyager to establish an SSL connection.

| Database | Arguments | Description |
| :------- | :------------ | :----------- |
| PostgreSQL <br /> MySQL | `--source-ssl-mode`| Value of this argument determines whether an encrypted connection is established between yb-voyager and the database server; and whether the certificate of the database server is verified from a CA. <br /> **Options**<ul><li>disable: Only try a non-SSL connection.</li><li>allow: First try a non-SSL connection; if that fails, try an SSL connection. (Not supported for MySQL.)</li><li> prefer (default): First try an SSL connection; if that fails, try a non-SSL connection.</li><li>require: Only try an SSL connection. If a root CA file is present, verify the certificate in the same way as if verify-ca was specified.</li><li> verify-ca: Only try an SSL connection, and verify that the server certificate is issued by a trusted certificate authority (CA).</li><li>verify-full: Only try an SSL connection, verify that the server certificate is issued by a trusted CA and that the requested server host name matches that in the certificate.</li></ul> |
| | `--source-ssl-cert` <br /> `--source-ssl-key` | These two arguments specify names of the files containing SSL certificate and key, respectively. The `<cert, key>` pair forms the identity of the client. |
| | `--source-ssl-root-cert` | Specifies the path to a file containing SSL certificate authority (CA) certificate(s). If the file exists, the server's certificate will be verified to be signed by one of these authorities.
| | `--source-ssl-crl` | Specifies the path to a file containing the SSL certificate revocation list (CRL). Certificates listed in this file, if it exists, will be rejected while attempting to authenticate the server's certificate.
| Oracle | `--oracle-tns-alias` | A TNS alias that is configured to establish a secure connection with the server is passed to yb-voyager. When you pass this argument, you don't need to pass the `--source-db-host`, `--source-db-port`, and `--source-db-name` arguments to yb-voyager.|
| YugabyteDB | `--target-ssl-mode` | Value of this argument determines whether an encrypted connection is established between yb-voyager and the database server; and whether the certificate of the database server is verified from a CA. <br /> **Options**<ul><li>disable: Only try a non-SSL connection.</li><li>allow: First try a non-SSL connection; if that fails, try an SSL connection. (Not supported for MySQL.)</li><li> prefer (default): First try an SSL connection; if that fails, try a non-SSL connection.</li><li>require: Only try an SSL connection. If a root CA file is present, verify the certificate in the same way as if verify-ca was specified.</li><li> verify-ca: Only try an SSL connection, and verify that the server certificate is issued by a trusted certificate authority (CA).</li><li>verify-full: Only try an SSL connection, verify that the server certificate is issued by a trusted CA and that the requested server host name matches that in the certificate.</li></ul>
| | `--target-ssl-cert` <br /> `--target-ssl-key` | These two arguments specify names of the files containing SSL certificate and key, respectively. The `<cert, key>` pair forms the identity of the client. |
| | `--target-ssl-root-cert` | Specifies the path to a file containing SSL certificate authority (CA) certificate(s). If the file exists, the server's certificate will be verified to be signed by one of these authorities. |
| | `--target-ssl-crl` | Specifies the path to a file containing the SSL certificate revocation list (CRL). Certificates listed in this file, if it exists, will be rejected while attempting to authenticate the server's certificate. |

## Unsupported features

Currently, yb-voyager doesn't support the following features:

| Feature | Description/Alternatives  | GitHub Issue |
| :-------| :---------- | :----------- |
| BLOB and CLOB | yb-voyager currently ignores all columns of type BLOB/CLOB. <br>  Use another mechanism to load the attributes till this feature is supported.| [43](https://github.com/yugabyte/yb-voyager/issues/43) |
| ALTER VIEW | YugabyteDB does not yet support any schemas containing `ALTER VIEW` statements. | [48](https://github.com/yugabyte/yb-voyager/issues/48) |

## Data modeling

Before performing migration from your source database to YugabyteDB, review your sharding strategies.

YugabyteDB supports two ways to shard data: HASH and RANGE. HASH is the default, as it is typically better suited for most OLTP applications. For more information, refer to [Hash and range sharding](../../architecture/docdb-sharding/sharding/). When exporting a PostgreSQL database, be aware that if you want RANGE sharding, you must call it out in the schema creation.

For most workloads, it is recommended to use HASH partitioning because it efficiently partitions the data, and spreads it evenly across all nodes.

RANGE sharding can be advantageous for particular use cases, such as time series. When querying data for specific time ranges, using RANGE sharding to split the data into the specific time ranges will help improve the speed and efficiency of the query.

Additionally, you can use a combination of HASH and RANGE sharding for your [primary key](../../explore/indexes-constraints/primary-key-ysql/) by choosing a HASH value as the [partition key](../../develop/learn/data-modeling-ycql/#partition-key-columns-required), and a RANGE value as the [clustering key](../../develop/learn/data-modeling-ycql/#clustering-key-columns-optional).
