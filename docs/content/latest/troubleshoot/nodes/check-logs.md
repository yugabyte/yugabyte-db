---
title: Inspect logs
linkTitle: Inspect logs
description: Inspect YugabyteDB logs
aliases:
  - /troubleshoot/nodes/check-logs/
menu:
  latest:
    parent: troubleshoot-nodes
    weight: 844
isTocNested: true
showAsideToc: true
---

## YugabyteDB base folder

The logs for each node are written to a sub-directory of the YugabyteDB `yugabyte-data` directory and may vary depending on your deployment:

- When you use `yb-ctl` to create local YugabyteDB clusters on a single host (for example, your laptop), the default location for each node is `/yugabyte-data/node-<node_nr>/`. For a 3-node cluster, `yb-ctl` utility creates three directories: `node-1`, `node-2` and `node-3`.
- For a multi-node cluster deployment to multiple hosts, the location where YugabyteDB disks are set up can vary (for example, `/home/centos/`, `/mnt/`, or another directory) on each node (host).

In the sections below, the YugabyteDB `yugabyte-data` directory is represented by `<yugabyte-data-directory>`.

## YB-Master logs

YB-Master services manage system meta-data, such as namespaces (databases or keyspaces), tables, and types: they handle DDL statements (for example, `CREATE TABLE`, `DROP TABLE`, `ALTER TABLE` KEYSPACE/TYPE`).  YB-Master services also manage users, permissions, and coordinate background operations, such as load balancing.
Master logs can be found at:

```sh
$ cd <yugabyte-data-directory>/disk1/yb-data/master/logs/
```

Logs are organized by error severity: `FATAL`, `ERROR`, `WARNING`, `INFO`. In case of issues, the `FATAL` and `ERROR` logs are most likely to be relevant.

## YB-TServer logs

YB-TServer services perform the actual I/O for end-user requests: they handle DML statements (for example, `INSERT`, `UPDATE`, `DELETE`, and `SELECT`) and Redis commands.
YB-TServer logs can be found at:

```sh
$ cd <yugabyte-data-directory>/disk1/yb-data/tserver/logs/
```

Logs are organized by error severity: `FATAL`, `ERROR`, `WARNING`, `INFO`. In case of issues, the `FATAL` and `ERROR` logs are most likely to be relevant.


## Logs management

There are 3 types of logs:

For yb-tserver/yb-master the log rotation size is controlled by the --max_log_size gflag.

`--max_log_size=256` will limit each file to 256MB.
The default size is 1.8GB if someone is not passing this argument.

YSQL (we also have the additional postgres.log) has daily and size based log rotation for postgres*.log files - i.e. a new log file will be created per day (or after 10 MB size). 

Purging of older log files is only available in Yugabyte Platform.
