---
title: fall-back switchover reference
headcontent: yb-voyager fall-back switchover
linkTitle: fall-back switchover
description: YugabyteDB Voyager fall-back switchover reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-fall-back-switchover
    parent: fall-back
    weight: 130
type: docs
---

This page describes the usage of the following switchover commands:

- [fall-back switchover](#fall-back-switchover)
- [fall-back status](#fall-back-status)

## fall-back switchover

Initiate a [switchover](../../../migrate/live-fall-back/#switchover-to-the-source-database-optional) to the fall-back database.

### Syntax

```text
Usage: yb-voyager fall-back switchover [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for fall-back switchover are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help for switchover. |

### Example

```sh
yb-voyager fall-back switchover --export-dir /path/to/yb/export/dir
```

## fall-back status

Shows the status of the fall-back switchover to the fall-back database. Status can be INITIATED, NOT INITIATED, or COMPLETED.

### Syntax

```text
Usage: yb-voyager fall-back status [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for fall-back switchover status are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help for cutover status. |

### Example

```sh
yb-voyager fall-back status --export-dir /dir/export-dir
```
