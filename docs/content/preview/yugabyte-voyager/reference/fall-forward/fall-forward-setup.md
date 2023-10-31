---
title: fall-forward setup reference
headcontent: yb-voyager fall-forward setup
linkTitle: fall-forward setup
description: YugabyteDB Voyager fall-forward setup reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-fall-forward-setup
    parent: fall-forward
    weight: 90
type: docs
---

Imports data to the fall-forward database, and streams new changes from the YugabyteDB database to the fall-forward database.

### Syntax

```text
Usage: yb-voyager fall-forward setup [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for fall-forward setup are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| --batch-size <number> | Size of batches in the number of rows generated for ingestion when you import data to fall forward database. (default: 10,000,000) |
| --disable-pb | Use this argument to not display progress bars. For live migration, `--disable-pb` can also be used to hide metrics for export data. (default: false) <br> Accepted parameters: true, false, yes, no, 0, 1 |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| --ff-db-host <hostname> | Domain name or IP address of the machine on which Fall-forward database server is running. (default: 127.0.0.1) |
| --ff-db-name <name> | Name of the database in the fall-forward database server on which import needs to be done. |
| --ff-db-password <password> | Password to connect to the fall-forward database server. Alternatively, you can also specify the password by setting the environment variable `FF_DB_PASSWORD`. If you don't provide a password via the CLI or environment variable during any migration phase, yb-voyager will prompt you at runtime for a password. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes. |
| --ff-db-port <port> | Port number of the fall-forward database server. (default: 1521 (Oracle)) |
| --ff-db-schema <schemaName> | Schema name of the fall-forward database. |
| --ff-db-sid <SID> | Oracle System Identifier (SID) that you wish to use while importing data to Oracle instances. Oracle migrations only. |
| --ff-db-user <username>| Username to connect to the fall-forward database server. |
| --ff-ssl-cert <path>| Fall-forward database SSL Certificate path. |
| --ff-ssl-crl <list>| Fall-forward database SSL Root Certificate Revocation List (CRL). |
| --ff-ssl-key <Keypath> | Fall-forward database SSL key path. |
| --ff-ssl-mode <SSLmode>| One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| --ff-ssl-root-cert | Fall-forward database SSL Root Certificate path. |
| -h, --help | Command line help for setup. |
| --oracle-home <path> | Path to set $ORACLE_HOME environment variable. `tnsnames.ora` is found in `$ORACLE_HOME/network/admin`. Oracle migrations only.|
| [--oracle-tns-alias](../../yb-voyager-cli/#ssl-connectivity) <alias> | TNS (Transparent Network Substrate) alias configured to establish a secure connection with the server. Oracle migrations only. |
| --parallel-jobs <connectionCount> | The number of parallel batches issued to the fall forward database. (default: 1) |
| --send-diagnostics | Send [diagnostics](../../../diagnostics-report/) information to Yugabyte. (default: true)<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --start-clean | Starts a fresh import with data files present in the `data` directory.<br>If there's any non-empty table on the target YugabyteDB database, you get a prompt whether to continue the import without truncating those tables; if you go ahead without truncating, then yb-voyager starts ingesting the data present in the data files with upsert mode.<br> **Note** that for cases where a table doesn't have a primary key, it may lead to insertion of duplicate data. In that case, you can avoid the duplication by excluding the table from the `--exclude-table-list`, or truncating those tables manually before using the `start-clean` flag. <br> Accepted parameters: true, false, yes, no, 0, 1 |
| --verbose | Display extra information in the output. (default: false)<br> Accepted parameters: true, false, yes, no, 0, 1 |
| -y, --yes | Answer yes to all prompts during the migration. (default: false) |

### Example

```sh
yb-voyager fall-forward setup --export-dir /dir/export-dir \
        --ff-db-host 127.0.0.1 \
        --ff-db-user ybvoyager \
        --ff-db-password 'password' \
        --ff-db-name ff_db \
        --ff-db-schema ff_schema \
        --parallel-jobs 12
```
