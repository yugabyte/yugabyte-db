---
title: archive changes reference
headcontent: yb-voyager archive changes
linkTitle: archive changes
description: YugabyteDB Voyager archive changes reference
menu:
  stable_yugabyte-voyager:
    identifier: voyager-archive-changes
    parent: cutover-archive
    weight: 150
type: docs
---

The archive changes command limits the disk space used by the locally queued CDC events. After the changes from the local queue are applied on the target YugabyteDB database (and source-replica database), they are eligible for deletion.

You can either choose to delete the segments or archive them to a separate location.

## Syntax

```text
Usage: yb-voyager archive changes [ <arguments> ... ]
```

### Arguments

The following table lists the valid CLI flags and parameters for `archive changes` command.

When run at the same time, flags take precedence over configuration flag settings.

{{<table>}}

| <div style="width:150px">CLI flag</div> | Config file parameter | Description |
| :--- | :-------- | :---------- |

| --fs-utilization-threshold |

```yaml{.nocopy}
archive-changes:
  fs-utilization-threshold:
```

|Disk use threshold (in percent) for the export directory. Used only with `--policy delete`. <br>Default: 70 |
| --policy |

```yaml {.nocopy}
archive-changes:
  policy:
```

| Specifies how to handle processed segments: `delete` or `archive`. |
| --archive-dir |

```yaml {.nocopy}
archive-changes:
  archive-dir: 
```

| Path to an existing directory to copy processed segments into before they are removed from the export directory. <br> Required when policy is `archive`. |
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
| -h, --help | — | Command line help for archive changes. |
| -y, --yes | — | Answer yes to all prompts during migration. <br>Default: false |
| -c, --config-file | — | Path to a [configuration file](../../configuration-file). |

{{</table>}}

### Examples

Configuration file:

```sh
yb-voyager archive changes --config-file <path-to-config-file>
```

CLI:

Example to delete changes without archiving them to another destination is as follows:

```sh
yb-voyager archive changes --export-dir /dir/export-dir --policy delete
```

Example to archive changes from the export directory to another destination is as follows:

```sh
yb-voyager archive changes --export-dir /dir/export-dir --policy archive --archive-dir /dir/archive-dir
```
