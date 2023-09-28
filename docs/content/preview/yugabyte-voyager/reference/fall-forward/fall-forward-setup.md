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

#### Syntax

```text
Usage: yb-voyager fall-forward setup [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for fall-forward setup are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| --batch-size <number> | Size of batches generated for ingestion during import data. (Default: 10000000) |
| --continue-on-error | Ignores errors and continues data import. |
| --disable-pb | Use this argument to not display progress bars. For live migration, `--disable-pb` can also be used to hide metrics for export data. (default: false) |
| --enable-upsert | Set to true to enable UPSERT mode on target tables, and false to disable UPSERT mode on target tables. (default: true) |
| --exclude-table-list <tableNames> | Comma-separated list of tables to exclude while migrating data (ignored if `--table-list` is used). The `exclude-table-list` argument is not supported during import data during live migration.|
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| --ff-db-host <hostname> | Domain name or IP address of the machine on which Fall-forward database server is running. (default: "127.0.0.1") |
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
| [--oracle-tns-alias](#ssl-connectivity) <alias> | TNS (Transparent Network Substrate) alias configured to establish a secure connection with the server. Oracle migrations only. |
| --parallel-jobs <connectionCount> | Number of parallel COPY commands issued to the target database. (default: 1) |
| --send-diagnostics | Send diagnostics information to Yugabyte. (Default: true) |
| --start-clean | Starts a fresh import with data files present in the `data` directory and if any table on fall- forward database is non-empty, it prompts whether you want to continue the import without the truncating those tables; if yes, then yb-voyager starts ingesting the data present in the data files without upsert mode and for the cases where a table doesn't have a primary key, it may duplicate the data. So, in that case, use `--exclude-table-list` flag to exclude such tables or truncate those tables manually before using the `start-clean` flag. |
| --table-list | Comma-separated list of the tables to import data. Do not use in conjunction with `--exclude-table-list.` |
| --verbose | Display extra information in the output. (default: false) |
| -y, --yes | Answer yes to all prompts during the migration. (default: false) |

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
