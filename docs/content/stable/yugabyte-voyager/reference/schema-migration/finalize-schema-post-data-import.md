---
title: finalize-schema-post-data-import reference
headcontent: yb-voyager finalize-schema-post-data-import
linkTitle: finalize-schema-post-data-import
description: YugabyteDB Voyager finalize-schema-post-data-import reference
menu:
  stable_yugabyte-voyager:
    identifier: voyager-finalize-schema-post-data-import
    parent: schema-migration
    weight: 60
type: docs
---

[Finalize the schema](../../../migrate/migrate-steps/#finalize-schema-post-data-import) post the import of data into YugabyteDB database.

Create indexes and triggers in the target schema, and refresh the materialized views. Must be done after [import data](../../../migrate/migrate-steps/#import-data) is complete.

## Syntax

```text
Usage: yb-voyager finalize-schema-post-data-import [ <arguments> ...]
```

### Arguments

The following table lists the valid CLI flags and parameters for `finalize-schema-post-data-import` command.

When run at the same time, flags take precedence over configuration flag settings.

{{<table>}}

| <div style="width:150px">CLI flag</div> | Config file parameter | Description |
| :--- | :-------- | :---------- |
| --run-guardrails-checks |

```yaml{.nocopy}
finalize-schema-post-data-import:
  run-guardrails-checks:
```

|Run guardrails checks during migration. <br>Default: true<br>Accepted values: true, false, yes, no, 0, 1 |

| --continue-on-error |

```yaml{.nocopy}
finalize-schema-post-data-import:
  continue-on-error:
```

|Continue to import all the exported schema even if there are errors, and output all the erroneous DDLs to the `failed.sql` file in the `export-dir/schema` directory. <br>Default: false <br> Example: `yb-voyager finalize-schema-post-data-import ... --continue-on-error true`<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --ignore-exist |

```yaml{.nocopy}
finalize-schema-post-data-import:
  ignore-exist:
```

|Ignore if an object already exists on the target database. <br>Default: false<br>Example: `yb-voyager finalize-schema-post-data-import ... --ignore-exist true` <br> Accepted parameters: true, false, yes, no, 0, 1 |
| --refresh-mviews |

```yaml{.nocopy}
finalize-schema-post-data-import:
  refresh-mviews:
```

|Refreshes the materialized views on target during the post-import-data phase. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |

| --target-db-host |

```yaml{.nocopy}
target:
  db-host:
```

|Domain name or IP address of the machine on which target database server is running. <br>Default: 127.0.0.1|
| --target-db-name |

```yaml{.nocopy}
target:
  db-name:
```

|Target database name. <br>Default: yugabyte |
| --target-db-password |

```yaml{.nocopy}
target:
  db-password:
```

|Target database password. Alternatively, you can also specify the password by setting the environment variable `TARGET_DB_PASSWORD`. If you don't provide a password via the CLI or environment variable during any migration phase, yb-voyager will prompt you at runtime for a password. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes. |
| --target-db-port |

```yaml{.nocopy}
target:
  db-port:
```

|Port number of the target database server. <br>Default: 5433 |
| --target-db-schema |

```yaml{.nocopy}
target:
  db-schema:
```

|Schema name of the target YugabyteDB database. MySQL and Oracle migrations only. |
| --target-db-user |

```yaml{.nocopy}
target:
  db-user:
```

|Username of the target YugabyteDB database. |
| [--target-ssl-cert](../../yb-voyager-cli/#yugabytedb-options) |

```yaml{.nocopy}
target:
  ssl-cert:
```

|Path to a file containing the certificate which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-key](../../yb-voyager-cli/#yugabytedb-options) |

```yaml{.nocopy}
target:
  ssl-key:
```

|Path to a file containing the key which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-crl](../../yb-voyager-cli/#yugabytedb-options) |

```yaml{.nocopy}
target:
  ssl-crl:
```

|Path to a file containing the SSL certificate revocation list (CRL).|
| --target-ssl-mode |

```yaml{.nocopy}
target:
  ssl-mode:
```

|Specify the SSL mode for the target database as one of `disable`, `allow`, `prefer` (default), `require`, `verify-ca`, or `verify-full`. |
| [--target-ssl-root-cert](../../yb-voyager-cli/#yugabytedb-options) |

```yaml{.nocopy}
target:
  ssl-root-cert:
```

|Path to a file containing SSL certificate authority (CA) certificate(s). |

| -e, --export-dir |

```yaml{.nocopy}
export-dir:
```

|Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file. |

| --send-diagnostics |

```yaml{.nocopy}
send-diagnostics:
```

|Enable or disable sending [diagnostics](../../../reference/diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |

| -l, --log-level |

```yaml {.nocopy}
log-level:
```

| Log level for yb-voyager. <br>Accepted values: trace, debug, info, warn, error, fatal, panic <br>Default: info |

| -h, --help | — | Command line help. |
| -y, --yes | — | Answer yes to all prompts during the export schema operation. <br>Default: false |
| -c, --config-file | — | Path to a [configuration file](../../configuration-file). |

{{</table>}}

### Example

Configuration file:

```yaml
yb-voyager finalize-schema-post-data-import --config-file <path-to-config-file>
```

CLI:

```sh
yb-voyager finalize-schema-post-data-import --export-dir /dir/export-dir \
        --target-db-host 127.0.0.1 \
        --target-db-user ybvoyager \
        --target-db-password 'password' \
        --target-db-name target_db \
        --target-db-schema target_schema \
        --refresh-mviews true
```
