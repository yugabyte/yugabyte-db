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
aliases:
  - /preview/yugabyte-voyager/reference/fall-forward/fall-forward-synchronize/
type: docs
rightNav:
  hideH4: true
---

The following page describes the following export commands:

- [export data](#export-data) (and export data from source)
- [export data status](#export-data-status)
- [get data-migration-report](#get-data-migration-report)
- [export data from target](#export-data-from-target)

## export data

For offline migration, export data [dumps](../../../migrate/migrate-steps/#export-data) data of the source database in the `export-dir/data` directory on the machine where yb-voyager is running.

For [live migration](../../../migrate/live-migrate/#export-data-from-source) (with [fall-forward](../../../migrate/live-fall-forward/#export-data-from-source) and [fall-back](../../../migrate/live-fall-back/#export-data-from-source)), the export data command is an alias of `export data from source` which [dumps](../../../migrate/live-migrate/#export-data-from-source) the snapshot in the `data` directory and starts capturing the new changes made to the source database.

### Syntax

Syntax for export data is as follows:

```text
Usage: yb-voyager export data [ <arguments> ... ]
```

Syntax for export data from source is as follows:

```text
Usage: yb-voyager export data from source [ <arguments> ... ]
```

### Arguments

The valid *arguments* for export data are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| --disable-pb |Use this argument to disable progress bar during data export and statistics printing during streaming phase. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| -e, &#8209;&#8209;export&#8209;dir | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| --export-type | Specify the migration type as `snapshot-only` (offline) or `snapshot-and-changes` (live, with fall-forward and fall-back). <br>Default: `snapshot-only` |
| -h, --help | Command line help. |
| --oracle&#8209;cdb&#8209;name | Oracle Container Database Name in case you are using a multi-tenant container database. Required for Oracle live migrations only. |
| --oracle-cdb-sid | Oracle System Identifier (SID) of the Container Database that you wish to use while exporting data from Oracle instances. Required for Oracle live migrations only. |
| &#8209;&#8209;oracle&#8209;cdb&#8209;tns&#8209;alias | Name of TNS Alias you wish to use to connect to Oracle Container Database in case you are using a multi-tenant container database. Required for Oracle live migrations only. |
| --oracle-db-sid | Oracle System Identifier you can use while exporting data from Oracle instances. Oracle migrations only.|
| --oracle-home | Path to set `$ORACLE_HOME` environment variable. `tnsnames.ora` is found in `$ORACLE_HOME/network/admin`. Not applicable during import phases or analyze schema. Oracle migrations only.|
| [--oracle-tns-alias](../../yb-voyager-cli/#oracle-options) | TNS (Transparent Network Substrate) alias configured to establish a secure connection with the server. Oracle migrations only. |
| --parallel-jobs | Number of parallel jobs to extract data from source database. <br>Default: 4; exports 4 tables at a time by default. If you use [BETA_FAST_DATA_EXPORT](../../../migrate/migrate-steps/#accelerate-data-export-for-mysql-and-oracle) to accelerate data export, yb-voyager exports only one table at a time and the --parallel-jobs argument is ignored. |
| --send-diagnostics | Enable or disable sending [diagnostics](../../../diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --source-db-host <hostname> | Domain name or IP address of the machine on which the source database server is running. |
| --source-db-name | Source database name. |
| --source-db-password | Password to connect to the source database. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. Alternatively, you can also specify the password by setting the environment variable `SOURCE_DB_PASSWORD`. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose it in single quotes. |
| --source-db-port | Port number of the source database server. <br>Default: 5432 (PostgreSQL), 3306 (MySQL), and 1521 (Oracle) |
| --source-db-schema | Schema name of the source database. Not applicable for MySQL. For Oracle, you can specify only one schema name using this option. For PostgreSQL, you can specify a list of comma-separated schema names. Case-sensitive schema names are not yet supported. Refer to [Importing with case-sensitive schema names](../../../known-issues/general-issues/#importing-with-case-sensitive-schema-names) for more details. |
| --source-db-type | One of `postgresql`, `mysql`, or `oracle`. |
| --source-db-user | Username of the source database. |
| [--source-ssl-cert](../../yb-voyager-cli/#ssl-connectivity) | Path to a file containing the certificate which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-key](../../yb-voyager-cli/#ssl-connectivity) | Path to a file containing the key which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-crl](../../yb-voyager-cli/#ssl-connectivity) | Path to a file containing the SSL certificate revocation list (CRL).|
| [--source-ssl-mode](../../yb-voyager-cli/#ssl-connectivity) | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--source-ssl-root-cert](../../yb-voyager-cli/#ssl-connectivity) | Path to a file containing SSL certificate authority (CA) certificate(s). |
| --start-clean | Starts a fresh data export after clearing all data from the `data` directory. <br> Default: false <br> Accepted parameters: true, false, yes, no, 0, 1 |
| --table-list | Comma-separated list of the tables to export data. Table names can also be glob patterns containing wildcard characters, such as an asterisk (*) (matches zero or more characters) or question mark (?) (matches one character). To use a glob pattern for table names, enclose the list in single quotes ('').<br> For example, `--table-list '"Products", order*'`. |
| --exclude-table-list | Comma-separated list of the tables to exclude during export. Table names follow the same convention as `--table-list`. |
| --table-list-file-path | Path of the file containing the list of table names (comma-separated or line-separated) to export. Table names use the same convention as `--table-list`. |
| &#8209;&#8209;exclude&#8209;table&#8209;list&#8209;file&#8209;path | Path of the file containing the list of table names (comma-separated or line-separated) to exclude while exporting data. Table names follow the same convention as `--table-list`. |
| -y, --yes | Answer yes to all prompts during the export schema operation. <br>Default: false |

### Example

#### Offline migration

An example for offline migration is as follows:

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

#### Live migration

An example for all live migration scenarios is as follows:

```sh
yb-voyager export data from source --export-dir /dir/export-dir \
        --source-db-type oracle \
        --source-db-host 127.0.0.1 \
        --source-db-port 1521 \
        --source-db-user ybvoyager \
        --source-db-password 'password' \
        --source-db-name source_db \
        --source-db-schema source_schema \
        --export-type "snapshot-and-change"
```

## export data status

For offline migration, get the status report of an ongoing or completed data export operation.

### Syntax

```text
Usage: yb-voyager export data status [ <arguments> ... ]
```

### Arguments

The valid *arguments* for export data status are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| -e, &#8209;&#8209;export&#8209;dir | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help. |

### Example

```sh
yb-voyager export data status --export-dir /dir/export-dir
```

## get data-migration-report

**Note** that this command is applicable for Live migrations only.

Provides a consolidated report of data migration per table among all the databases involved in the live migration. The report includes the number of rows exported, the number of rows imported, change events exported and imported (INSERTS, UPDATES, and DELETES), and the final row count on the database.

### Syntax

```text
Usage: yb-voyager get data-migration-report [ <arguments> ... ]
```

### Arguments

The valid *arguments* for get data-migration-report are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| -e, &#8209;&#8209;export&#8209;dir | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help. |
| &#8209;&#8209;source&#8209;db&#8209;password | Password to connect to the source database. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. Alternatively, you can also specify the password by setting the environment variable `SOURCE_DB_PASSWORD`. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose it in single quotes. |
| &#8209;&#8209;source&#8209;replica&#8209;db&#8209;password | Password to connect to the source-replica database. Alternatively, you can also specify the password by setting the environment variable `SOURCE_REPLICA_DB_PASSWORD`. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose it in single quotes. |
| --target-db-password | Password to connect to the target YugabyteDB database. Alternatively, you can also specify the password by setting the environment variable `TARGET_DB_PASSWORD`. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes. |

### Example

```sh
yb-voyager get data-migration-report --export-dir /dir/export-dir
```

## export data from target

**Note** that this command is applicable for Live migrations only.

Exports new changes from the target YugabyteDB database to be imported to either the source database (for fall-back migration) or source-replica database (for fall-forward migration) to ensure synchronization between the source or source-replica database and the YugabyteDB database after the cutover.

### Syntax

```text
Usage: yb-voyager export data from target [ <arguments> ... ]
```

### Arguments

The valid *arguments* for export data from target are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| --disable-pb | Use this argument to disable the progress bar during data export and printing statistics during the streaming phase. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| -e, &#8209;&#8209;export&#8209;dir | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help for synchronize. |
| --send&#8209;diagnostics | Enable or disable sending [diagnostics](../../../diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --table-list | Comma-separated list of the tables to export data. Table names can also be glob patterns containing wildcard characters, such as an asterisk (*) (matches zero or more characters) or question mark (?) (matches one character). To use a glob pattern for table names, enclose the list in single quotes ('').<br> For example, `--table-list '"Products", order*'`. |
| --exclude-table-list | Comma-separated list of the tables to exclude during export. Table names follow the same convention as `--table-list`. |
| --table&#8209;list&#8209;file&#8209;path | Path of the file containing the list of table names (comma-separated or line-separated) to export. Table names use the same convention as `--table-list`. |
| &#8209;&#8209;exclude&#8209;table&#8209;list&#8209;file&#8209;path | Path of the file containing the list of table names (comma-separated or line-separated) to exclude while exporting data. Table names follow the same convention as `--table-list`. |
| --target-db-password | Password to connect to the target YugabyteDB database. Alternatively, you can also specify the password by setting the environment variable `TARGET_DB_PASSWORD`. If you don't provide a password via the CLI, yb-voyager will prompt you at runtime for a password. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes. |
| --target-ssl-mode | Specify the SSL mode for the target database as one of `disable`, `allow`, `prefer` (default), `require`, `verify-ca`, or `verify-full`. |
| --target-ssl-root-cert | Path to a file containing the target YugabyteDB SSL Root Certificate. |
| -y, --yes | Answer yes to all prompts during the export schema operation. <br>Default: false |

### Example

```sh
yb-voyager export data from target --export-dir /dir/export-dir \
        --target-db-password 'password'
```
