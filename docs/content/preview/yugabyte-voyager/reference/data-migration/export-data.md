---
title: export data reference
headcontent: yb-voyager export data
linkTitle: export data
description: YugabyteDB Voyager export data reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-export-data
    parent: data-migration
    weight: 20
type: docs
---


The following page describes the following export commands:

- [export data](#export-data)
- [export data status](#export-data-status)

## export data

For offline migration, export data [dumps](../../../migrate/migrate-steps/#export-data) data of the source database in the `export-dir/data` directory on the machine where yb-voyager is running.

For [live migration](../../../migrate/live-migrate/#export-data) (and [fall-forward](../../../migrate/live-fall-forward/#export-data)), export data [dumps](../../../migrate/live-migrate/#export-data) the snapshot in the `data` directory and starts capturing the new changes made to the source database.

### Syntax

```text
Usage: yb-voyager export data [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for export data are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| --disable-pb |Use this argument to disable progress bar during data export and statistics printing during streaming phase. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| --export-type | Choose migration type between `snapshot-only` (offline) or `snapshot-and-changes`(live, and optionally fall-forward). <br>Default: `snapshot-only` |
| -h, --help | Command line help. |
| --oracle-cdb-name <name> | Oracle Container Database Name in case you are using a multi-tenant container database. Required for Oracle live migrations only. |
| --oracle-cdb-sid <SID> | Oracle System Identifier (SID) of the Container Database that you wish to use while exporting data from Oracle instances. Required for Oracle live migrations only. |
| --oracle-cdb-tns-alias <alias> | Name of TNS Alias you wish to use to connect to Oracle Container Database in case you are using a multi-tenant container database. Required for Oracle live migrations only. |
| --oracle-db-sid <SID> | Oracle System Identifier you can use while exporting data from Oracle instances. Oracle migrations only.|
| --oracle-home <path> | Path to set `$ORACLE_HOME` environment variable. `tnsnames.ora` is found in `$ORACLE_HOME/network/admin`. Not applicable during import phases or analyze schema. Oracle migrations only.|
| [--oracle-tns-alias](../../yb-voyager-cli/#ssl-connectivity) <alias> | TNS (Transparent Network Substrate) alias configured to establish a secure connection with the server. Oracle migrations only. |
| --parallel-jobs <connectionCount> | Number of parallel jobs to extract data from source database. <br>Default: 4; exports 4 tables at a time by default. If you use [BETA_FAST_DATA_EXPORT](../../../migrate/migrate-steps/#accelerate-data-export-for-mysql-and-oracle) to accelerate data export, yb-voyager exports only one table at a time and the --parallel-jobs argument is ignored. |
| --send-diagnostics | Send [diagnostics](../../../diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1|
| --source-db-host <hostname> | Domain name or IP address of the machine on which the source database server is running. |
| --source-db-name <name> | Source database name. |
| --source-db-password <password>| Source database password. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. Alternatively, you can also specify the password by setting the environment variable `SOURCE_DB_PASSWORD`. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose it in single quotes. |
| --source-db-port <port> | Port number of the source database machine. <br>Default: 5432 (PostgreSQL), 3306 (MySQL), and 1521 (Oracle) |
| --source-db-schema <schemaName> | Schema name of the source database. Not applicable for MySQL. For Oracle, you can specify only one schema name using this option. For PostgreSQL, you can specify a list of comma-separated schema names. Case-sensitive schema names are not yet supported. Refer to [Importing with case-sensitive schema names](../../../known-issues/general-issues/#importing-with-case-sensitive-schema-names) for more details. |
| --source-db-type <databaseType> | One of `postgresql`, `mysql`, or `oracle`. |
| --source-db-user <username> | Username of the source database. |
| [--source-ssl-cert](../../yb-voyager-cli/#ssl-connectivity) <certificateName> | Path to a file containing
 the certificate which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-key](../../yb-voyager-cli/#ssl-connectivity) <keyName> | Path to a file containing
 the key which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-crl](../../yb-voyager-cli/#ssl-connectivity) <path> | Path to a file containing the SSL certificate revocation list (CRL).|
| [--source-ssl-mode](../../yb-voyager-cli/#ssl-connectivity) <SSLmode> | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--source-ssl-root-cert](../../yb-voyager-cli/#ssl-connectivity) <path> | Path to a file containing SSL certificate authority (CA) certificate(s). |
| --start-clean | Starts a fresh data export after clearing all data from the `data` directory. <br> Accepted parameters: true, false, yes, no, 0, 1 |
| --table-list | Comma-separated list of the tables for which data needs to be exported. By default, table names in the list are case-insensitive. Enclose names in double quotes ("") to make them case-sensitive.<br>Table names can also be glob patterns containing wildcard characters, such as an asterisk (*) (matches zero or more characters) or question mark (?) (matches one character). To use a glob pattern for table names, enclose the list in single quotes ('').<br> For example, `--table-list '"Products", order*'`.<br> This argument is unsupported for live migration. |
| --exclude-table-list <tableNames> | Comma-separated list of the tables for which data needs to be excluded during export. Table names follow the same convention as `--table-list`. <br> This argument is unsupported for live migration. |
| --table-list-file-path | Path of the file containing the list of table names (comma-separated or line-separated) to export. Table names use the same convention as `--table-list`. |
| --exclude-table-list-file-path | Path of the file containing the list of table names (comma-separated or line-separated) to exclude while exporting data. Table names follow the same convention as `--table-list`. |
| --verbose | Display extra information in the output. <br>Default: false <br> Accepted parameters: true, false, yes, no, 0, 1 |
| -y, --yes | Answer yes to all prompts during the export schema operation. <br>Default: false |

### Example

```sh
yb-voyager export data --export-dir /dir/export-dir \
        --source-db-type oracle \
        --source-db-host 127.0.0.1 \
        --source-db-port 1521 \
        --source-db-user ybvoyager \
        --source-db-password 'password' \
        --source-db-name source_db \
        --source-db-schema source_schema
```

## export data status

For offline migration, get the status report of an ongoing or completed data export operation.

For live migration (and fall-forward), get the report of the ongoing export phase which includes metrics such as the number of rows exported in the snapshot phase, the total number of change events exported from the source, the number of `INSERT`/`UPDATE`/`DELETE` events, and the final row count exported.

### Syntax

```text
Usage: yb-voyager export data status [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for export data status are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help. |

### Example

```sh
yb-voyager export data status --export-dir /dir/export-dir
```
