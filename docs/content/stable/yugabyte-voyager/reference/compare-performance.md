---
title: Compare performance
headcontent: yb-voyager compare-performance
linkTitle: Compare performance
description: YugabyteDB Voyager compare performance reference
menu:
  stable_yugabyte-voyager:
    identifier: compare-performance-voyager
    parent: yb-voyager-cli
    weight: 101
tags:
  feature: tech-preview
type: docs
---

Compare query performance between the source database and the target YugabyteDB database.

This command analyzes statistics collected during [assess migration](../assess-migration/) from the source database, compares it with statistics collected from the target YugabyteDB database, The command generates both HTML and JSON reports for easy query comparison.

## Syntax

```text
Usage: yb-voyager compare-performance [ <arguments> ... ]
```

### Arguments

The valid *arguments* for compare performance are described in the following table:

{{<table>}}
| <div style="width:120px">CLI flag</div> | Config file parameter | Description |
| :--- | :-------- | :---------- |

| -e, --export-dir |

```yaml {.nocopy}
export-dir:
```

| Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|

| -l, --log-level |

```yaml {.nocopy}
log-level:
```

| Log level for yb-voyager. <br>Accepted values: trace, debug, info, warn, error, fatal, panic <br>Default: info |

| --send-diagnostics |

```yaml {.nocopy}
send-diagnostics:
```

| Enable or disable sending [diagnostics](../../reference/diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |

| --target-db-host |

```yaml {.nocopy}
target:
  db-host:
```

| Domain name or IP address of the machine on which the target database server is running. <br>Default: "127.0.0.1" |

| --target-db-name |

```yaml {.nocopy}
target:
  db-name:
```

| Target database name. |

| --target-db-password |

```yaml {.nocopy}
target:
  db-password:
```

| Password to connect to the target YugabyteDB database. Alternatively, you can also specify the password by setting the environment variable `TARGET_DB_PASSWORD`. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. If the password contains special characters that are interpreted by the shell (for example, # and \$), enclose the password in single quotes. |

| --target-db-port |

```yaml {.nocopy}
target:
  db-port:
```

| Port number of the target database machine. <br> Default: 5433 |

| --target-db-schema |

```yaml {.nocopy}
target:
  db-schema:
```

| Schema name of the target database. Works for MySQL and Oracle only. For PostgreSQL, you can ALTER schema name post import. |

| --target-db-user |

```yaml {.nocopy}
target:
  db-user:
```

| Username of the target database. |

| [--target-ssl-cert](../yb-voyager-cli/#ssl-connectivity) |

```yaml {.nocopy}
target:
  ssl-cert:
```

| Path to a file containing the certificate which is part of the SSL `<cert,key>` pair. |

| [--target-ssl-crl](../yb-voyager-cli/#ssl-connectivity) |

```yaml {.nocopy}
target:
  ssl-crl:
```

| Path to a file containing the SSL certificate revocation list (CRL). |

| [--target-ssl-key](../yb-voyager-cli/#ssl-connectivity) |

```yaml {.nocopy}
target:
  ssl-key:
```

| Path to a file containing the key which is part of the SSL `<cert,key>` pair. |

| [--target-ssl-mode](../yb-voyager-cli/#ssl-connectivity) |

```yaml {.nocopy}
target:
  ssl-mode:
```

| Specify the SSL mode for the target database as one of `disable`, `allow`, `prefer` (default), `require`, `verify-ca`, or `verify-full`. |

| [--target-ssl-root-cert](../yb-voyager-cli/#ssl-connectivity) |

```yaml {.nocopy}
target:
  ssl-root-cert:
```

| Path to a file containing target SSL certificate authority (CA) certificate(s). |

| --start-clean | — |  Cleans up existing performance comparison reports before generating new ones. <br>Default: false <br> Accepted parameters: true, false, yes, no, 0, 1. |

| -h, --help | — | Command line help. |

| -y, --yes | — | Answer yes to all prompts during migration. <br>Default: false |

| -c, --config-file | — | Path to a [configuration file](../configuration-file). |

{{</table>}}

### Example

Configuration file:

```sh
yb-voyager compare-performance --config-file <path-to-config-file>
```

CLI:

```sh
yb-voyager compare-performance
        --target-db-host localhost \
        --target-db-port 5433 \
        --target-db-name yugabyte \
        --target-db-user yugabyte \
        --target-db-password password \
        --export-dir /dir/export-dir
```


