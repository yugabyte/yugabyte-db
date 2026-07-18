---
title: import schema reference
headcontent: yb-voyager import schema
linkTitle: import schema
description: YugabyteDB Voyager import schema reference
menu:
  stable_yugabyte-voyager:
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

{{< note title ="Deprecated flags" >}}
The `--post-import-data` and `--refresh-mviews` flags are deprecated. Use [finalize-schema-post-data-import](../finalize-schema-post-data-import/) instead.
{{< /note >}}

## Syntax

```text
Usage: yb-voyager import schema [ <arguments> ... ]
```

### Arguments

The following table lists the valid CLI flags and parameters for `import schema` command.

When run at the same time, flags take precedence over configuration flag settings.
{{<table>}}

| <div style="width:150px">CLI flag</div> | Config file parameter | Description |
| :--- | :-------- | :---------- |
| --continue-on-error |

```yaml{.nocopy}
import-schema:
  continue-on-error:
```

| Continue to import all the exported schema even if there are errors, and output all the erroneous DDLs to the `failed.sql` file in the `export-dir/schema` directory. <br>Default: false <br> Example: `yb-voyager import schema ... --continue-on-error true`<br> Accepted parameters: true, false, yes, no, 0, 1 |

| --enable-orafce |

```yaml{.nocopy}
import-schema:
  enable-orafce:
```

| Enables Orafce extension on target (if source database type is Oracle). <br>Default: true <br> Accepted parameters: true, false, yes, no, 0, 1 |

| --exclude-object-type-list |

```yaml{.nocopy}
import-schema:
  exclude-object-type-list:
```

| Comma-separated list of schema object types (object types are case-insensitive) to exclude while importing schema (ignored if --object-type-list is used).<br> Example: `--exclude-object-type-list 'FUNCTION,PROCEDURE'` |

| --ignore-exist |

```yaml{.nocopy}
import-schema:
  ignore-exist:
```

| Ignore if an object already exists on the target database. <br>Default: false<br>Example: `yb-voyager import schema ... --ignore-exist true` <br> Accepted parameters: true, false, yes, no, 0, 1 |

| --object-type-list, <br> --exclude-object-type-list |

```yaml{.nocopy}
import-schema:
  object-type-list:
  exclude-object-type-list:
```

| Comma-separated list of objects to import (--object-type-list) or not (--exclude-object-type-list). You can provide only one of the arguments at a time. <br> Example: `yb-voyager import schema …. -object-type-list "TABLE,FUNCTION,VIEW"` <br> Accepted parameters: <ul><li>Oracle: TYPE, SEQUENCE, TABLE, PARTITION, INDEX, PACKAGE, TRIGGER, FUNCTION, PROCEDURE, MVIEW, SYNONYM </li><li>PostgreSQL: SCHEMA, COLLATION, EXTENSION, TYPE, DOMAIN, SEQUENCE, TABLE, INDEX, FUNCTION, AGGREGATE, PROCEDURE, VIEW, TRIGGER, MVIEW, RULE, COMMENT</li><li>MySQL: TABLE, PARTITION, INDEX, VIEW, TRIGGER, FUNCTION, PROCEDURE</li></ul> |

| --straight-order |

```yaml{.nocopy}
import-schema:
  straight-order:
```

| Imports the schema objects in the order specified via the `--object-type-list` flag. <br>Default: false<br> Example: `yb-voyager import schema ... --object-type-list 'TYPE,TABLE,VIEW...'  --straight-order true` <br> Accepted parameters: true, false, yes, no, 0, 1 |

| --target-db-host |

```yaml{.nocopy}
target:
  db-host:
```

| Domain name or IP address of the machine on which target database server is running. <br>Default: 127.0.0.1 |

| --target-db-name |

```yaml{.nocopy}
target:
  db-name:
```

| Target database name. <br>Default: yugabyte |

| --target-db-password |

```yaml{.nocopy}
target:
  db-password:
```

| Target database password. Alternatively, you can also specify the password by setting the environment variable `TARGET_DB_PASSWORD`. If you don't provide a password via the CLI or environment variable during any migration phase, yb-voyager will prompt you at runtime for a password. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes. |

| --target-db-port |

```yaml{.nocopy}
target:
  db-port:
```

| Port number of the target database server. <br>Default: 5433 |

| --target-db-schema |

```yaml{.nocopy}
source:
  db-schema:
```

| Schema name of the target YugabyteDB database. MySQL and Oracle migrations only. |

| --target-db-user |

```yaml{.nocopy}
target:
  db-user:
```

| Username of the target YugabyteDB database. |

| [--target-ssl-cert](../../yb-voyager-cli/#yugabytedb-options) |

```yaml{.nocopy}
target:
  ssl-cert:
```

| Path to a file containing the certificate which is part of the SSL `<cert,key>` pair. |

| [--target-ssl-key](../../yb-voyager-cli/#yugabytedb-options) |

```yaml{.nocopy}
target:
  ssl-key:
```

| Path to a file containing the key which is part of the SSL `<cert,key>` pair. |

| [--target-ssl-crl](../../yb-voyager-cli/#yugabytedb-options) |

```yaml{.nocopy}
target:
  ssl-crl:
```

| Path to a file containing the SSL certificate revocation list (CRL). |

| --target-ssl-mode |

```yaml{.nocopy}
target:
  ssl-mode:
```

| Specify the SSL mode for the target database as one of `disable`, `allow`, `prefer` (default), `require`, `verify-ca`, or `verify-full`. |

| [--target-ssl-root-cert](../../yb-voyager-cli/#yugabytedb-options) |

```yaml{.nocopy}
target:
  ssl-root-cert:
```

| Path to a file containing SSL certificate authority (CA) certificate(s). |
| -e, --export-dir |

```yaml{.nocopy}
export-dir:
```

| Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file. |

| --send-diagnostics |

```yaml{.nocopy}
send-diagnostics:
```

| Enable or disable sending [diagnostics](../../../reference/diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |

| -l, --log-level |

```yaml {.nocopy}
log-level:
```

| Log level for yb-voyager. <br>Accepted values: trace, debug, info, warn, error, fatal, panic <br>Default: info |

| --post-snapshot-import | — | **[Deprecated]** Perform schema related tasks on the target YugabyteDB after data import is complete. Use --refresh-mviews along with this flag to refresh materialized views. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --refresh-mviews | — |**[Deprecated]** Refreshes the materialized views on target during the post-import-data phase. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| -y, --yes | —  | Answer yes to all prompts during the export schema operation. <br>Default: false |
| -h, --help | —  | Command line help. |
| -c, --config-file | — | Path to a [configuration file](../../configuration-file). |

{{</table>}}

### Examples

Configuration file:

```yaml
yb-voyager import schema --config-file <path-to-config-file>
```

CLI:


```sh
yb-voyager import schema --export-dir /dir/export-dir \
        --target-db-host 127.0.0.1 \
        --target-db-user ybvoyager \
        --target-db-password 'password' \
        --target-db-name target_db \
        --target-db-schema target_schema
```

Example of import schema with [--post-snapshot-import](../../../migrate/migrate-steps/#post-snapshot-import) and --refresh-mviews flags are as follows:

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
