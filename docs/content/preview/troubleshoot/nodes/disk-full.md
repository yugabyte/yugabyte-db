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

YugabyteDB data drive doesn't rely on the local root file system for storing data. Instead, YugabyteDB data directories are typically mounted on separate file systems to ensure isolation and manageability.

The following flags are used to configure the data drive:

- [fs_data_dirs](../../../reference/configuration/yb-tserver/#fs-data-dirs)
- [log_dir](../../../reference/configuration/yb-tserver/#log-dir)

If any of the data drives is approaching disk full condition, then the inserts, updates, or deletes that write to the tablet are rejected due to insufficient disk space, and they would fail with the following error:

```output
IO error (yb/tserver/tablet_service.cc:2288): Write to tablet 749e4d78244c43d2bba9cdb8505b732f rejected. Node 9a24d9c8493b44caa97ba4ae06e5829d has insufficient disk space
```

The flags `max_disk_throughput_mbps` and `reject_writes_min_disk_space_mb` determine the amount of disk space that is considered too low. The default value is 3GB.

The YB-TServer logs contain the following message when the system nears the threshold (18GB):

```output
W0829 15:35:51.325239 1986375680 log.cc:2186] Low disk space on yb-data/tserver/wals/table-7457ebcd745e4ea89375a30406f2c188/tablet-749e4d78244c43d2bba9cdb8505b732f/wal-000000001. Free space: 3518698209 bytes
```

and the following message when there is no more space left:

```output
0829 15:35:51.325239 1986375680 log.cc:2181] Not enough disk space available on yb-data/tserver/wals/table-7457ebcd745e4ea89375a30406f2c188/tablet-749e4d78244c43d2bba9cdb8505b732f/wal-000000001. Free space: 3518698209 bytes
```

The following options prevents YSQL/YCQL writes from consuming additional disk space:

### Increase capacity

Scale the cluster to increase capacity. You can do this in two ways:

- Scale up

   Switch to bigger machines or add more storage disks. If you are using YugabyteDB Anywhere, follow the instructions in [Expanding universe node disk capacity with YugabyteDB Anywhere](https://support.yugabyte.com/hc/en-us/articles/5463616207757-Expanding-universe-node-disk-capacity-with-YugabyteDB-Anywhere-platform).

- Scale out

    Add more nodes to the cluster. The YugabyteDB load balancer will rebalance the tablets and the data across the added nodes.

### Remove unnecessary files from the nodes

Unnecessary files can accumulate on the nodes, including large log files or older core dumps.

Do not modify or remove the YugabyteDB data, bin, or config directories and files. Damage or loss of these files can result in unavailability and data loss.

### Drop unnecessary tables and databases/namespaces

Dropping large tables, YSQL databases, or YCQL namespaces that are no longer necessary will help recover disk space.

When dropping tables on a database/namespace with [Point-in-time recovery](../../../manage/backup-restore/point-in-time-recovery/) enabled, the tables are set to a HIDDEN state and won't be cleaned up until all their snapshot schedules have expired.

When dropping YSQL tables, ensure the YB-Master has enough disk space, as this involves updating the YSQL catalog tables.

### Disable database features that require more disk space

The following features require more disk space. You can disable or turn these features off, if the features are not used by any of the applications, to reclaim space on the nodes.

- Time-to-live (TTL)
- Point-in-time recovery (PITR)
- xCluster
- Change Data Capture (CDC)

### Run manual compaction

YugabyteDB automatically compacts data to keep the database running efficiently and reduce disk use. Certain operations, like bulk load of data or deleting a large number of rows, can cause a temporary spike in disk usage. If waiting for the automatic compaction task is not an option, you can manually run the compaction task using the following commands:

- [Compact individual tables](../../../admin/yb-admin/#compact-table)
- [Compact individual tablets](../../../admin/yb-ts-cli/#compact-tablet)

Note that regardless of the above options, it is possible that some other activity on the node (such as remote bootstrap) can end up taking up more space and use up the space on the disk.

The following sections describe the disk full scenarios and the recommended recovery actions.

## Scenarios

### The drive containing the log file is full

During the tablet bootstrap that takes place during a YB-TServer restart, a new WAL file is allocated, irrespective of the amount of logs generated in the server's previous WAL file. If a YB-TServer has several tablets and results in a crash loop, each one of the tablets creates new WAL files on every bootstrap, filling up the disk. The process is unable to create new log files at tablet bootstrap time, and this can be identified by looking at the `/var/log/messages` files.

### Disk full situation on other data drives

#### Crash loop due to write I/O error

If the disk isn't completely full (it probably has a few KBs of disk space left), the YB-TServer can successfully bootstrap. However, a subsequent write operation might fail with a fatal/crash error due to the disk getting full. This can lead to a continuous crash loop as the server restarts and encounters the same issue all over again.
An example IO error is as follows:

```output
consensus_queue.cc:368] Check failed: _s.ok() Bad status: IO error (yb/util/env_posix.cc:504): /mnt/d1/yb-data/tserver/wals/table-6e527d21c179455286ae94ddcbebd267/tablet-be46450d80d049ee8b6a0780dfa54926/.tmp.newsegmentiS4vyN: No space left on device (system error 28)
```

#### Automatic YB-TServer blacklisting

During YB-TServer startup, the FSManager (File System Manager) module checks the health of each data drive by creating a small test file. If the disk is completely full, the YB-TServer does the following:

1. Skips the full drive: The test file creation fails, the node ignores this particular data drive, and marks it faulty. It also increments the `drive_fault` metric.

1. Reports faulty drive: YB-TServer informs YB-Master about the faulty drive issue. The YB-Master server then blacklists the YB-TServer with the faulty drive, preventing it from creating any new tablets.

1. Potential tablet migration: The load balancer might load balance tablets from the blacklisted YB-TServer to other YB-TServers for {{<release "2.20.3.0">}} or {{<release "2024.1.0.0">}} and later. For earlier releases, the YB-TServer can run into the following fatal error:

    ```output
    Log line format: [IWEF]mmdd hh:mm:ss.uuuuuu threadid file:line] msg
    F0119 04:08:47.225858 46656 tablet_server_main_impl.cc:241] Already present (yb/tserver/ts_tablet_manager.cc:1554): Could not init Tablet Manager: State transition of tablet d1e9c6c53fa9410c86d3505073305b41 already in progress:     {d1e9c6c53fa9410c86d3505073305b41, opening tablet}
    ```

## Recommended recovery actions

The following two recommendations are specific to [YugabyteDB Anywhere](../../../yugabyte-platform/).

**Option 1**: Increase the disk volume size using the [Edit Universe](../../../yugabyte-platform/manage-deployments/edit-universe/#edit-a-universe) option. Note the following important considerations:

- Preflight checks - YugabyteDB Anywhere performs preflight checks to ensure a smooth [smart resize](../../../yugabyte-platform/manage-deployments/edit-universe/#smart-resize) operation. One of the checks verifies there is no leaderless tablet in the cluster. To bypass this check, you can disable the `yb.checks.leaderless_tablets.enabled` global [runtime configuration flag](../../../yugabyte-platform/administer-yugabyte-platform/manage-runtime-config/) as a Super Admin user.

- Master leader - The universe needs to have a designated master leader node to receive and process node resize requests. If no master leader is available, the resize operation will fail. After the disk space is successfully increased, the bootstrap process for the resized nodes should proceed without issues.

**Option 2**: Add a new node using the [Edit Universe](../../../yugabyte-platform/manage-deployments/edit-universe/#edit-a-universe) option.

Note that in a cluster with a [replication factor](../../../architecture/docdb-replication/replication/#replication-factor) (RF) of N, if more than N/2 nodes fail, adding a new node will fail. To recover from this situation, you need to bring some failed nodes back online (using Option 1 or Option 2) to form a quorum, and then add new nodes. If that's not possible, you might need to manually perform an unsafe configuration change.
