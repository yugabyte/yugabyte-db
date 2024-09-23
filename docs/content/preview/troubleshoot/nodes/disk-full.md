---
title: Disk Full
linkTitle: Disk Full
headerTitle: Disk Full issue
description: Learn how to address YugaByteDB node data drive full issues
menu:
  preview:
    parent: troubleshoot-nodes
    weight: 40
type: docs
---

This document describes the potential consequences and recommended actions when a YB-TServer or YB-Master encounters disk full conditions on the data drives. The consequence of data drive full depends on whether the full data drive stores YB-TServer or YB-Master log files.

YugabyteDB data drive doesn't rely on the local root file system for storing data. Instead, YugaByteDB data directories are typically mounted on separate file systems to ensure isolation and manageability.

The following flags are used to configure the data drive:

- [fs_data_dirs](../../../reference/configuration/yb-tserver/#fs-data-dirs)
- [log_dir](../../../reference/configuration/yb-tserver/#log-dir)

## Scenarios

### The drive containing the log file is full

During the tablet bootstrap that takes place during a YB-TServer restart, a new WAL file is allocated, irrespective of the amount of logs generated in the server's previous WAL file. If a YB-TServer has several tablets and results in a crash loop, each one of the tablets creates new WAL files on every bootstrap, filling up the disk.

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

**Option 1**: Check if any files (like older YB-TServer logs) on the `log_dir` drive can be removed/deleted to help the YB-TServer process to bootstrap successfully.

**Option 2**: Starting with {{<release "2.18.1.0">}}, you can prevent repeated WAL file allocation in a crash loop by using the [--reuse_unclosed_segment_threshold_bytes](../../../reference/configuration/yb-tserver/#reuse-unclosed-segment-threshold-bytes) flag.

If a tablet's last WAL file size is less than or equal to this threshold value, the bootstrap process will reuse the last WAL file rather than create a new one. To set the flag so that the last WAL file is always reused, you can set the flag to the current maximum WAL file size (64MB), as follows:

```sh
reuse_unclosed_segment_threshold_bytes=67108864
```

The following two options are recommendations specific to [YugabyteDB Anywhere](../../../yugabyte-platform/).

**Option 3**: Increase the disk volume size through YugabyteDB Anywhere [edit universe](../../../yugabyte-platform/manage-deployments/edit-universe/#edit-a-universe) UI. Note that there are a few important considerations:

- Preflight checks : YugabyteDB Anywhere UI performs preflight checks to ensure a smooth [smart resize](../../../yugabyte-platform/manage-deployments/edit-universe/#smart-resize) operation. One of the checks verifies there is no leaderless tablet in the cluster. To bypass this check, you can disable the [runtime Global Configuration flag](../../../yugabyte-platform/administer-yugabyte-platform/manage-runtime-config/) `yb.checks.leaderless_tablets.enabled` as a Super Admin user.

- Master leader requirement: A designated master leader node is essential within the YugabyteDB Anywhere universe to receive and process node resize requests. If no master leader is available, the resize operation will fail. After the disk space is successfully increased, the bootstrap process for the resized nodes should proceed without issues.

**Option 4**: Add a new node through YugabyteDB Anywhere [edit universe](../../../yugabyte-platform/manage-deployments/edit-universe/#edit-a-universe) UI.

Note that in a cluster with a [replication factor](../../../architecture/docdb-replication/replication/#replication-factor) (RF) of N, if more than N/2 nodes fail, adding a new node operation will fail. To recover from this situation, you need to bring some failed nodes back online (using Option 1, Option 2, or Option 3) to form the quorum first, and then add new nodes. If that's not possible, you might need to manually perform an unsafe configuration change.

{{<warning title="Important: Accidental Disk Unmount">}}

During internal testing (refer to GitHub issue [#22120](https://github.com/yugabyte/yugabyte-db/issues/22120)), it was identified that that accidentally unmounting a YugaByteDB data disk can potentially lead to data inconsistencies and cause the YB-TServer to enter a crash loop state.

The testing indicated that unmounting a disk followed by a delayed remount resulted in a window of 15-90 minutes where the YB-TServer process could still access the empty directory. This sequence could cause the server to crash and trigger data recovery mechanisms that recreate missing tablets on a different disk. If the original disk is remounted later, duplicate tablets will be detected, causing the server to crash repeatedly.

**Recommendation**<br>
To minimize the risk of data inconsistencies, it is recommended that you follow best practices for disk management and avoid accidental unmounts of data volumes.

{{</warning>}}
