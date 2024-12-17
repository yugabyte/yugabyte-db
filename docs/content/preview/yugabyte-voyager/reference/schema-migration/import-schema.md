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

During migration, run the import schema command twice, first without the [--post-snapshot-import](#arguments) argument and then with the argument. The second invocation creates indexes and triggers in the target schema, and must be done after [import data](../../../migrate/migrate-steps/#import-data) is complete.

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
| --continue&#8209;on&#8209;error | Continue to import all the exported schema even if there are errors, and output all the erroneous DDLs to the `failed.sql` file in the `export-dir/schema` directory. <br>Default: false <br> Example: `yb-voyager import schema ... --continue-on-error true`<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --enable-orafce | Enables Orafce extension on target (if source database type is Oracle). <br>Default: true <br> Accepted parameters: true, false, yes, no, 0, 1 |
| --exclude&#8209;object&#8209;type&#8209;list | Comma-separated list of schema object types (object types are case-insensitive) to exclude while importing schema (ignored if --object-type-list is used).<br> Example: `--exclude-object-type-list 'FUNCTION,PROCEDURE'` |
| -e, --export-dir | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file. |
| -h, --help | Command line help. |
| --ignore-exist | Ignore if an object already exists on the target database. <br>Default: false<br>Example: `yb-voyager import schema ... --ignore-exist true` <br> Accepted parameters: true, false, yes, no, 0, 1 |
| --object-type-list, <br> &#8209;&#8209;exclude&#8209;object&#8209;type&#8209;list  | Comma-separated list of objects to import (--object-type-list) or not (--exclude-object-type-list). You can provide only one of the arguments at a time. <br> Example: `yb-voyager import schema …. –object-type-list "TABLE,FUNCTION,VIEW"` <br> Accepted parameters: <ul><li>Oracle: TYPE, SEQUENCE, TABLE, PARTITION, INDEX, PACKAGE, TRIGGER, FUNCTION, PROCEDURE, MVIEW, SYNONYM </li><li>PostgreSQL: SCHEMA, COLLATION, EXTENSION, TYPE, DOMAIN, SEQUENCE, TABLE, INDEX, FUNCTION, AGGREGATE, PROCEDURE, VIEW, TRIGGER, MVIEW, RULE, COMMENT</li><li>MySQL: TABLE, PARTITION, INDEX, VIEW, TRIGGER, FUNCTION, PROCEDURE</li></ul> |
| --post-snapshot-import | Perform schema related tasks on the target YugabyteDB after data import is complete. Use --refresh-mviews along with this flag to refresh materialized views. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --refresh-mviews | Refreshes the materialized views on target during the post-import-data phase. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --send-diagnostics | Enable or disable sending [diagnostics](../../../diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --start-clean | Starts a fresh schema import on the target YugabyteDB database for the schema present in the `schema` directory.<br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --straight-order | Imports the schema objects in the order specified via the `--object-type-list` flag. <br>Default: false<br> Example: `yb-voyager import schema ... --object-type-list 'TYPE,TABLE,VIEW...'  --straight-order true` <br> Accepted parameters: true, false, yes, no, 0, 1 |
| --target-db-host | Domain name or IP address of the machine on which target database server is running. <br>Default: 127.0.0.1|
| --target-db-name | Target database name. <br>Default: yugabyte |
| --target-db-password | Target database password. Alternatively, you can also specify the password by setting the environment variable `TARGET_DB_PASSWORD`. If you don't provide a password via the CLI or environment variable during any migration phase, yb-voyager will prompt you at runtime for a password. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes. |
| --target-db-port | Port number of the target database server. <br>Default: 5433 |
| --target-db-schema | Schema name of the target YugabyteDB database. MySQL and Oracle migrations only. |
| --target-db-user | Username of the target YugabyteDB database. |
| [--target-ssl-cert](../../yb-voyager-cli/#yugabytedb-options) | Path to a file containing the certificate which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-key](../../yb-voyager-cli/#yugabytedb-options) | Path to a file containing the key which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-crl](../../yb-voyager-cli/#yugabytedb-options) | Path to a file containing the SSL certificate revocation list (CRL).|
| --target-ssl-mode | Specify the SSL mode for the target database as one of `disable`, `allow`, `prefer` (default), `require`, `verify-ca`, or `verify-full`. |
| [--target-ssl-root-cert](../../yb-voyager-cli/#yugabytedb-options) | Path to a file containing SSL certificate authority (CA) certificate(s). |
| -y, --yes | Answer yes to all prompts during the export schema operation. <br>Default: false |

### Examples

```sh
yb-voyager import schema --export-dir /dir/export-dir \
        --target-db-host 127.0.0.1 \
        --target-db-user ybvoyager \
        --target-db-password 'password' \
        --target-db-name target_db \
        --target-db-schema target_schema
```

Example of [post-import data](../../../migrate/migrate-steps/#refresh-materialized-views) phase is as follows:

```sh

yb-voyager import schema --export-dir /dir/export-dir \
        --target-db-host 127.0.0.1 \
        --target-db-user ybvoyager \
        --target-db-password 'password' \
        --target-db-name target_db \
        --target-db-schema target_schema \
        --post-snapshot-import true \
        --refresh-mviews true
```
