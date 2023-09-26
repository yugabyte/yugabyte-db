---
title: export schema reference
headcontent: yb-voyager export schema
linkTitle: export schema
description: YugabyteDB Voyager export schema reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-export-schema
    parent: yb-voyager-cli
    weight: 10
type: docs
---


[Export the schema](../../migrate/migrate-steps/#export-and-analyze-schema) from the source database.

#### Syntax

```text
Usage: yb-voyager export schema [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for export schema are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help. |
| [--oracle-db-sid](#oracle-db-sid) <SID> | Oracle System Identifier. Oracle migrations only.|
| [--oracle-home](#oracle-home) <path> | Path to set `$ORACLE_HOME` environment variable. Oracle migrations only.|
| [--oracle-tns-alias](#ssl-connectivity) <alias> | TNS (Transparent Network Substrate) alias configured to establish a secure connection with the server. Oracle migrations only. |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. |
| [--source-db-type](#source-db-type) <databaseType> | One of `postgresql`, `mysql`, or `oracle`. |
| [--source-db-host](#source-db-host) <hostname> | Hostname of the source database server. |
| [--source-db-name](#source-db-name) <name> | Source database name. |
| [--source-db-password](#source-db-password) <password>| Source database password. |
| [--source-db-port](#source-db-port) <port> | Port number of the source database machine. |
| [--source-db-schema](#source-db-schema) <schemaName> | Schema name of the source database. |
| [--source-db-user](#source-db-user) <username> | Name of the source database user (typically `ybvoyager`). |
| [--source-ssl-cert](#ssl-connectivity) <certificateName> | Name of the certificate which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-key](#ssl-connectivity) <keyName> | Name of the key which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-crl](#ssl-connectivity) <path> | Path to a file containing the SSL certificate revocation list (CRL).|
| [--source-ssl-mode](#ssl-connectivity) <SSLmode> | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--source-ssl-root-cert](#ssl-connectivity) <path> | Path to a file containing SSL certificate authority (CA) certificate(s). |
| [--start-clean](#start-clean) | Starts a fresh schema export after clearing the `schema` directory. |
| [--use-orafce](#use-orafce) | Use the Orafce extension. Oracle migrations only. |
| [-y, --yes](#yes) | Answer yes to all prompts during the export schema operation. |

#### Example

```sh
yb-voyager export schema --export-dir /path/to/yb/export/dir \
        --source-db-type sourceDB \
        --source-db-host localhost \
        --source-db-port port \
        --source-db-user username \
        --source-db-password password \ # Enclose the password in single quotes if it contains special characters.
        --source-db-name dbname \
        --source-db-schema schemaName \ # Not applicable for MySQL
        --start-clean

```
