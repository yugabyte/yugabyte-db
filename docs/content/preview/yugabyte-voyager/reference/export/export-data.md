---
title: export data reference
headcontent: yb-voyager export data
linkTitle: export data
description: YugabyteDB Voyager export data reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-export-data
    parent: export
    weight: 20
type: docs
---

For offline migration, export data [dumps](../../migrate/migrate-steps/#export-data) the source database to the machine where yb-voyager is installed.

For [live migration](../../migrate/live-migrate/#export-data) (and [fall-forward](../../migrate/live-fall-forward/#export-data)), export data [dumps](../../migrate/live-migrate/#export-data) the snapshot in the `data` directory and starts capturing the new changes made to the source database.

#### Syntax

```text
Usage: yb-voyager export data [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for export data are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [--disable-pb](#disable-pb) | Hide progress bars. |
| [--table-list](#table-list) | Comma-separated list of the tables for which data is exported. |
| [--exclude-table-list](#exclude-table-list) <tableNames> | Comma-separated list of tables to exclude while exporting data. |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [--export-type](#export-type) | Choose migration type between `snapshot-only` (default) or `snapshot-and-changes` |
| [-h, --help](#command-line-help) | Command line help. |
| [--oracle-db-sid](#oracle-db-sid) <SID> | Oracle System Identifier. Oracle migrations only. |
| [--oracle-home](#oracle-home) <path> | Path to set `$ORACLE_HOME` environment variable. Oracle migrations only.|
| [--oracle-tns-alias](#ssl-connectivity) <alias> | TNS (Transparent Network Substrate) alias configured to establish a secure connection with the server. Oracle migrations only. |
| [--parallel-jobs](#parallel-jobs) <connectionCount> | Number of parallel jobs to extract data from source database. (Default: 4) |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. |
| [--source-db-type](#source-db-type) <databaseType> | One of `postgresql`, `mysql`, or `oracle`. |
| [--source-db-host](#source-db-host) <hostname> | Hostname of the source database server. |
| [--source-db-name](#source-db-name) <name> | Source database name. |
| [--source-db-password](#source-db-password) <password>| Source database password. |
| [--source-db-port](#source-db-port) <port> | Port number of the source database machine. |
| [--source-db-schema](#source-db-schema) <schemaName> | Schema name of the source database. |
| [--source-db-user](#source-db-user) <username> | Username of the source database. |
| [--source-ssl-cert](#ssl-connectivity) <certificateName> | Name of the certificate which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-key](#ssl-connectivity) <keyName> | Name of the key which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-crl](#ssl-connectivity) <path> | Path to a file containing the SSL certificate revocation list (CRL).|
| [--source-ssl-mode](#ssl-connectivity) <SSLmode> | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--source-ssl-root-cert](#ssl-connectivity) <path> | Path to a file containing SSL certificate authority (CA) certificate(s). |
| [--start-clean](#start-clean) | Starts a fresh data export after clearing all data from the `data` directory.  |
| [-y, --yes](#yes) | Answer yes to all prompts during the export schema operation. |

#### Example

```sh
yb-voyager export data --export-dir /path/to/yb/export/dir \
        --source-db-type sourceDB \
        --source-db-host hostname \
        --source-db-port port \
        --source-db-user username \
        --source-db-password password \ # Enclose the password in single quotes if it contains special characters.
        --source-db-name dbname \
        --source-db-schema schemaName # Not applicable for MySQL
```
