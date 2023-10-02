---
title: import schema reference
headcontent: yb-voyager import schema
linkTitle: import schema
description: YugabyteDB Voyager import schema reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-import-schema
    parent: schema-migration
    weight: 50
type: docs
---

[Import the schema](../../../migrate/migrate-steps/#import-schema) to the YugabyteDB database.

During migration, run the import schema command twice, first without the [--post-import-data](#arguments) argument and then with the argument. The second invocation creates indexes and triggers in the target schema, and must be done after [import data](../../../migrate/migrate-steps/#import-data) is complete.

{{< note title="For Oracle migrations" >}}

For Oracle migrations using YugabyteDB Voyager v1.1 or later, the Orafce extension is installed on the target database by default. This enables you to use a subset of predefined functions, operators, and packages from Oracle. The extension is installed in the public schema, and when listing functions or views, extra objects will be visible on the target database which may confuse you. You can remove the extension using the [DROP EXTENSION](../../../../api/ysql/the-sql-language/statements/ddl_drop_extension) command.

{{< /note >}}

## Syntax

```text
Usage: yb-voyager import schema [ <arguments> ... ]
```

### Arguments

The valid *arguments* for import schema are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| --continue-on-error | If set, this flag ignores errors and continues with the import. |
| --enable-orafce | enables Orafce extension on target (if source database type is Oracle) (default: true) |
| --exclude-object-list <objectTypes> | list of schema object types to exclude while importing schema (ignored if --object-list is used) |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file. |
| -h, --help | Command line help. |
| --ignore-exist | Set to true to ignore errors if object already exists and false to output the errors to the standard output (default: false) |
| --object-list <objectTypes> | List of schema object types to include while importing schema. |
| --post-import-data | Imports indexes and triggers in the YugabyteDB database after data import is complete. This argument assumes that data import is already done and imports only indexes and triggers in the YugabyteDB database.|
| --refresh-mviews | If set, refreshes the materialised views on target during post import data phase (default: false) |
| --send-diagnostics| Send diagnostics information to Yugabyte. |
| --start-clean | Starts a fresh schema import on the target yugabyteDB database for the schema present in the `schema` directory |
| --straight-order | If set, objects are imported in the order specified with the --object-list flag (default: false) |
| --target-db-host <hostname> | Domain name or IP address of the machine on which target database server is running. |
| --target-db-name <name> | Target database name. (default: yugabyte) |
| --target-db-password <password>| Target database password. Alternatively, you can also specify the password by setting the environment variable `TARGET_DB_PASSWORD`. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes. |
| --target-db-port <port> | Port number of the target database machine. (default: 5433) |
| --target-db-schema <schemaName> | Schema name of the target database. MySQL and Oracle migrations only. |
| --target-db-sid <SID> | Oracle System Identifier (SID) that you wish to use while importing data to Oracle instances. Oracle migrations only. |
| --target-db-user <username> | Username of the target database. |
| [--target-ssl-cert](../../yb-voyager-cli/#ssl-connectivity) <certificateName> | Name of the certificate which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-key](../../yb-voyager-cli/#ssl-connectivity) <keyName> | Name of the key which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-crl](../../yb-voyager-cli/#ssl-connectivity) <path> | Path to a file containing the SSL certificate revocation list (CRL).|
| [--target-ssl-mode](../../yb-voyager-cli/#ssl-connectivity) <SSLmode> | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--target-ssl-root-cert](../../yb-voyager-cli/#ssl-connectivity) <path> | Path to a file containing SSL certificate authority (CA) certificate(s). |
| --verbose | Display extra information in the output. (default: false) |
| -y, --yes | Answer yes to all prompts during the export schema operation. (default: false) |

<!-- To do : document the following arguments with description
--continue-on-error
--exclude-object-list
--ignore-exist
--object-list string
--refresh-mviews
--straight-order -->
## Example

```sh
yb-voyager import schema --export-dir /path/to/yb/export/dir \
        --target-db-host hostname \
        --target-db-user username \
        --target-db-password password \ # Enclose the password in single quotes if it contains special characters.
        --target-db-name dbname \
        --target-db-schema schemaName # MySQL and Oracle only
```
