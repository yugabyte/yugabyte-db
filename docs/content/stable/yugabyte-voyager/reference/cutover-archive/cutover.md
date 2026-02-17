---
title: cutover reference
headcontent: yb-voyager cutover
linkTitle: cutover
description: YugabyteDB Voyager cutover reference
menu:
  stable_yugabyte-voyager:
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

- [YugabyteDB Connector (GA)](../../../../additional-features/change-data-capture/using-logical-replication/yugabytedb-connector/): Does not require access to internal ports and is recommended for deployments where the gRPC connector cannot be used, for example, YugabyteDB Aeon. Supported in YugabyteDB versions 2024.2.4 or later. Live migration for PostgreSQL source database (with fall-forward or fall-back using YugabyteDB Connector) is {{<tags/feature/ga>}}. Refer to [feature availability](../../../migrate/live-migrate/#feature-availability) for more details.

- [YugabyteDB gRPC Connector](../../../../additional-features/change-data-capture/using-yugabytedb-grpc-replication/debezium-connector-yugabytedb/): Requires direct access to the cluster's internal ports. Specifically, YB-TServer (9100) and YB-Master (7100). This connector is suitable for deployments where these ports are accessible.

  Note: The following datatypes are unsupported when exporting from the target YugabyteDB using the gRPC connector: BOX, CIRCLE, LINE, LSEG, PATH, PG_LSN, POINT, POLYGON, TSQUERY, TSVECTOR, TXID_SNAPSHOT, GEOMETRY, GEOGRAPHY, RASTER, HSTORE.

  Use the argument `--use-yb-grpc-connector` to select the YugabyteDB gRPC connector for the migration as described in the following table.

### Arguments

The following table lists the valid CLI flags and parameters for `cutover to target` command.

When run at the same time, flags take precedence over configuration flag settings.

{{<table>}}

| <div style="width:150px">CLI flag</div> | Config file parameter | Description |
| :--- | :-------- | :---------- |
| --prepare-for-fall-back |

```yaml{.nocopy}
cutover-to-target:
  prepare-for-fall-back:
```

|Prepare for fall-back by streaming changes from the target YugabyteDB database to the source database. Not applicable to the fall-forward workflow.<br> Accepted parameters: true, false, yes, no, 0, 1 |
| --use-yb-grpc-connector |

```yaml{.nocopy}
cutover-to-target:
  use-yb-grpc-connector:
```

|
Applicable to fall-forward or fall-back workflows where you export changes from YugabyteDB during [export data from target](../../../reference/data-migration/export-data/#export-data-from-target). If set to true, [YugabyteDB gRPC Connector](../../../../additional-features/change-data-capture/using-yugabytedb-grpc-replication/debezium-connector-yugabytedb/) is used. Otherwise, [YugabyteDB Connector](../../../../additional-features/change-data-capture/using-logical-replication/yugabytedb-connector/) is used.<br> .<br>Accepted parameters: true, false, yes, no, 0, 1. |
| -e, --export-dir |

```yaml{.nocopy}
export-dir:
```

|Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|

| -l, --log-level |

```yaml {.nocopy}
log-level:
```

| Log level for yb-voyager. <br>Accepted values: trace, debug, info, warn, error, fatal, panic <br>Default: info |
| -h, --help | — |Command line help for initiate cutover to target. |
| -c, --config-file | — | Path to a [configuration file](../../configuration-file). |

{{</table>}}

<!--| --use-yb-grpc-connector | Use the [gRPC Replication Protocol](../../../../additional-features/change-data-capture/using-yugabytedb-grpc-replication/) for export. If set to false, [PostgreSQL Replication Protocol](../../../../additional-features/change-data-capture/using-logical-replication/) (supported in YugabyteDB v2024.1.1+) is used. For PostgreSQL Replication Protocol, ensure no ALTER TABLE commands causing table rewrites (for example, adding primary keys) are present in the schema during import.<br>Default: true<br>Accepted parameters: true, false, yes, no, 0, 1. | -->

### Example

Configuration file:

```sh
yb-voyager initiate cutover to target --config-file <path-to-config-file>
```

CLI:

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

The following table lists the valid CLI flags and parameters for `cutover to source` command.

When run at the same time, flags take precedence over configuration flag settings.

{{<table>}}

| <div style="width:150px">CLI flag</div> | Config file parameter | Description |
| :--- | :-------- | :---------- |
| -e, --export-dir |

```yaml{.nocopy}
export-dir:
```

|Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|

| -l, --log-level |

```yaml {.nocopy}
log-level:
```

| Log level for yb-voyager. <br>Accepted values: trace, debug, info, warn, error, fatal, panic <br>Default: info |
| -h, --help | — |Command line help for cutover. |
| -c, --config-file | — | Path to a [configuration file](../../configuration-file). |

{{</table>}}

### Example

Configuration file:

```sh
yb-voyager initiate cutover to source --config-file <path-to-config-file>
```

CLI:

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

The following table lists the valid CLI flags and parameters for `cutover to source-replica` command.

When run at the same time, flags take precedence over configuration flag settings.

{{<table>}}

| <div style="width:150px">CLI flag</div> | Config file parameter | Description |
| :--- | :-------- | :---------- |
| -e, --export-dir |

```yaml{.nocopy}
export-dir:
```

|Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|

| -l, --log-level |

```yaml {.nocopy}
log-level:
```

| Log level for yb-voyager. <br>Accepted values: trace, debug, info, warn, error, fatal, panic <br>Default: info |

| -h, --help | — | Command line help for cutover. |
| -c, --config-file | — | Path to a [configuration file](../../configuration-file). |

{{</table>}}

### Example

Configuration file:

```sh
yb-voyager initiate cutover to source-replica --config-file <path-to-config-file>
```

CLI:

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

The following table lists the valid CLI flags and parameters for `cutover status` command.

When run at the same time, flags take precedence over configuration flag settings.

{{<table>}}

| <div style="width:150px">CLI flag</div> | Config file parameter | Description |
| :--- | :-------- | :---------- |
| -e, --export-dir |

```yaml{.nocopy}
export-dir:
```

| Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | — | Command line help for cutover status. |
| -c, --config-file | — | Path to a [configuration file](../../configuration-file). |

{{</table>}}

### Example

Configuration file:

```sh
yb-voyager cutover status --config-file <path-to-config-file>
```

CLI:

```sh
yb-voyager cutover status --export-dir /dir/export-dir
```
