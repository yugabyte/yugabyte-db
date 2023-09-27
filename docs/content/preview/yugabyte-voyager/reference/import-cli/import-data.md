---
title: import data reference
headcontent: yb-voyager import data
linkTitle: import data
description: YugabyteDB Voyager import data reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-import-data
    parent: import-cli
    weight: 60
type: docs
---

For offline migration, [Import the data](../../migrate/migrate-steps/#import-data) to the YugabyteDB database.

For live migration (and fall-forward), the command [imports the data](../../migrate/migrate-steps/#import-data) from the `data` directory to the target database, and starts ingesting the new changes captured by `export data` to the target database.

#### Syntax

```text
yb-voyager import data [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for import data are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [--batch-size](#batch-size) <number> | Size of batches generated for ingestion during [import data]. |
| [--disable-pb](#disable-pb) | Hide progress bars. |
| [--table-list](#table-list) | Comma-separated list of the tables for which data is exported. |
| [--exclude-table-list](#exclude-table-list) <tableNames> | Comma-separated list of tables to exclude while exporting data. |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help. |
| [--parallel-jobs](#parallel-jobs) <connectionCount> | Number of parallel COPY commands issued to the target database. |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. |
| [--start-clean](#start-clean) | Starts a fresh import with data files present in the `data` directory. If any table on the YugabyteDB database is not empty, you are prompted to continue the import without truncating those tables. If you continue, yb-voyager starts ingesting the data present in the data files with upsert mode, and for cases where a table doesn't have a primary key, it may duplicate the data. In this case, you should use the `--exclude-table-list` flag to exclude such tables, or truncate those tables manually before using the `start-clean` flag. |
| [--target-db-host](#target-db-host) <hostname> | Hostname of the target database server. |
| [--target-db-name](#target-db-name) <name> | Target database name. |
| [--target-db-password](#target-db-password) <password>| Target database password. |
| [--target-db-port](#target-db-port) <port> | Port number of the target database machine. |
| [--target-db-schema](#target-db-schema) <schemaName> | Schema name of the target database. |
| [--target-db-user](#target-db-user) <username> | Username of the target database. |
| [--target-ssl-cert](#ssl-connectivity) <certificateName> | Name of the certificate which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-key](#ssl-connectivity) <keyName> | Name of the key which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-crl](#ssl-connectivity) <path> | Path to a file containing the SSL certificate revocation list (CRL).|
| [--target-ssl-mode](#ssl-connectivity) <SSLmode> | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--target-ssl-root-cert](#ssl-connectivity) <path> | Path to a file containing SSL certificate authority (CA) certificate(s). |
| [--verbose](#verbose) | Display extra information in the output. |
| [-y, --yes](#yes) | Answer yes to all prompts during the export schema operation. |

<!-- To do : document the following arguments with description
| --continue-on-error |
| --enable-upsert |
| --target-endpoints |
| --use-public-ip | -->
#### Example

```sh
yb-voyager import data --export-dir /path/to/yb/export/dir \
        --target-db-host hostname \
        --target-db-user username \
        --target-db-password password \ # Enclose the password in single quotes if it contains special characters.
        --target-db-name dbname \
        --target-db-schema schemaName \ # MySQL and Oracle only
        --parallel-jobs connectionCount
```

### import data status

For offline migration, get the status report of an ongoing or completed data import operation. The report contains migration status of tables, number of rows or bytes imported, and percentage completion.

For live migration, get the status report of [import data](#import-data). For live migration with fall forward, the report also includes the status of [fall forward setup](#fall-forward-setup). The report includes the status of tables, the number of rows imported, the total number of changes imported, the number of `INSERT`, `UPDATE`, and `DELETE` events, and the final row count of the target or fall-forward database.

#### Syntax

```text
Usage: yb-voyager import data status [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for import data status are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help. |
| [target-db-password](#target-db-password) | Password of the target database. Live migrations only. |
| [ff-db-password](#ff-db-password) | Password of the fall-forward database. Live migration with fall-forward only.  |

#### Example

```sh
yb-voyager import data status --export-dir /path/to/yb/export/dir
```
