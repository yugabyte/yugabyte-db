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

### YugabyteDB gRPC vs YugabyteDB Connector

During the `export data from target` phase, after `cutover to target` in the fall-forward/fall-back workflows, YugabyteDB Voyager exports changes from YugabyteDB. YugabyteDB provides two types of CDC (Change Data Capture) connectors:

- [YugabyteDB gRPC Connector](../../../../develop/change-data-capture/using-yugabytedb-grpc-replication/debezium-connector-yugabytedb/): Requires direct access to the cluster's internal ports-specifically, YB-TServer (9100) and YB-Master (7100). This connector is suitable for deployments where these ports are accessible.

- [YugabyteDB Connector](../../../../develop/change-data-capture/using-logical-replication/yugabytedb-connector/): Does not require access to internal ports and is recommended for deployments where the gRPC connector cannot be used, for example, YugabyteDB Aeon. Supported in YugabyteDB versions 2024.1.1 or later. Note that currently there are is a known limitation with this connector. Refer to GitHub issue [27248](https://github.com/yugabyte/yugabyte-db/issues/27248) for more details.

Use the argument `--use-yb-grpc-connector` to select the YugabyteDB gRPC connector for the migration as described in the following table.

### Arguments

The valid *arguments* for initiate cutover to target are described in the following table:

| <div style="width:150px">Argument</div> | Description/valid options |
| :------- | :------------------------ |
| -e, --export-dir | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help for initiate cutover to target. |
| --prepare-for-fall-back | Prepare for fall-back by streaming changes from the target YugabyteDB database to the source database. Not applicable to the fall-forward workflow.<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --use-yb-grpc-connector | Applicable to fall-forward or fall-back workflows where you export changes from YugabyteDB during [export data from target](../../../reference/data-migration/export-data/#export-data-from-target). If set to true, [YugabyteDB gRPC Connector](../../../../develop/change-data-capture/using-yugabytedb-grpc-replication/debezium-connector-yugabytedb/) is used. Otherwise, [YugabyteDB Connector](../../../../develop/change-data-capture/using-logical-replication/yugabytedb-connector/) is used.<br> .<br>Accepted parameters: true, false, yes, no, 0, 1. |

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

| <div style="width:150px">Argument</div> | Description/valid options |
| :------- | :------------------------ |
| -e, --export-dir | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
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

| <div style="width:150px">Argument</div> | Description/valid options |
| :------- | :------------------------ |
| -e, --export-dir | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
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

| <div style="width:150px">Argument</div> | Description/valid options |
| :------- | :------------------------ |
| -e, --export-dir | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help for cutover status. |

### Example

```sh
yb-voyager cutover status --export-dir /dir/export-dir
```
