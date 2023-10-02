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
| --batch-size <number> | Size of batches generated for ingestion during import data. (default: 20000 rows) |
| --continue-on-error | If set, this flag ignores errors and continues with the import. |
| --disable-pb | Use this argument to not display progress bars. For live migration, `--disable-pb` can also be used to hide metrics for import data. (default: false) |
| --enable-upsert | Set to true to enable UPSERT mode on target tables and false to disable the mode. (default true) |
| --table-list | Comma-separated list of the tables for which data is exported. Do not use in conjunction with `--exclude-table-list`. |
| --exclude-table-list <tableNames> | Comma-separated list of tables to exclude while exporting data. For import data command, the list of table names passed in the `--table-list` and `--exclude-table-list` are, by default, case sensitive. You don't need to enclose them in double quotes. For live migration, during import data, the `--exclude-table-list` argument is not supported. |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file. |
| -h, --help | Command line help. |
| --parallel-jobs <connectionCount> | Number of parallel COPY commands issued to the target database. Depending on the YugabyteDB database configuration, the value of `--parallel-jobs` should be tweaked such that at most 50% of target cores are utilised. (default: If yb-voyager can determine the total number of cores N in the YugabyteDB database cluster, it uses N/2 as the default. Otherwise, it defaults to twice the number of nodes in the cluster.)|
| --send-diagnostics | Send diagnostics information to Yugabyte. |
| --start-clean | Starts a fresh import with data files present in the `data` directory. If any table on the YugabyteDB database is not empty, you are prompted to continue the import without truncating those tables. If you continue, yb-voyager starts ingesting the data present in the data files with upsert mode, and for cases where a table doesn't have a primary key, it may duplicate the data. In this case, you should use the `--exclude-table-list` flag to exclude such tables, or truncate those tables manually before using the `start-clean` flag. |
| --target-db-host <hostname> | Domain name or IP address of the machine on which target database server is running. (default: "127.0.0.1") |
| --target-db-name <name> | Target database name. |
| --target-db-password <password>| Target database password. Alternatively, you can also specify the password by setting the environment variable `TARGET_DB_PASSWORD`. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes. |
| --target-db-port <port> | Port number of the target database machine. (default: 5433) |
| --target-db-schema <schemaName> | Schema name of the target database. MySQL and Oracle migrations only. |
| --target-db-sid <SID>  | Oracle System Identifier (SID) that you wish to use while importing data to Oracle instances. Oracle migrations only. |
| --target-db-user <username> | Username of the target database. |
| --target-endpoints <nodeEndpoints> | Comma-separated list of node's endpoint to use for parallel import of data (default is to use all the nodes in the cluster). For example: "host1:port1,host2:port2" or "host1,host2". Note: use-public-ip flag will be ignored if this is used. |
| --use-public-ip | Set to true to use the public IPs of the nodes to distribute --parallel-jobs uniformly for data import (default: false).<br> Note that you may need to configure database to have public_ip available by setting [server-broadcast-addresses](../../../../reference/configuration/yb-tserver/#server-broadcast-addresses). |
| [--target-ssl-cert](../../yb-voyager-cli/#ssl-connectivity) <certificateName> | Name of the certificate which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-key](../../yb-voyager-cli/#ssl-connectivity) <keyName> | Name of the key which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-crl](../../yb-voyager-cli/#ssl-connectivity) <path> | Path to a file containing the SSL certificate revocation list (CRL).|
| [--target-ssl-mode](../../yb-voyager-cli/#ssl-connectivity) <SSLmode> | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--target-ssl-root-cert](../../yb-voyager-cli/#ssl-connectivity) <path> | Path to a file containing SSL certificate authority (CA) certificate(s). |
| --verbose | Display extra information in the output. (default: false) |
| -y, --yes | Answer yes to all prompts during the export schema operation. (default: false) |

<!-- To do : document the following arguments with description
| --continue-on-error |
| --enable-upsert |
| --target-endpoints |
| --use-public-ip | -->
### Example

```sh
yb-voyager import data --export-dir /path/to/yb/export/dir \
        --target-db-host hostname \
        --target-db-user username \
        --target-db-password password \ # Enclose the password in single quotes if it contains special characters.
        --target-db-name dbname \
        --target-db-schema schemaName \ # MySQL and Oracle only
        --parallel-jobs connectionCount
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
| target-db-password | Password of the target database. Live migrations only. |
| ff-db-password | Password of the fall-forward database. Live migration with fall-forward only. |

### Example

```sh
yb-voyager import data status --export-dir /path/to/yb/export/dir
```
