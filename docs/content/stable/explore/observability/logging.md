---
title: Logging
linkTitle: Logs
headerTitle: Understand YugabyteDB logging
menu:
  stable:
    identifier: observability-logging
    parent: explore-observability
    weight: 900
aliases:
  - /stable/troubleshoot/nodes/check-logs/
type: docs
---

Logs play a critical role in distributed systems by providing visibility into system behavior, aiding in debugging, monitoring, and ensuring reliability. Logs capture events, state changes, and errors across distributed components, offering insights into how the system operates. Using logs, you can track request latency, resource utilization, and throughput, ensuring performance remains optimal.

## Location

The logs for each node are written to a subdirectory of the YugabyteDB `yugabyte-data` directory and may vary depending on your deployment, as follows:

- When using yugabyted to create the local YugabyteDB cluster, by default logs are located in `~/var/logs`.
- For a multi-node cluster deployment to multiple hosts, the location where YugabyteDB disks are set up can vary (for example, `/home/centos/`, `/mnt/`, or another directory) on each node (host).
- When using the `--fs_data_dirs` flag with multiple directories, logs are saved in the first directory in the list.
- When using YugabyteDB Anywhere, logs are located in `/home/yugabyte/{master,tserver}/logs`. This is a symlink to the first directory in `--fs_data_dirs` list.
- When using the Docker container, logs are located in `/root/var/logs` inside the container.
- When you use yb-ctl to create local YugabyteDB clusters on a single host (for example, your computer), the default location for each node is `/yugabyte-data/node-<node_nr>/`. For a 3-node cluster, the yb-ctl utility creates three directories: `node-1`, `node-2`, `node-3`.

In this document, the YugabyteDB `yugabyte-data` directory is represented by `<yugabyte-data-directory>`.

## Error severity

Log messages are assigned different severity levels depending on their significance.

Char | Severity  |                                                Usage
---- | --------- | ---------------------------------------------------------------------------------------------------
`I`  | `INFO`    | Informational messages to show progress of an activity like Session start, Leader election, and so on.
`W`  | `WARNING` | Messages related to likely problems like COMMIT outside a transaction block, non-zero exit code, and so on.
`E`  | `ERROR`   | Messages related to errors that caused the current command to abort, like malformed SQL.
`F`  | `FATAL`   | Messages related to terminating a connection or server.

## Log management

To ensure that logs do not grow indefinitely, consume excessive disk space, and make it harder to analyze, logs are rotated. This involves periodically moving or archiving old log files and creating new ones for ongoing logging. The log rotation size is controlled by the `--max_log_size` flag. For example, setting this flag to 256 limits each file to 256 MB. The default size is 1.8 GB.

For YSQL, there are additional `postgres*log` files that have daily- and size-based log rotation. That is, a new log file is created every day or when a log reaches 10 MB in size.

Instead of logging all queries executed, you can log only a sample of queries by configuring the following settings:

- `log_min_duration_sample` - Defines a minimum execution time (in ms) for a query to be sampled and logged. This is useful for selectively logging slower queries without overwhelming the logs. For example, a value of 500 logs queries taking 500ms or longer.

- `log_statement_sample_rate` - Specifies the fraction of SQL statements to be logged, enabling sampling of queries for logging purposes. This works in conjunction with log_statement and is useful for reducing logging overhead in high-traffic environments. For example, setting this to 0.1 logs about 10% of queries.

For information on available configuration flags, see [YB-Master logging flags](../../../reference/configuration/yb-master/#logging-flags) and [YB-TServer logging flags](../../../reference/configuration/yb-tserver/#logging-flags).

## Log format

YB-Master and YB-TServer services log messages in the following format.

```prolog
Lmmdd hh:mm:ss.uuuuuu threadid file:line] msg
```

For example:

```prolog
I0108 11:14:16.095727 195207168 tablet_service.cc:2690] Leader stepdown request tablet_id: "a00a5..." dest_uuid: "28fc3..." new_leader_uuid: "737bb..." failed. Resp code=UNKNOWN_ERROR
```

The fields are as follows:

{{%table%}}

| Field | Description |
| ----- | ----------- |

| L | A single character, representing the log level (see [Error severity](#error-severity)). |
| mm | Month (zero-padded; for example, May is **05**). |
| dd | Day (zero-padded). |
| hh:mm:ss.uuuuuu | Time in hours, minutes, and fractional seconds. |
| threadid | Thread ID. |
| file | File name. |
| line | Line number. |
| msg | The logged message. |

{{%/table%}}
