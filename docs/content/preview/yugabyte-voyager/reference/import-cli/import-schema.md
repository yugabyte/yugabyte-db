---
title: import schema reference
headcontent: yb-voyager import schema
linkTitle: import schema
description: YugabyteDB Voyager import schema reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-import-schema
    parent: import-cli
    weight: 50
type: docs
---

[Import the schema](../../migrate/migrate-steps/#import-schema) to the YugabyteDB database.

During migration, run the import schema command twice, first without the [--post-import-data](#post-import-data) argument and then with the argument. The second invocation creates indexes and triggers in the target schema, and must be done after [import data](../../migrate/migrate-steps/#import-data) is complete.

{{< note title="For Oracle migrations" >}}

For Oracle migrations using YugabyteDB Voyager v1.1 or later, the Orafce extension is installed on the target database by default. This enables you to use a subset of predefined functions, operators, and packages from Oracle. The extension is installed in the public schema, and when listing functions or views, extra objects will be visible on the target database which may confuse you. You can remove the extension using the [DROP EXTENSION](../../../api/ysql/the-sql-language/statements/ddl_drop_extension) command.

{{< /note >}}

#### Syntax

```text
Usage: yb-voyager import schema [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for import schema are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help. |
|  [--post-import-data](#post-import-data) | Imports indexes and triggers in the YugabyteDB database after data import is complete. |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. |
| [--start-clean](#start-clean) | Starts a fresh schema import on the target yugabyteDB database for the schema present in the `schema` directory |
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
--continue-on-error
--exclude-object-list
--ignore-exist
--object-list string
--refresh-mviews
--straight-order -->
#### Example

```sh
yb-voyager import schema --export-dir /path/to/yb/export/dir \
        --target-db-host hostname \
        --target-db-user username \
        --target-db-password password \ # Enclose the password in single quotes if it contains special characters.
        --target-db-name dbname \
        --target-db-schema schemaName # MySQL and Oracle only
```
