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
rightNav:
  hideH4: true
---

The following page describes the following cutover commands:

- [cutover to target](#cutover-to-target)
- [cutover to source](#cutover-to-source)
- [cutover to source-replica](#cutover-to-source-replica)
- [cutover status](#cutover-status)

## cutover to target

Initiate [cutover](../../../migrate/live-migrate/#cutover-to-the-target) to the target YugabyteDB database.

### Syntax

```text
Usage: yb-voyager initiate cutover to target [ <arguments> ... ]
```

### Arguments

The valid *arguments* for initiate cutover to target are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| -e, --export-dir | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help for initiate cutover to target. |
| &#8209;&#8209;prepare&#8209;for&#8209;fall&#8209;back | Prepare for fall-back by streaming changes from the target YugabyteDB database to the source database. Not applicable to the fall-forward workflow.<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --use-yb-grpc-connector | Use the [gRPC Replication Protocol](../../../../explore/change-data-capture/using-yugabytedb-grpc-replication/) for export. If set to false, [PostgreSQL Replication Protocol](../../../../explore/change-data-capture/using-logical-replication/) (supported in YugabyteDB v2024.1.1+) is used. For PostgreSQL Replication Protocol, ensure no ALTER TABLE commands causing table rewrites (for example, adding primary keys) are present in the schema during import.<br>Default: true<br>Accepted parameters: true, false, yes, no, 0, 1. |

### Example

```sh
yb-voyager initiate cutover to target --export-dir /dir/export-dir --prepare-for-fall-back false
```

## cutover to source

Initiate a [cutover](../../../migrate/live-fall-back/#cutover-to-the-source-optional) to the source database.

### Syntax

```text
Usage: yb-voyager initiate cutover to source [ <arguments> ... ]
```

### Arguments

The valid *arguments* for initiate cutover to source are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| -e, &#8209;&#8209;export&#8209;dir | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help for cutover. |

### Example

```sh
yb-voyager initiate cutover to source --export-dir /path/to/yb/export/dir
```

## cutover to source-replica

Initiate a [cutover](../../../migrate/live-fall-forward/#cutover-to-source-replica-optional) to the source-replica database.

### Syntax

```text
Usage: yb-voyager initiate cutover to source-replica [ <arguments> ... ]
```

### Arguments

The valid *arguments* for initiate cutover to source-replica are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| -e, &#8209;&#8209;export&#8209;dir | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help for cutover. |

### Example

```sh
yb-voyager initiate cutover to source-replica --export-dir /path/to/yb/export/dir
```

## cutover status

Shows the status of the cutover process, whether it is [cutover to target](#cutover-to-target), [cutover to source-replica](#cutover-to-source-replica), or [cutover to source](#cutover-to-source). Status can be INITIATED, NOT INITIATED, or COMPLETED.

### Syntax

```text
Usage: yb-voyager cutover status [ <arguments> ... ]
```

### Arguments

The valid *arguments* for cutover status are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| -e, &#8209;&#8209;export&#8209;dir | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help for cutover status. |

### Example

```sh
yb-voyager cutover status --export-dir /dir/export-dir
```
