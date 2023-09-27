---
title: fall-forward synchronize reference
headcontent: yb-voyager fall-forward synchronize
linkTitle: fall-forward synchronize
description: YugabyteDB Voyager fall-forward synchronize reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-fall-forward-synchronize
    parent: fall-forward
    weight: 100
type: docs
---

Exports new changes from the YugabyteDB database to import to the fall-forward database so that the fall-forward database can be in sync with the YugabyteDB database after cutover.

#### Syntax

```text
Usage: yb-voyager fall-forward synchronize [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for fall-forward synchronize are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| --disable-pb | Set to true to disable progress bar during data import (default false) |
| [--exclude-table-list](#exclude-table-list) <tableNames> | Comma-separated list of tables to exclude while importing data (ignored if `--table-list` is used) |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help for synchronize. |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. (Default: true) |
| [--table-list](#table-list) | Comma-separated list of the tables to export data. |
| [--target-db-host](#target-db-host) <hostname> | Hostname of the target database server. (Default: "127.0.0.1")|
| [--target-db-name](#target-db-name) <name> | Target database name on which import needs to be done.|
| [--target-db-password](#target-db-password) <password>| Target database password to connect to the YugabyteDB database server. |
| [--target-db-port](#target-db-port) <port> | Port number of the target database machine that runs the YugabyteDB YSQL API. |
| [--target-db-schema](#target-db-schema) <schemaName> | Target schema name in YugabyteDB. |
| [--target-db-user](#target-db-user) <username> | Username of the target database. |
| [--target-ssl-cert](#ssl-connectivity) <certificateName> | Name of the certificate which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-key](#ssl-connectivity) <keyName> | Name of the key which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-crl](#ssl-connectivity) <path> | Path to a file containing the SSL certificate revocation list (CRL).|
| [--target-ssl-mode](#ssl-connectivity) <SSLmode> | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--target-ssl-root-cert](#ssl-connectivity) <path> | Path to a file containing SSL certificate authority (CA) certificate(s). |
| [--verbose](#verbose) | Display extra information in the output. |
| [-y, --yes](#yes) | Answer yes to all prompts during migration (Default: false). |

#### Example

```sh
yb-voyager fall-forward synchronize --export-dir /path/to/yb/export/dir \
        --target-db-host hostname \
        --target-db-port port \
        --target-db-user username \
        --target-db-password password \ # Enclose the password in single quotes if it contains special characters.
        --target-db-name dbname \
        --target-db-schema schemaName
```
