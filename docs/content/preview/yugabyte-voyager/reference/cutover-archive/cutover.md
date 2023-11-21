---
title: cutover reference
headcontent: yb-voyager cutover
linkTitle: cutover
description: YugabyteDB Voyager cutover reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-cutover-initiate
    parent: cutover-archive
    weight: 110
type: docs
---

The following page describes the following cutover commands:

- [cutover initiate](#cutover-initiate)
- [cutover status](#cutover-status)

### cutover initiate

Initiate [cutover](../../../migrate/live-migrate/#cutover-to-the-target) to the YugabyteDB database.

#### Syntax

```text
Usage: yb-voyager cutover initiate [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for cutover initiate are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help for cutover initiate. |
| --prepare-for-fall-back | Prepare for fall-back by streaming changes from the target yugabyteDB database to the source database. <br>Default false<br> Accepted parameters: true, false, yes, no, 0, 1 |

#### Example

```sh
yb-voyager cutover initiate --export-dir /dir/export-dir
```

### cutover status

Shows the status of the cutover to the YugabyteDB database. Status can be INITIATED, NOT INITIATED, or COMPLETED.

## Syntax

```text
Usage: yb-voyager cutover status [ <arguments> ... ]
```

### Arguments

The valid *arguments* for cutover status are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help for cutover status. |

#### Example

```sh
yb-voyager cutover status --export-dir /dir/export-dir
