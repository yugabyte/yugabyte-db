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
