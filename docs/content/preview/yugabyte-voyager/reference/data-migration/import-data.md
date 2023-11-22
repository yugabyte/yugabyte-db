---
title: import data reference
headcontent: yb-voyager import data
linkTitle: import data
description: YugabyteDB Voyager import data reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-import-data
    parent: data-migration
    weight: 60
aliases:
  - /preview/yugabyte-voyager/reference/fall-forward/fall-forward-setup/
  - /preview/yugabyte-voyager/reference/fall-forward/fall-forward-switchover/
type: docs
rightNav:
  hideH4: true
---

The following page describes the usage of the following import commands:

- [import data](#import-data) (and import data to target)
- [import data status](#import-data-status)
- [get data-migration-report](#get-data-migration-report)
- [import data to source](#import-data-to-source)
- [import data to source-replica](#import-data-to-source-replica)

### import data

For offline migration, [Import the data](../../../migrate/migrate-steps/#import-data) to the YugabyteDB database.

For [live migration](../../../migrate/live-migrate/#import-data) (with [fall-forward](../../../migrate/live-fall-forward/#import-data) and [fall-back](../../../migrate/live-fall-back/#import-data)), the import data command is an alias of `import data to target` which [imports the data](../../../migrate/migrate-steps/#import-data) from the `data` directory to the target database, and starts ingesting the new changes captured by `export data` to the target database.

#### Syntax

Syntax for import data is as follows:

```text
Usage: yb-voyager import data [ <arguments> ... ]
```

Syntax for import data to target is as follows:

```text
Usage: yb-voyager import data to target [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for import data are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| --batch-size <number> | Size of batches in the number of rows generated for ingestion during import data. <br>Default: 20000 rows<br>Example: `yb-voyager import data ... --batch-size 20000` |
| --continue-on-error | Ignores the error while executing the DDLs for resuming sequences on target db after the data is imported. <br>Default: false<br> Usage: `yb-voyager import data ... --continue-on-error true`<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --disable-pb |Use this argument to disable progress bar or statistics during data import. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --enable-upsert | Enable UPSERT mode on target tables while importing data. <br>Default: true<br> Usage for disabling the mode: `yb-voyager import data ... --enable-upsert false`<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --table-list | Comma-separated list of the tables for which data needs to be imported. Table names in the list are by default, case-insensitive. To make it case-sensitive, enclose each name in double quotes("").<br>Table names can also be glob patterns containing wildcard characters such as an asterisk (*) (matches zero or more characters), and a question mark (?) (matches one character). If a glob pattern for tables or case-sensitive table name is to be provided in table-list/exclude-table-list, enclose the list in single quotes('').<br> For example, `--table-list '"Products", order*'`.<br> This argument is unsupported for live migration.|
| --exclude-table-list <tableNames> | Comma-separated list of the tables for which data needs to be excluded during import. Table names follow the same convention as `--table-list`. <br> This argument is unsupported for live migration. |
| --table-list-file-path | Path of the file containing the list of table names (comma separated or line separated) to import.  Table names use the same convention as `--table-list`. |
| --exclude-table-list-file-path |  Path of the file containing the list of table names (comma separated or line separated) to exclude while importing data of those exported tables. Table names follow the same convention as `--table-list`. |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file. |
| -h, --help | Command line help. |
| --parallel-jobs <connectionCount> | Number of parallel jobs to use while importing data. Depending on the YugabyteDB database configuration, the value of `--parallel-jobs` should be tweaked such that at most 50% of target cores are utilised. <br>Default: If yb-voyager can determine the total number of cores N in the YugabyteDB database cluster, it uses N/2 as the default. Otherwise, it defaults to twice the number of nodes in the cluster.|
| --send-diagnostics | Enable or disable sending [diagnostics](../../../diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --start-clean | Starts a fresh import with data files present in the `data` directory.<br>If the target YugabyteDB database has non-empty tables, you are prompted to continue the import without truncating those tables; if you go ahead without truncating, then yb-voyager starts ingesting the data present in the data files in upsert mode.<br> **Note** that for cases where a table doesn't have a primary key, duplicate data may be inserted. You can avoid duplication by excluding the table using `--exclude-table-list`, or by truncating those tables manually before using the `start-clean` flag. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --target-db-host <hostname> | Domain name or IP address of the machine on which target database server is running. <br>Default: "127.0.0.1" |
| --target-db-name <name> | Target database name. |
| --target-db-password <password>| Password to connect to the target YugabyteDB server. Alternatively, you can also specify the password by setting the environment variable `TARGET_DB_PASSWORD`. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes. |
| --target-db-port <port> | Port number of the target database machine. <br>Default: 5433 |
| --target-db-schema <schemaName> | Schema name of the target database. MySQL and Oracle migrations only. |
| --target-db-user <username> | Username of the target database. |
| --target-endpoints <nodeEndpoints> | Comma-separated list of node's endpoint to use for parallel import of data <br>Default: Use all the nodes in the cluster. For example: "host1:port1,host2:port2" or "host1,host2". Note: use-public-ip flag will be ignored if this is used. |
| --use-public-ip | Use the node public IP addresses to distribute `--parallel-jobs` uniformly on data import. <br>Default: false<br> **Note** that you may need to configure the database with public IP addresses by setting [server-broadcast-addresses](../../../../reference/configuration/yb-tserver/#server-broadcast-addresses).<br>Example: `yb-voyager import data ... --use-public-ip true`<br> Accepted parameters: true, false, yes, no, 0, 1 |
| [--target-ssl-cert](../../yb-voyager-cli/#ssl-connectivity) <certificateName> | Path to a file containing the certificate which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-key](../../yb-voyager-cli/#ssl-connectivity) <keyName> | Path to a file containing the key which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-crl](../../yb-voyager-cli/#ssl-connectivity) <path> | Path to a file containing the SSL certificate revocation list (CRL).|
| [--target-ssl-mode](../../yb-voyager-cli/#ssl-connectivity) <SSLmode> | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--target-ssl-root-cert](../../yb-voyager-cli/#ssl-connectivity) <path> | Path to a file containing SSL certificate authority (CA) certificate(s). |
| -y, --yes | Answer yes to all prompts during the export schema operation. <br>Default: false |

#### Example

##### Offline migration

An example for offline migration is as follows:

```sh
yb-voyager import data --export-dir /dir/export-dir \
        --target-db-host 127.0.0.1 \
        --target-db-user ybvoyager \
        --target-db-password 'password' \
        --target-db-name target_db \
        --target-db-schema target_schema \
        --parallel-jobs 12
```

##### Live migration

An example for all live migration scenarios is as follows:

```sh
yb-voyager import data to target--export-dir /dir/export-dir \
        --target-db-host 127.0.0.1 \
        --target-db-user ybvoyager \
        --target-db-password 'password' \
        --target-db-name target_db \
        --target-db-schema target_schema \
        --parallel-jobs 12
```

### import data status

For offline migration, get the status report of an ongoing or completed data import operation. The report contains migration status of tables, number of rows or bytes imported, and percentage completion.

#### Syntax

```text
Usage: yb-voyager import data status [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for import data status are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| -h, --help | Command line help. |
| -y, --yes | Answer yes to all prompts during the import data operation. <br>Default: false |

#### Example

```sh
yb-voyager import data status --export-dir /dir/export-dir
```

### get data-migration-report

**Note** that this command is applicable for Live migrations only.

Provides a consolidated report of data migration per table among all the databases involved in the live migration. The report includes the number of rows exported, the number of rows imported, change events exported and imported (INSERTS, UPDATES, and DELETES), and the final row count on the database.

#### Syntax

```text
Usage: yb-voyager get data-migration-report [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for get data-migration-report are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help. |
| --source-db-password <password>| Password to connect to the source database server. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. Alternatively, you can also specify the password by setting the environment variable `SOURCE_DB_PASSWORD`. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose it in single quotes. |
| --source-replica-db-password <password> | Password to connect to the source-replica database server. Alternatively, you can also specify the password by setting the environment variable `SOURCE_REPLICA_DB_PASSWORD`. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose it in single quotes. |
| --target-db-password <password>| Password to connect to the target YugabyteDB server. Alternatively, you can also specify the password by setting the environment variable `TARGET_DB_PASSWORD`. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes. |

#### Example

```sh
yb-voyager get data-migration-report --export-dir /dir/export-dir
```

### import data to source

**Note** that this command is applicable for Live migrations only.

Imports data to the source database, and streams new changes from the YugabyteDB database to the source database.

#### Syntax

```text
Usage: yb-voyager import data to source [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for import data to source are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help for import data to source. |
| --parallel-jobs | Number of parallel jobs to use while importing data. <br>Default: 16(Oracle) |
| --send-diagnostics | Enable or disable sending [diagnostics](../../../diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --source-db-password | Password to connect to the source database server. |
| --start-clean | Starts a fresh import with exported data files present in the export-dir/data directory. <br> If any table on YugabyteDB database is non-empty, it prompts whether you want to continue the import without truncating those tables; if you go ahead without truncating, then yb-voyager starts ingesting the data present in the data files with upsert mode. <br>Note that for the cases where a table doesn't have a primary key, this may lead to insertion of duplicate data. To avoid this, exclude the table using the --exclude-file-list or truncate those tables manually before using the start-clean flag. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| -y, --yes | Answer yes to all prompts during the migration. <br>Default: false |

#### Example

```sh
yb-voyager import data to source --export-dir /dir/export-dir \
        --source-db-password 'password'
```

### import data to source-replica

**Note** that this command is applicable for Live migrations only.

Imports data to the source-replica database, and streams new changes from the YugabyteDB database to the source-replica database.

#### Syntax

```text
Usage: yb-voyager import data to source-replica [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for import data to source-replica are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| --batch-size <number> | Size of batches in the number of rows generated for ingestion when you import data to source-replica database. <br> Default: 10,000,000 |
| --disable-pb |Use this argument to disable progress bar or statistics during data import. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| --source-replica-db-host <hostname> | Domain name or IP address of the machine on which source-replica database server is running. <br>Default: 127.0.0.1 |
| --source-replica-db-name <name> | Name of the database in the source-replica database server on which import needs to be done. |
| --source-replica-db-password <password> | Password to connect to the source-replica database server. Alternatively, you can also specify the password by setting the environment variable `SOURCE_REPLICA_DB_PASSWORD`. If you don't provide a password via the CLI or environment variable during any migration phase, yb-voyager will prompt you at runtime for a password. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes. |
| --source-replica-db-port <port> | Port number of the source-replica database server. <br>Default: 1521 (Oracle) |
| --source-replica-db-schema <schemaName> | Schema name of the source-replica database. |
| --source-replica-db-sid <SID> | Oracle System Identifier (SID) that you wish to use while importing data to Oracle instances. Oracle migrations only. |
| --source-replica-db-user <username>| Username to connect to the source-replica database server. |
| --source-replica-ssl-cert <path>| Source-replica database SSL Certificate path. |
| --source-replica-ssl-crl <list>| Source-replica database SSL Root Certificate Revocation List (CRL). |
| --source-replica-ssl-key <Keypath> | Source-replica database SSL key path. |
| --source-replica-ssl-mode <SSLmode>| One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| --source-replica-ssl-root-cert | Source-replica database SSL Root Certificate path. |
| -h, --help | Command line help for import data to source-replica. |
| --oracle-home <path> | Path to set $ORACLE_HOME environment variable. `tnsnames.ora` is found in `$ORACLE_HOME/network/admin`. Oracle migrations only.|
| [--oracle-tns-alias](../../yb-voyager-cli/#ssl-connectivity) <alias> | TNS (Transparent Network Substrate) alias configured to establish a secure connection with the server. Oracle migrations only. |
| --parallel-jobs <connectionCount> | The number of parallel batches issued to the source-replica database. <br> Default: 1 |
| --start-clean | Starts a fresh import with data files present in the `data` directory.<br>If there's any non-empty table on the target YugabyteDB database, you get a prompt whether to continue the import without truncating those tables; if you go ahead without truncating, then yb-voyager starts ingesting the data present in the data files with upsert mode.<br> **Note** that for cases where a table doesn't have a primary key, it may lead to insertion of duplicate data. In that case, you can avoid the duplication by excluding the table from the `--exclude-table-list`, or truncating those tables manually before using the `start-clean` flag. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --send-diagnostics | Enable or disable sending [diagnostics](../../../diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |
| -y, --yes | Answer yes to all prompts during the migration. <br>Default: false |

#### Example

```sh
yb-voyager import data to source-replica --export-dir /dir/export-dir \
        --source-replica-db-host 127.0.0.1 \
        --source-replica-db-user ybvoyager \
        --source-replica-db-password 'password' \
        --source-replica-db-name source_replica_db \
        --source-replica-db-schema source_replica_schema \
        --parallel-jobs 12
```
