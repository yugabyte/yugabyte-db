---
title: Antivirus and Endpoint Scanning Recommendations
headerTitle: Antivirus and endpoint scanning
linkTitle: Antivirus recommendations
headContent: Recommendations for configuring antivirus software for YugabyteDB
description: Recommendations for configuring antivirus software for YugabyteDB
menu:
  stable_faq:
    identifier: antivirus
    parent: faq
    weight: 200
type: docs
unversioned: true
rightNav:
  hideH4: true
---

Most endpoint protection tools (AV/EDR) work by intercepting file and process activity at the OS level. Common mechanisms include:

- Real-time (on-access) scanning

    Files are scanned when created, opened, modified, or deleted. In database workloads, this applies to every write, flush, compaction, and log operation.

- Process and behaviour monitoring

    Security agents observe patterns such as sustained high I/O, frequent file rewrites, and memory activity. Database engines naturally exhibit these patterns during regular operation.

- File handling interference (in some configurations)

    Scanners may briefly hold files open, delay close/unlink operations, or apply remediation actions. Even short interruptions at the filesystem layer can affect database stability and latency.

These behaviours are common across enterprise security products and are widely recognised as incompatible with high-throughput database storage paths unless exclusions are configured.

Use the following recommendations to minimize issues when running endpoint protection tools on VMs hosting YugabyteDB or YugabyteDB Anywhere.

## YugabyteDB

Database workloads generate sustained high-frequency file activity due to:

- RocksDB compactions and SST rewrites
- Write-ahead logging (WAL)
- Tablet splits and metadata updates
- Snapshot and backup operations
- Continuous log writes

Real-time scanning of these paths can result in:

- Increased query latency and reduced throughput
- Higher CPU and disk I/O usage
- Unpredictable stalls during compactions or flush operations
- In some environments, filesystem-level contention or file locks that interfere with database operations

These behaviours are commonly documented across enterprise databases and are not unique to YugabyteDB.

### Recommended approach

If the security policy requires anti-malware software to stay enabled, configure explicit exclusions for all YugabyteDB data and log directories: ATA storage (RocksDB files, WAL, intents, metadata), logs, local snapshots and backup staging areas.

Default paths (adjust to match your deployment):

- /home/yugabyte/yb-data/
- /mnt/*/yb-data/
- /data*/yb-data/
- /var/lib/yugabyte/
- /home/yugabyte/master/logs/
- /home/yugabyte/tserver/logs/
- /home/yugabyte/controller/logs
- Any custom directories configured via:

  - [--fs_data_dirs](../../reference/configuration/yb-tserver/#fs-data-dirs)
  - [--fs_wal_dirs](../../reference/configuration/yb-tserver/#fs-wal-dirs)
  - [--log_dir](../../reference/configuration/yb-tserver/#log-dir)

- /opt/yugabyte/node-agent/logs or /home/yugabyte/node-agent/logs

If your deployment uses custom mount points or non-default layouts, ensure those locations are excluded as well.

### What should not typically be excluded

To preserve security visibility, exclusions should not usually include:

- YugabyteDB binaries (yb-master, yb-tserver, controller, node-agent, libraries)
- Configuration directories
- Operating system paths such as /usr, /etc, /lib
- Backup repositories (unless files and directories are regularly rewritten and the performance impact has been identified).

If required, YugabyteDB Support can assist in validating an exclusion list based on your deployment configuration.

## YugabyteDB Anywhere

YugabyteDB Anywhere is the control plane that orchestrates universes, manages backups, executes automation tasks, handles credentials, and coordinates node agents. While YugabyteDB Anywhere is not on the hot data path like a YB-TServer, it still performs frequent file, process, and network activity that can be disrupted by aggressive antivirus (AV), EDR, or endpoint scanning policies.

YugabyteDB Anywhere hosts typically perform:

- Continuous automation via Ansible/task runners
- Frequent creation and deletion of temporary files
- Persistent writes to:
  - Platform logs
  - Backup metadata
  - Task state
  - Certificate material
  - Support bundles
- Execution of sub-processes (such as ssh, scp, rsync, tar, openssl)
- Interaction with Node Agents across customer environments

Real-time scanning can interfere with this behaviour and may result in:

- Failed or stalled tasks (provisioning, upgrades, backups)
- Timeouts during universe operations
- Slower platform responsiveness
- In rare cases, corruption of temporary working directories during task execution

### Recommended approach

If security policy requires AV/EDR to remain enabled, configure path exclusions for YugabyteDB Anywhere's high-churn directories.

Exclude directories used for YugabyteDB Anywhere data, logs, backups, metadata, temporary working directories, support bundle generation, and runtime artifacts.

Typical YugabyteDB Anywhere paths for installations using the default layout:

- /opt/yugabyte/, including the sub-directories
  - /opt/yugabyte/yugaware/
  - /opt/yugabyte/yugaware/data/
  - /opt/yugabyte/yugaware/logs/
  - /opt/yugabyte/yugaware/releases/
  - /opt/yugabyte/yugaware/storage/
  - /opt/yugabyte/yugaware/tmp/
- /tmp/yugabyte-* (temporary execution artifacts)

If you use custom paths (that is, non-default install directories), ensure those are included.

### YugabyteDB Anywhere-specific considerations

Some YugabyteDB Anywhere workflows are particularly sensitive to scanning interference:

- Universe provisioning (frequent SSH and file staging operations)
- Software upgrades (temporary extraction and replacement of large archive files)
- Backup orchestration (parallel metadata writes)
- Support bundle generation (large file aggregation and compression)
- Certificate rotation workflows

In environments where scanning interferes, symptoms often appear as:

- Intermittent task failures without a clear root cause
- Operations that succeed on retry
- Unexpected timeouts in the YugabyteDB Anywhere UI
- Tasks stuck in a Running status despite no visible progress

These symptoms frequently resolve once exclusions are applied.

### What should not typically be excluded

To preserve security posture, exclusions should not typically include:

- Operating system paths (/usr, /etc, /lib, etc.)
- System package managers
- User home directories unrelated to YugabyteDB Anywhere
- External backup repositories (unless confirmed as high-churn and problematic)

Scope exclusions to YugabyteDB Anywhere-specific directories only.
