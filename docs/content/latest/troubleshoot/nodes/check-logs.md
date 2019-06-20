---
title: Inspect Logs
linkTitle: Inspect Logs
description: Inspect Logs
aliases:
  - /troubleshoot/nodes/check-logs/
menu:
  latest:
    parent: troubleshoot-nodes
    weight: 844
isTocNested: true
showAsideToc: true
---

## YugaByte DB base folder

The logs for each node are found in the yugabyte base directory which may depend on the details of your deployment:

- When using the `yb-ctl` the default locations for each node is `/tmp/yugabyte-local-cluster/node-<node_nr>/`. 
For instance, for 3 nodes (default) `yb-ctl` will generate three folders `node-1`, `node-2` and `node-3`.
- For a multi-node cluster the location where YugaByte disks are set up (e.g. `/home/centos/` or `/mnt/`) for each node.

We will refer to the YugaByte base folder as `<yugabyte-base-folder>` below.

## Master logs

Masters manage system meta-data such as keyspaces, tables and types: they handle DDL statements (e.g. `CREATE/DROP/ALTER TABLE/KEYSPACE/TYPE`).  Masters also manage users, permissions, and coordinate background operations such as load balancing.
Master logs can be found at:

```sh
$ cd <yugabyte-base-folder>/disk1/yb-data/master/logs/
```
Logs are organized by error severity: `FATAL`, `ERROR`, `WARNING`, `INFO`. In case of issues, the `FATAL` and `ERROR` logs are most likely to be relevant. 

## TServer logs

TServers do the actual IO for end-user requests: they handle DML statements (e.g. `INSERT`, `UPDATE`, `DELETE`, `SELECT`)  and Redis commands.
Tserver logs can be found at:
```sh
$ cd <yugabyte-base-folder>/disk1/yb-data/tserver/logs/
```
Logs are organized by error severity: `FATAL`, `ERROR`, `WARNING`, `INFO`. In case of issues, the `FATAL` and `ERROR` logs are most likely to be relevant. 

