---
title: export data reference
headcontent: yb-voyager export data
linkTitle: export data
description: YugabyteDB Voyager export data reference
menu:
  stable_yugabyte-voyager:
    identifier: voyager-export-data
    parent: data-migration
    weight: 20
aliases:
  - /stable/yugabyte-voyager/reference/fall-forward/fall-forward-synchronize/
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

The following table lists the valid CLI flags and parameters for `export data` command.

When run at the same time, flags take precedence over configuration flag settings.

{{<table>}}

| <div style="width:150px">CLI flag</div> | Config file parameter | Description |
| :--- | :-------- | :---------- |

| --run-guardrails-checks |

```yaml{.nocopy}
export-data:
  run-guardrails-checks:
```

| Run guardrails checks during migration. <br>Default: true<br>Accepted values: true, false, yes, no, 0, 1 |

| --disable-pb |

```yaml{.nocopy}
export-data:
  disable-pb:
```

|Use this argument to disable progress bar during data export and statistics printing during streaming phase. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --export-type |

```yaml{.nocopy}
export-data:
  export-type:
```

| Specify the migration type as `snapshot-only` (offline) or `snapshot-and-changes` (live, with fall-forward and fall-back). <br>Default: `snapshot-only` |
| --parallel-jobs |

```yaml{.nocopy}
export-data:
  parallel-jobs:
```

| Number of parallel jobs to extract data from source database. <br>Default: 4; exports 4 tables at a time by default. If you use [BETA_FAST_DATA_EXPORT](../../../migrate/migrate-steps/#accelerate-data-export-for-mysql-and-oracle) to accelerate data export, yb-voyager exports only one table at a time and the --parallel-jobs argument is ignored. |
| --table-list |

```yaml{.nocopy}
export-data:
  table-list:
```

| Comma-separated list of the tables to export data. Table names can also be glob patterns containing wildcard characters, such as an asterisk (\\*) (matches zero or more characters) or question mark (?) (matches one character). To use a glob pattern for table names, enclose the list in single quotes ('').<br> For example, `--table-list '"Products", order*'`. |
| --exclude-table-list |

```yaml{.nocopy}
export-data:
  exclude-table-list:
```

| Comma-separated list of the tables to exclude during export. Table names follow the same convention as `--table-list`. |
| --table-list-file-path |

```yaml{.nocopy}
export-data:
  table-list-file-path:
```

| Path of the file containing the list of table names (comma-separated or line-separated) to export. Table names use the same convention as `--table-list`. |
| --exclude-table-list-file-path |

```yaml{.nocopy}
export-data:
  exclude-table-list-file-path:
```

| Path of the file containing the list of table names (comma-separated or line-separated) to exclude while exporting data. Table names follow the same convention as `--table-list`. |

| --allow-oracle-clob-data-export |

```yaml{.nocopy}
export-data:
  allow-oracle-clob-data-export:
```

| [Experimental] Allow exporting data of CLOB columns in offline migration. Oracle migrations only. The flag is _not supported_ for live migrations or [BETA_FAST_DATA_EXPORT](../../../migrate/migrate-steps/#accelerate-data-export-for-mysql-and-oracle). <br> Default: false <br> Accepted parameters: true, false |

| -e, --export-dir |

```yaml{.nocopy}
export-dir:
```

| Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|

| --send-diagnostics |

```yaml{.nocopy}
send-diagnostics:
```

| Enable or disable sending [diagnostics](../../../reference/diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |

| -l, --log-level |

```yaml {.nocopy}
log-level:
```

| Log level for yb-voyager. <br>Accepted values: trace, debug, info, warn, error, fatal, panic <br>Default: info |
| --source-db-host |

```yaml{.nocopy}
source:
  db-host:
```

| Domain name or IP address of the machine on which the source database server is running. |
| --source-db-name |

```yaml{.nocopy}
source:
  db-name:
```

| Source database name. |
| --source-db-password |

```yaml{.nocopy}
source:
  db-password:
```

| Password to connect to the source database. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. Alternatively, you can also specify the password by setting the environment variable `SOURCE_DB_PASSWORD`. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose it in single quotes. |
| --source-db-port |

```yaml{.nocopy}
source:
  db-port:
```

| Port number of the source database server. <br>Default: 5432 (PostgreSQL), 3306 (MySQL), and 1521 (Oracle) |
| --source-db-schema |

```yaml{.nocopy}
source:
  db-schema:
```

| Schema name of the source database. Not applicable for MySQL. For Oracle, you can specify only one schema name using this option. For PostgreSQL, you can specify a list of comma-separated schema names. Case-sensitive schema names are not yet supported. Refer to [Schema review workarounds](../../../known-issues/#schema-review) for the appropriate database type for more details. |
| --source-db-user |

```yaml{.nocopy}
source:
  db-user:
```

| Username of the source database. |
| --source-db-type |

```yaml{.nocopy}
source:
  db-type:
```

| One of `postgresql`, `mysql`, or `oracle`. |
| [--source-ssl-cert](../../yb-voyager-cli/#ssl-connectivity) |

```yaml{.nocopy}
source:
  ssl-cert:
```

| Path to a file containing the certificate which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-key](../../yb-voyager-cli/#ssl-connectivity) |

```yaml{.nocopy}
source:
  ssl-key:
```

| Path to a file containing the key which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-crl](../../yb-voyager-cli/#ssl-connectivity) |

```yaml{.nocopy}
source:
  ssl-crl:
```

| Path to a file containing the SSL certificate revocation list (CRL).|
| [--source-ssl-mode](../../yb-voyager-cli/#ssl-connectivity) |

```yaml{.nocopy}
source:
  ssl-mode:
```

| One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--source-ssl-root-cert](../../yb-voyager-cli/#ssl-connectivity) |

```yaml{.nocopy}
source:
  ssl-root-cert:
```

| Path to a file containing SSL certificate authority (CA) certificate(s). |
| --oracle-cdb-name |

```yaml{.nocopy}
source:
  oracle-cdb-name:
```

| Oracle Container Database Name in case you are using a multi-tenant container database. Required for Oracle live migrations only. |
| --oracle-cdb-sid |

```yaml{.nocopy}
source:
  oracle-cdb-sid:
```

| Oracle System Identifier (SID) of the Container Database that you wish to use while exporting data from Oracle instances. Required for Oracle live migrations only. |
| --oracle-cdb-tns-alias |

```yaml{.nocopy}
source:
  oracle-cdb-tns-alias:
```

| Name of TNS Alias you wish to use to connect to Oracle Container Database in case you are using a multi-tenant container database. Required for Oracle live migrations only. |
| --oracle-db-sid |

```yaml{.nocopy}
source:
  oracle-db-sid:
```

| Oracle System Identifier you can use while exporting data from Oracle instances. Oracle migrations only.|
| --oracle-home |

```yaml{.nocopy}
source:
  oracle-home:
```

| Path to set `$ORACLE_HOME` environment variable. `tnsnames.ora` is found in `$ORACLE_HOME/network/admin`. Not applicable during import phases or analyze schema. Oracle migrations only.|
| [--oracle-tns-alias](../../yb-voyager-cli/#oracle-options) |

```yaml{.nocopy}
source:
  oracle-tns-alias:
```

| TNS (Transparent Network Substrate) alias configured to establish a secure connection with the server. Oracle migrations only. |

| --start-clean | — | Starts a fresh data export after clearing all data from the `data` directory. <br> Default: false <br> Accepted parameters: true, false, yes, no, 0, 1 |
| -h, --help | — | Command line help. |
| -y, --yes | — | Answer yes to all prompts during the export schema operation. <br>Default: false |
| -c, --config-file | — | Path to a [configuration file](../../configuration-file). |

{{</table>}}

### Example

#### Offline migration

Configuration file:

```yaml
yb-voyager export data --config-file <path-to-config-file>
```

CLI:

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

Configuration file:

```yaml
yb-voyager export data from source --config-file <path-to-config-file>
```

CLI:

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

The following table lists the valid CLI flags and parameters for `export data status` command.

When run at the same time, flags take precedence over configuration flag settings.

{{<table>}}

| <div style="width:150px">CLI flag</div> | Config file parameter | Description |
| :--- | :-------- | :---------- |
| -e, --export-dir |

```yaml{.nocopy}
export-dir:
```

| Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | — | Command line help. |
| --output-format | — | Format for the status report. <br>Accepted parameters: <ul><li> `json`: Generate a JSON format output file.</li><li> `table` (Default): Output the report to the console.</li></ul> |

{{</table>}}

### Example

Configuration file:

```yaml
yb-voyager export data status --config-file <path-to-config-file>
```

CLI:

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

The following table lists the valid CLI flags and parameters for `get data-migration-report` command.

When run at the same time, flags take precedence over configuration flag settings.

{{<table>}}

| <div style="width:150px">CLI flag</div> | Config file parameter | Description |
| :--- | :-------- | :---------- |
| -e, --export-dir |

```yaml{.nocopy}
export-dir:
```

|Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|

| -l, --log-level |

```yaml {.nocopy}
log-level:
```

| Log level for yb-voyager. <br>Accepted values: trace, debug, info, warn, error, fatal, panic <br>Default: info |
| --source-db-password |

```yaml{.nocopy}
source:
  db-password:
```

|Password to connect to the source database. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. Alternatively, you can also specify the password by setting the environment variable `SOURCE_DB_PASSWORD`. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose it in single quotes. |
| --source-replica-db-password |

```yaml{.nocopy}
source-replica:
  replica-db-password:
```

|Password to connect to the source-replica database. Alternatively, you can also specify the password by setting the environment variable `SOURCE_REPLICA_DB_PASSWORD`. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose it in single quotes. |
| --target-db-password |

```yaml{.nocopy}
target:
  db-password:
```

|Password to connect to the target YugabyteDB database. Alternatively, you can also specify the password by setting the environment variable `TARGET_DB_PASSWORD`. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes. |
| -h, --help | — | Command line help. |
| --output-format | — | Format for the status report. <br>Accepted parameters: <ul><li> `json`: Generate a JSON format output file.</li><li> `table` (Default): Output the report to the console.</li></ul> |
| -c, --config-file | — | Path to a [configuration file](../../configuration-file). |

{{</table>}}

### Example

Configuration file:

```yaml
yb-voyager get data-migration-report --config-file <path-to-config-file>
```

CLI:

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

{{<table>}}

| <div style="width:150px">CLI flag</div> | Config file parameter | Description |
| :------- | :-------- | :------------------------ |
| --disable-pb |

```yaml{.nocopy}
export-data-from-target:
  disable-pb:
```

| Use this argument to disable the progress bar during data export and printing statistics during the streaming phase. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --table-list |

```yaml{.nocopy}
export-data-from-target:
  table-list:
```

| Comma-separated list of the tables to export data. Table names can also be glob patterns containing wildcard characters, such as an asterisk (\\*) (matches zero or more characters) or question mark (?) (matches one character). To use a glob pattern for table names, enclose the list in single quotes ('').<br> For example, `--table-list '"Products", order*'`. |
| --exclude-table-list |

```yaml{.nocopy}
export-data-from-target:
  exclude-table-list:
```

| Comma-separated list of the tables to exclude during export. Table names follow the same convention as `--table-list`. |
| --table-list-file-path |

```yaml{.nocopy}
export-data-from-target:
  table-list-file-path:
```

| Path of the file containing the list of table names (comma-separated or line-separated) to export. Table names use the same convention as `--table-list`. |
| --exclude-table-list-file-path |

```yaml{.nocopy}
export-data-from-target:
  exclude-table-list-file-path:
```

| Path of the file containing the list of table names (comma-separated or line-separated) to exclude while exporting data. Table names follow the same convention as `--table-list`. |
| -e, --export-dir |

```yaml{.nocopy}
export-dir:
```

| Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| --send-diagnostics |

```yaml{.nocopy}
send-diagnostics:
```

| Enable or disable sending [diagnostics](../../../reference/diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |

| -l, --log-level |

```yaml {.nocopy}
log-level:
```

| Log level for yb-voyager. <br>Accepted values: trace, debug, info, warn, error, fatal, panic <br>Default: info |
| --target-db-password |

```yaml{.nocopy}
target:
  db-password:
```

| Password to connect to the target YugabyteDB database. Alternatively, you can also specify the password by setting the environment variable `TARGET_DB_PASSWORD`. If you don't provide a password via the CLI, yb-voyager will prompt you at runtime for a password. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes. |
| [--target-ssl-mode](../../yb-voyager-cli/#ssl-connectivity) |

```yaml{.nocopy}
target:
  ssl-mode:
```

| Determines whether an encrypted connection is established between yb-voyager and the database server; and whether the certificate of the database server is verified from a CA.

For a list of valid options, refer to [SSL mode options](#ssl-mode-options). |

| [--target-ssl-root](../../yb-voyager-cli/#ssl-connectivity) |

```yaml{.nocopy}
target:
  ssl-root:
```

| Path to a file containing the target YugabyteDB SSL Root Certificate. If the target cluster has SSL enabled, this flag is required. |
| -h, --help | — | Command line help for synchronize. |
| -y, --yes | — | Answer yes to all prompts during the export schema operation. <br>Default: false |
| -c, --config-file | — | Path to a [configuration file](../../configuration-file). |

{{</table>}}

#### SSL mode options

The available SSL modes are as follows:

- [YugabyteDB gRPC Connector](../../../../additional-features/change-data-capture/using-yugabytedb-grpc-replication/debezium-connector-yugabytedb/)
  - disable: Only try a non-SSL connection.
  - require: Only try an SSL connection; fail if that can't be established.
  - verify-ca: Only try an SSL connection; verify the server TLS certificate against the configured CA certificates, and fail if no valid matching CA certificate is found.
- [YugabyteDB Connector](../../../../additional-features/change-data-capture/using-logical-replication/yugabytedb-connector/):
  - disable: Uses an unencrypted connection.
  - allow: Tries an unencrypted connection first and, if it fails, attempts a secure (encrypted) connection.
  - prefer: Tries a secure (encrypted) connection first and, if it fails, falls back to an unencrypted connection.
  - require: Uses a secure (encrypted) connection; fails if one cannot be established.
  - verify-ca: Same as require, but also verifies the server's TLS certificate against the configured Certificate Authority (CA) certificates. Fails if no valid matching CA certificates are found.
  - verify-full: Same as verify-ca, but also verifies that the server's certificate matches the host the connector is connecting to.

### Example

Configuration file:

```yaml
yb-voyager export data from target --config-file <path-to-config-file>
```

CLI:

```sh
yb-voyager export data from target --export-dir /dir/export-dir \
        --target-db-password 'password'
```
