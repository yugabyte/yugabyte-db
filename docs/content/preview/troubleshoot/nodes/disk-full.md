---
title: Disk full
linkTitle: Disk full
headerTitle: Disk full issue
description: Learn how to address YugaByteDB node data drive full issues
menu:
  preview:
    parent: troubleshoot-nodes
    weight: 40
type: docs
---

This document describes the potential consequences and recommended actions when a YB-TServer or YB-Master encounters disk full conditions on the data drives.

## Overview

During the tablet bootstrap that takes place during a YB-TServer restart, a new WAL file is allocated, irrespective of the amount of logs generated in the server's previous WAL file. When the disk hosting the tablet WALs are low on space, the database will reject all data modification operations. Even delete of rows are rejected as YugabyteDB MVCC versioning requires a delete to write more data and then lazily garbage collect the dropped rows.

YugabyteDB data drive doesn't rely on the local root file system for storing data. Instead, YugaByteDB data directories are typically mounted on separate file systems to ensure isolation and manageability.

The flags `max_disk_throughput_mbps` and `reject_writes_min_disk_space_mb` determine the amount of disk space that is considered too low. The default value is 3GB.

The YB-TServer logs contain the following message when the system nears the threshold (18GB):

```output
W0829 15:35:51.325239 1986375680 log.cc:2186] Low disk space on yb-data/tserver/wals/table-7457ebcd745e4ea89375a30406f2c188/tablet-749e4d78244c43d2bba9cdb8505b732f/wal-000000001. Free space: 3518698209 bytes
```

and the following message when there is no more space left:

```output
0829 15:35:51.325239 1986375680 log.cc:2181] Not enough disk space available on yb-data/tserver/wals/table-7457ebcd745e4ea89375a30406f2c188/tablet-749e4d78244c43d2bba9cdb8505b732f/wal-000000001. Free space: 3518698209 bytes
```

## Issue

### Write to tablet rejected due to insufficient disk space

In YugabyteDB {{<release "2024.1.0.0">}} and later, insert, update, or delete to tables fail due to insufficient disk space with the following error:

```output
IO error (yb/tserver/tablet_service.cc:2288): Write to tablet 749e4d78244c43d2bba9cdb8505b732f rejected. Node 9a24d9c8493b44caa97ba4ae06e5829d has insufficient disk space
```

## Recommended recovery actions

**Option 1**: Increase capacity

YugabyteDB allows you to expand the cluster in an online manner. There are two ways to scale the cluster:

- Scale up
    You can scale up by switching to bigger machines, or by adding more storage disks. If you are using YugabyteDB Anywhere, follow the instructions in [expanding universe node disk capacity with YugabyteDB Anywhere](https://support.yugabyte.com/hc/en-us/articles/5463616207757-Expanding-universe-node-disk-capacity-with-YugabyteDB-Anywhere-platform)

- Scale out
    You can scale out by adding more nodes to the cluster. This will cause the YugabyteDB load balancer to rebalance the tablets and the data across more nodes.

**Option 2**: Remove unnecessary files from the nodes

Sometimes, unnecessary files can accumulate on the nodes including, large log files, or older core dumps.

Do not modify or remove the YugabyteDB data, bin, or config directories and files. Damage or loss of these files can result in unavailability and data loss.

**Option 3** Drop unnecessary tables and databases/namespaces

Dropping large tables, YSQL databases, or YCQL namespaces that are no longer necessary will help recover disk space.

When dropping tables on a database/namespace with [Point-in-time recovery](../../../manage/backup-restore/point-in-time-recovery/) enabled, the tables will be set in a HIDDEN state. It will not be cleaned up until all its snapshot schedules have expired.

Dropping YSQL tables require the YB-Master to have sufficient disk space, as it involves updating the YSQL catalog tables.

**Option 4** Disable database features that require more disk space

The following features result in more disk space usage. You can disable or turn these features off to reclaim space on the nodes.

- Time-to-live (TTL)
- Point-in-time recovery (PITR)
- xCluster
- Change Data Capture (CDC)

**Option 5** Run manual compaction

YugabyteDB performs automatic compaction of the data to keep the database running efficiently, and reduce disk space. Certain operations like bulk load of data, or deleting a large number of rows can cause a temporary spike in disk usage. If waiting for the automatic compaction task is not an option, then you can manually run the compaction task using the following:

- [Compact individual tables](../../../admin/yb-admin/#compact-table)
- [Compact individual tablets](../../../admin/yb-ts-cli/#compact-tablet)
