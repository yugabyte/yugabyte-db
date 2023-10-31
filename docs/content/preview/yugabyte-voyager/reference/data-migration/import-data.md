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
type: docs
---

This page describes the usage of the following import commands:

- [import data](#import-data)
- [import data status](#import-data-status)

## import data

For offline migration, [Import the data](../../../migrate/migrate-steps/#import-data) to the YugabyteDB database.

For live migration (and fall-forward), the command [imports the data](../../../migrate/migrate-steps/#import-data) from the `data` directory to the target database, and starts ingesting the new changes captured by `export data` to the target database.

### Syntax

```text
yb-voyager import data [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for import data are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| --batch-size <number> | Size of batches in the number of rows generated for ingestion during import data. (default: 20000 rows)<br>Example: `yb-voyager import data ... --batch-size 20000` |
| --continue-on-error | Ignores the error while executing the DDLs for resuming sequences on target db after the data is imported. (default: false) |
| --disable-pb | Turn off the display of progress bars. For live migration, `--disable-pb` can also be used to hide metrics for import data. (default: false) |
| --enable-upsert | Enable UPSERT mode on target tables while importing data. (default: true)<br> Usage for disabling the mode: `yb-voyager import data ... --enable-upsert=false` |
| --table-list | Comma-separated list of the tables for which data needs to be imported. Table names in the list are by default, case-insensitive. To make it case-sensitive, enclose each name in double quotes("").<br>Table names can also be glob patterns containing wildcard characters such as an asterisk (*) (matches zero or more characters), and a question mark (?) (matches one character). If a glob pattern for tables or case-sensitive table name is to be provided in table-list/exclude-table-list, enclose the list in single quotes('').<br> For example,  `--table-list '"Products", order*'`.<br> This argument is unsupported for live migration.|
| --table-list-file-path | Path of the file containing a list of table names (comma-separated or line-separated) to import data of the exported tables. Table names follow a similar convention as `--table-list`. |
| --exclude-table-list <tableNames> | Comma-separated list of the tables for which data needs to be excluded during import. Table names in the list are by default, case-insensitive. To make it case-sensitive, enclose each name in double quotes("").<br> Table names can also be glob patterns containing wildcard characters such as an asterisk (*) (matches zero or more characters), and a question mark (?) (matches one character). If a glob pattern for tables or case-sensitive table name is to be provided in table-list/exclude-table-list, enclose the list in single quotes('').<br> For example, `--table-list '"Order*", products, '`. <br> This argument is unsupported for live migration. |
| --exclude-table-list-file-path | Path of the file containing the list of table names (comma-separated or line-separated) to exclude import data of the exported tables. Table names follow a similar convention as `--exclude-table-list`.|
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file. |
| -h, --help | Command line help. |
| --parallel-jobs <connectionCount> | Number of parallel COPY commands issued to the target database. Depending on the YugabyteDB database configuration, the value of `--parallel-jobs` should be tweaked such that at most 50% of target cores are utilised. (default: If yb-voyager can determine the total number of cores N in the YugabyteDB database cluster, it uses N/2 as the default. Otherwise, it defaults to twice the number of nodes in the cluster.)|
| --send-diagnostics | Send [diagnostics](../../../diagnostics-report/) information to Yugabyte. (default: true)<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --start-clean | Starts a fresh import with data files present in the `data` directory.<br>If there's any non-empty table on the target YugabyteDB database, you get a prompt whether to continue the import without truncating those tables; if you go ahead without truncating, then yb-voyager starts ingesting the data present in the data files with upsert mode.<br> **Note** that for cases where a table doesn't have a primary key, it may lead to insertion of duplicate data. In that case, you can avoid the duplication by excluding the table from the `--exclude-table-list`, or truncating those tables manually before using the `start-clean` flag. |
| --target-db-host <hostname> | Domain name or IP address of the machine on which target database server is running. (default: "127.0.0.1") |
| --target-db-name <name> | Target database name. |
| --target-db-password <password>| Target database password. Alternatively, you can also specify the password by setting the environment variable `TARGET_DB_PASSWORD`. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes. |
| --target-db-port <port> | Port number of the target database machine. (default: 5433) |
| --target-db-schema <schemaName> | Schema name of the target database. MySQL and Oracle migrations only. |
| --target-db-user <username> | Username of the target database. |
| --target-endpoints <nodeEndpoints> | Comma-separated list of node's endpoint to use for parallel import of data (default is to use all the nodes in the cluster). For example: "host1:port1,host2:port2" or "host1,host2". Note: use-public-ip flag will be ignored if this is used. |
| --use-public-ip | Suggests voyager to use the public IPs of the nodes to distribute --parallel-jobs uniformly for data import (default: false).<br> Note that you may need to configure database to have public IP available by setting [server-broadcast-addresses](../../../../reference/configuration/yb-tserver/#server-broadcast-addresses).<br>Usage: `yb-voyager import data ...  --use-public-ip` |
| [--target-ssl-cert](../../yb-voyager-cli/#ssl-connectivity) <certificateName> | Name of the certificate which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-key](../../yb-voyager-cli/#ssl-connectivity) <keyName> | Name of the key which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-crl](../../yb-voyager-cli/#ssl-connectivity) <path> | Path to a file containing the SSL certificate revocation list (CRL).|
| [--target-ssl-mode](../../yb-voyager-cli/#ssl-connectivity) <SSLmode> | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--target-ssl-root-cert](../../yb-voyager-cli/#ssl-connectivity) <path> | Path to a file containing SSL certificate authority (CA) certificate(s). |
| --verbose | Display extra information in the output. (default: false) |
| -y, --yes | Answer yes to all prompts during the export schema operation. (default: false) |

### Example

```sh
yb-voyager import data --export-dir /dir/export-dir \
        --target-db-host 127.0.0.1 \
        --target-db-user ybvoyager \
        --target-db-password 'password' \
        --target-db-name target_db \
        --target-db-schema target_schema \
        --parallel-jobs 12
```

## import data status

For offline migration, get the status report of an ongoing or completed data import operation. The report contains migration status of tables, number of rows or bytes imported, and percentage completion.

For live migration, get the status report of import data. For live migration with fall forward, the report also includes the status of fall forward setup. The report includes the status of tables, the number of rows imported, the total number of changes imported, the number of `INSERT`, `UPDATE`, and `DELETE` events, and the final row count of the target or fall-forward database.

### Syntax

```text
Usage: yb-voyager import data status [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for import data status are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| -h, --help | Command line help. |
| target-db-password | Target database password. Alternatively, you can also specify the password by setting the environment variable `TARGET_DB_PASSWORD`. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes. Required for live migrations only. |
| ff-db-password | Password to connect to the fall-forward database server. Alternatively, you can also specify the password by setting the environment variable `FF_DB_PASSWORD`. If you don't provide a password via the CLI or environment variable during any migration phase, yb-voyager will prompt you at runtime for a password. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes. Required for live migration with fall forward only. |
| --send-diagnostics | Send [diagnostics](../../../diagnostics-report/) information to Yugabyte. (default: true)<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --verbose | Display extra information in the output. (default: false) |
| -y, --yes | Answer yes to all prompts during the import data operation. (default: false) |

### Example

```sh
yb-voyager import data status --export-dir /dir/export-dir
```
