---
title: analyze schema reference
headcontent: yb-voyager analyze schema
linkTitle: analyze schema
description: YugabyteDB Voyager analyze schema reference
menu:
  stable_yugabyte-voyager:
    identifier: voyager-analyze-schema
    parent: schema-migration
    weight: 20
type: docs
---

[Analyse the PostgreSQL schema](../../../migrate/migrate-steps/#analyze-schema) dumped in the export schema step.

## Syntax

```sh
yb-voyager analyze-schema [ <arguments> ... ]
```

### Arguments

The following table lists the valid CLI flags and parameters for `analyze-schema` command.

When run at the same time, flags take precedence over configuration flag settings.

{{<table>}}

| <div style="width:150px">CLI flag</div> | Config file parameter | Description |
| :--- | :-------- | :---------- |
| --output-format |

```yaml{.nocopy}
analyze-schema:
  output-format:
```

|Format for the status report. One of `html`, `txt`, `json`, or `xml`. If not provided, reports are generated in both `json` and `html` formats by default. |
| --target-db-version |

```yaml{.nocopy}
analyze-schema:
  target-db-version:
```

|Specifies the target version of YugabyteDB in the format `A.B.C.D`.<br>Default: latest stable version |
| -e, --export-dir |

```yaml{.nocopy}
export-dir:
```

|Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
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
| -h, --help | — |Command line help. |
| -y, --yes | — |Answer yes to all prompts during the export schema operation. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |
| -c, --config-file | — | Path to a [configuration file](../../configuration-file). |

{{</table>}}

### Example

Configuration file:

```sh
yb-voyager analyze-schema --config-file <path-to-config-file>
```

CLI:

```sh
yb-voyager analyze-schema --export-dir /dir/export-dir --output-format txt
```
