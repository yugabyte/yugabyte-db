---
title: Inspect logs
linkTitle: Inspect logs
headerTitle: Inspect YugabyteDB logs
description: Inspect YugabyteDB logs
menu:
  preview:
    parent: troubleshoot-nodes
    weight: 20
type: docs
---

YugabyteDB has an extensive error-handling mechanism, with logging being one of its main components.

## YugabyteDB base folder

The logs for each node are written to a subdirectory of the YugabyteDB `yugabyte-data` directory and may vary depending on your deployment, as follows:

- When you use yb-ctl to create local YugabyteDB clusters on a single host (for example, your computer), the default location for each node is `/yugabyte-data/node-<node_nr>/`. For a 3-node cluster, the yb-ctl utility creates three directories: `node-1`, `node-2`, `node-3`.
- For a multi-node cluster deployment to multiple hosts, the location where YugabyteDB disks are set up can vary (for example, `/home/centos/`, `/mnt/`, or another directory) on each node (host).
- When using the `--fs_data_dirs` flag with multiple directories, logs are saved in the first directory in the list.
- When using YugabyteDB Anywhere, logs are located in `/home/yugabyte/{master,tserver}/logs`. This is a symlink to the first directory in `--fs_data_dirs` list.
- When using the Docker container, logs are located in `/root/var/logs` inside the container.
- When using the yugabyted command-line interface to create the local YugabyteDB cluster, by default logs are located in `~/var/logs`.

In this document, the YugabyteDB `yugabyte-data` directory is represented by `<yugabyte-data-directory>`.

## YB-Master logs

The YB-Master service manages system metadata, such as namespaces (databases or keyspaces) and tables. It also handles Data Definition Language (DDL) statements such as `CREATE TABLE`, `DROP TABLE`, `ALTER TABLE`, `KEYSPACE/TYPE`. In addition, it manages users, permissions, and coordinate background operations, such as load balancing. You can access these logs as follows:

```sh
cd <yugabyte-data-directory>/disk1/yb-data/master/logs/
```

Logs are organized by error severity: `FATAL`, `ERROR`, `WARNING`, `INFO`.

## YB-TServer logs

The YB-TServer service performs the actual input-output for end-user requests. It handles Data Manipulation Language (DML) statements such as `INSERT`, `UPDATE`, `DELETE`, and `SELECT`. You can access these logs as follows:

```sh
cd <yugabyte-data-directory>/disk1/yb-data/tserver/logs/
```

Logs are organized by error severity: `FATAL`, `ERROR`, `WARNING`, `INFO`.

## Logs management

For YB-Master and YB-TServer, the log rotation size is controlled by the `--max_log_size` flag. For example, setting this flag to 256 limits each file to 256 MB. The default size is 1.8 GB.

For YSQL, there are additional `postgres*log` files that have daily-based and size-based log rotation. That is, a new log file is created every day or when a log reaches 10 MB in size.

For information on available configuration flags, see [YB-Master logging flags](../../../reference/configuration/yb-master/#logging-flags) and [YB-TServer logging flags](../../../reference/configuration/yb-tserver/#logging-flags).

## Log format

YB-Master and YB-TServer log messages follow this pattern:

```output
Lmmdd hh:mm:ss.uuuuuu threadid file:line] msg
```

The fields are as follows:

- `L`: A single character, representing the log level (`I` for INFO, `W` for WARNING, `E` for ERROR, and `F` for FATAL).
- `mm`: Month (zero-padded; for example, May is `05`).
- `dd`: Day (zero-padded).
- `hh:mm:ss.uuuuuu`: Time in hours, minutes, and fractional seconds.
- `threadid`: Thread ID.
- `file`: File name.
- `line`: Line number.
- `msg`: The logged message.
