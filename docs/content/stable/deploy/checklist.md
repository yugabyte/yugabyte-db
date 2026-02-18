---
title: Deployment checklist for YugabyteDB clusters
headerTitle: Deployment checklist
linkTitle: Deployment checklist
description: Checklist to review system requirements, configuration details, and so on, when deploying the YugabyteDB database to production or for performance testing.
menu:
  stable:
    identifier: checklist
    parent: deploy
    weight: 10
type: docs
---

A YugabyteDB cluster (also referred to as a [universe](../../architecture/key-concepts/#universe)) consists of two distributed services - the [YB-TServer](../../architecture/yb-tserver/) service and the [YB-Master](../../architecture/yb-master/) service. Because the YB-Master service serves the role of the cluster metadata manager, it should be brought up first, followed by the YB-TServer service. To bring up these distributed services, the respective servers (YB-Master or YB-TServer) need to be started across different nodes. There is a number of topics to consider and recommendations to follow when starting these services.

{{< warning title="Antivirus and endpoint scanning" >}}
Antivirus and endpoint scanning software can potentially impact the operation of YugabyteDB. Refer to [Antivirus and endpoint scanning](../../faq/antivirus/) for recommendations on using antivirus and endpoint scanning tools.
{{< /warning >}}

## Basics

- YugabyteDB supports both x86 and ARM (aarch64) CPU architectures.
- YugabyteDB is supported on a variety of [operating systems](../../reference/configuration/operating-systems/). For production workloads, the recommended operating systems are AlmaLinux 8 and RHEL 8.
- The appropriate system limits should be set using [ulimit](../manual-deployment/system-config/#set-ulimits) on each node running a YugabyteDB server.
- [chrony](../manual-deployment/system-config#set-up-time-synchronization) should be used to synchronize time among the machines.

## Replication

YugabyteDB internally replicates data in a consistent manner using the Raft consensus protocol to survive node failure without compromising data correctness. This distributed consensus replication is applied at a per-shard (also known as tablet) level similar to Google Spanner.

The replication factor (RF) corresponds to the number of copies of the data. You need at least as many nodes as the RF, which means one node for RF 1, three nodes for RF 3, and so on. With a RF of 3, your cluster can tolerate one node failure. With a RF of 5, it can tolerate two node failures. More generally, if RF is n, YugabyteDB can survive floor((n - 1) / 2) failures without compromising correctness or availability of data.

See [Fault tolerance](../../architecture/docdb-replication/replication/#fault-tolerance) for more information.

When deploying a cluster, keep in mind the following:

- The default replication factor is 3.
- The number of YB-Master servers running in a cluster should match RF. Run each server on a separate machine to prevent losing availability on failures. You need to specify the RF using the `--replication_factor` flag when bringing up the YB-Master servers.
- The number of YB-TServer servers running in the cluster should not be less than the RF. Run each server on a separate machine to prevent losing availability on failures.
- An even RF number offers the same fault tolerance as its preceding odd number. For example, both RF 4 and RF 3 can only tolerate the loss of 1 node. So to keep costs low, it's preferable to use an odd RF number.

Note that YugabyteDB works with both hostnames or IP addresses. The latter are preferred at this point, as they are more extensively tested.

See the [yb-master command reference](../manual-deployment/start-masters/) for more information.

## Hardware requirements

YugabyteDB is designed to run on bare-metal machines, virtual machines (VMs), and containers.

### CPU and RAM

You should allocate adequate CPU and RAM. YugabyteDB has adequate defaults for running on a wide range of machines, and has been tested from 2 core to 64 core machines, and up to 200GB RAM.

**Minimum requirement**

- 2 cores
- 2GB RAM

**Production requirement**

- YCQL - 16+ cores and 32GB+ RAM
- YSQL - 16+ cores and 64GB+ RAM

Add more CPU (compared to adding more RAM) to improve performance.

**Additional considerations**

For typical Online Transaction Processing (OLTP) workloads, YugabyteDB performance improves with more aggregate CPU in the cluster. You can achieve this by using larger nodes or adding more nodes to a cluster. Note that if you do not have enough CPUs, this will manifest itself as higher latencies and eventually dropped requests.

Memory depends on your application query pattern. Writes require memory but only up to a certain point (for example, 4GB, but if you have a write-heavy workload you may need a little more). Beyond that, more memory generally helps improve the read throughput and latencies by caching data in the internal cache. If you do not have enough memory to fit the read working set, then you will typically experience higher read latencies because data has to be read from disk. Having a faster disk could help in some of these cases.

YugabyteDB explicitly manages a block cache, and does not need the entire data set to fit in memory. It does not rely on the OS to keep data in its buffers. If you provide YugabyteDB sufficient memory, data accessed and present in block cache stays in memory.

### Memory and tablet limits

For a cluster with [RF3](../../architecture/key-concepts/#replication-factor-rf), 1000 tablets imply 3000 tablet replicas. If the cluster has three nodes, then each node has on average 1000 tablet replicas. A six node cluster would have on average 500 tablet replicas per-node, and so on.

Each 1000 tablet replicas on a node impose an overhead of 0.4 vCPUs for Raft heartbeats (assuming a 0.5 second heartbeat interval), and 800 MiB of memory.

The overhead is proportional to the number of tablet replicas, so 500 tablet replicas would need half as much.

Additional memory will be required for supporting caches and the like if the tablets are being actively used. We recommend provisioning an extra 6200 MiB of memory for each 1000 tablet replicas on a node to handle these cases; that is, a TServer should have 7000 MiB of RAM allocated to it for each 1000 tablet replicas it may be expected to support.

You can manually provision the amount of memory each TServer uses by setting the [--memory_limit_hard_bytes](../../reference/configuration/yb-tserver/#memory-limit-hard-bytes) or [--default_memory_limit_to_ram_ratio](../../reference/configuration/yb-tserver/#default-memory-limit-to-ram-ratio) flags.

{{<note title = "Kubernetes deployments">}}
For Kubernetes universes, memory limits are controlled via resource specifications in the Helm chart. Accordingly, `--default_memory_limit_to_ram_ratio` does not apply, and `--memory_limit_hard_bytes` is automatically set from the Kubernetes pod memory limits.

See [Memory limits in Kubernetes deployments](../kubernetes/single-zone/oss/helm-chart/#memory-limits-for-kubernetes-deployments) for details.
{{</note>}}

#### YSQL

Manually provisioning is a bit tricky as you need to take into account how much memory the kernel needs as well as the PostgreSQL processes and any Master process that is going to be colocated with the TServer.

Accordingly, it is recommended that you instead use the [--use_memory_defaults_optimized_for_ysql](../../reference/configuration/yb-tserver/#use-memory-defaults-optimized-for-ysql) flag, which gives good memory division settings for using YSQL optimized for your node's size.

The flag does the following:

- Automatically sets memory division flag defaults to provide much more memory for PostgreSQL, and optimized for the node size. For details on memory flag defaults, refer to [Memory division flags](../../reference/configuration/yb-tserver/#memory-division-flags).
- Enforces tablet limits based on available memory. This limits the total number of tablet replicas that a cluster can support. If you try to create a table whose additional tablet replicas would bring the total number of tablet replicas in the cluster over this limit, the create table request is rejected. For more information, refer to [Tablet limits](../../architecture/docdb-sharding/tablet-splitting/#tablet-limits).

{{< tip title="Tip" >}}

To view the number of live tablets and the limits, open the **YB-Master UI** (`<master_host>:7000/`) and click the **Tablet Servers** tab. Under **Universe Summary**, the total number of live tablet replicas is listed as **Active Tablet-Peers**, and the limit as **Tablet Peer Limit**.

![Tablet limits](/images/admin/master-tablet-limits.png)

{{< /tip >}}

(Note that although the default setting is false, when creating a new cluster using yugabyted or YugabyteDB Anywhere, the flag is set to true, unless you explicitly set it to false.)

Given the amount of RAM devoted to per tablet overhead, it is possible to compute the maximum number of tablet replicas. The following table shows sample values of node RAM versus maximum tablet replicas. You can use these values to estimate how big of a node you will need based on how many tablet replicas per server you want supported.

| total node GiB | max number of tablet replicas | max number of PostgreSQL connections |
| ---: | ---: | ---: |
|   4 |    240 |  30 |
|   8 |    530 |  65 |
|  16 |  1,250 | 130 |
|  32 |  2,700 | 225 |
|  64 |  5,500 | 370 |
| 128 | 11,000 | 550 |
| 256 | 22,100 | 730 |

These values are approximate because different kernels use different amounts of memory, leaving different amounts of memory for the TServer and thus the per-tablet overhead TServer component.

Also shown is an estimate of how many PostgreSQL connections that node can handle assuming default PostgreSQL flags and usage.  Unusually memory expensive queries or preloading PostgreSQL catalog information will reduce the number of connections that can be supported.

Thus a 8 GiB node would be expected to be able support 530 tablet replicas and 65 (physical) typical PostgreSQL connections.  A cluster of six of these nodes would be able to support 530 \* 2 = 1,060 [RF3](../../architecture/key-concepts/#replication-factor-rf) tablets and 65 \* 6 = 390 typical physical PostgreSQL connections assuming the connections are evenly distributed among the nodes.

#### YCQL

If you are not using YSQL, ensure the [use_memory_defaults_optimized_for_ysql](../../reference/configuration/yb-master/#use-memory-defaults-optimized-for-ysql) flag is set to false. This flag optimizes YugabyteDB's memory setup for YSQL, reserving a considerable amount of memory for PostgreSQL; if you are not using YSQL then that memory is wasted when it could be helping improve performance by allowing more data to be cached.

Note that although the default setting is false, when creating a new cluster using yugabyted or YugabyteDB Anywhere, the flag is set to true, unless you explicitly set it to false.

### Verify support for SSE2 and SSE4.2

YugabyteDB requires the SSE2 instruction set support, which was introduced into Intel chips with the Pentium 4 in 2001 and AMD processors in 2003. Most systems produced in the last several years are equipped with SSE2.

In addition, YugabyteDB requires SSE4.2.

To verify that your system supports SSE2, run the following command:

```sh
cat /proc/cpuinfo | grep sse2
```

To verify that your system supports SSE4.2, run the following command:

```sh
cat /proc/cpuinfo | grep sse4.2
```

## Disks

- SSDs (solid state disks) are required.

  - YugabyteDB Anywhere additionally supports the use of GCP Hyperdisks when deploying universes on GCP (specifically, the Balanced and Extreme options).

    Note: These disk types are only available in some GCP regions.

- Both local or remote attached storage work with YugabyteDB. Because YugabyteDB internally replicates data for fault tolerance, remote attached storage which does its own additional replication is not a requirement. Local disks often offer better performance at a lower cost.
- Multi-disk nodes:

  - Do not use RAID across multiple disks. YugabyteDB can natively handle multi-disk nodes (JBOD).
  - Create a data directory on each of the data disks and specify a comma separated list of those directories to the YB-Master and YB-TServer servers via the `--fs_data_dirs` flag.

- Mount settings:

  - XFS is the recommended filesystem.
  - Use the `noatime` setting when mounting the data drives.
  - ZFS is not currently supported.
  - NFS is not currently supported.

YugabyteDB does not require any form of RAID, but runs optimally on a JBOD (just a bunch of disks) setup.
YugabyteDB can also leverage multiple disks per node and has been tested beyond 20 TB of storage per node.

Write-heavy applications usually require more disk IOPS (especially if the size of each record is larger), therefore in this case the total IOPS that a disk can support matters. On the read side, if the data does not fit into the cache and data needs to be read from the disk in order to satisfy queries, the disk performance (latency and IOPS) will start to matter.

YugabyteDB uses per-tablet [size tiered compaction](../../architecture/yb-tserver/). Therefore the typical space amplification in YugabyteDB tends to be in the 10-20% range.

YugabyteDB stores data compressed by default. The effectiveness of compression depends on the data set. For example, if the data has already been compressed, then the additional compression at the storage layer of YugabyteDB will not be very effective.

It is recommended to plan for about 20% headroom on each node to allow space for miscellaneous overheads such as temporary additional space needed for compactions, metadata overheads, and so on.

### Ephemeral disks

{{<tags/feature/ea idea="2298">}} Ephemeral or local disks are physically attached storage that exists only for the life of the virtual machine (VM). They deliver extremely fast I/O but data is not persistent. If a VM stops, hard reboots, or terminates, its local disk data is lost. Only the boot disk (with the OS) is persistent, but the database data directory on the ephemeral disk will be wiped.  On the major public clouds, such disks are variously called "Instance Stores" (on AWS), "Local SSDs" (on GCP), and "Temporary Disks" (on Azure).

Ephemeral disk support is limited to [manual YugabyteDB deployments](../manual-deployment/) and [YugabyteDB Anywhere with an on-premises provider](../../yugabyte-platform/configure-yugabyte-platform/on-premises/). It is not supported for public cloud providers (AWS, GCP, or Azure) in YugabyteDB Anywhere. Refer to [Additional tips](#additional-tips).

Ephemeral storage is an excellent choice for YugabyteDB if your primary goal is to optimize for low-latency read-heavy workloads. Our internal benchmarks show that using local SSDs instead of remote, network-attached cloud disks can reduce latency for read-heavy operations by up to 30%. This performance gain can be significant for applications where every millisecond of latency is critical.

However, if performance is not your primary concern, network-attached storage offers more reliability and is easier to manage.

#### Key considerations and risks

Operating a distributed database on ephemeral storage requires careful planning. There's also an increased risk of multiple node failures leading to data loss or downtime.

- Data loss: Data stored on an ephemeral disk is not persistent. The data is wiped out in the following scenarios:

  - VM stop/start and (in some clouds) hard reboot. If a YugabyteDB node's VM is stopped and started (simulating a power off, and then power on), its ephemeral disk will be empty. On some clouds (for example, Azure), hard reboots (that is, BIOS reset commands that force reset a server) may also result in an empty ephemeral disk.

  - Unplanned events. Events like power outages or host failures result in lost data on the local disk. While the node's OS and binaries (on the boot disk) may survive, all YugabyteDB data on the ephemeral volume is lost.

  - Planned operations. Any planned operations that cause VMs to start, stop, or reboot will wipe the VMs' ephemeral disks.  While Yugabyte software does not (and cannot) directly trigger such VM actions (whether YugabyteDB is deployed manually or managed using YugabyteDB Anywhere), be aware that some user operations, such as disaster recovery (DR) testing or switchover can trigger such VM actions and be problematic.

- Double fault scenarios: The probability of a "double fault" (losing two nodes at once) is higher with ephemeral storage. This is because nodes with ephemeral disks take longer to recover. For example, if a node goes down and then revives within 15 minutes, if it has a persistent disk, it can recover quickly via an incremental (WAL-based) replication. But nodes with ephemeral disks require much longer (minutes to hours) to recover because they must perform a full (and not incremental) re-replication of all data destined for that node. This extended recovery window also extends the window in which a double fault scenario could happen. Losing two out of three replicas (in an RF3 configuration of a cluster or a tablespace) can result in a majority loss, leading to potential data loss.

- Operational complexity: Certain standard operations become dangerous with ephemeral disks. For instance, shutting down all nodes in a cluster with persistent disks is safe, but doing the same with an ephemeral disk cluster will result in total data loss. Operational workflows must be carefully reviewed to avoid these situations.

- Higher reliance on backups: Because the probability of a majority failure (such as a double fault in an RF3 configuration) is higher, the need to restore from a backup increases. High frequency backups are recommended; YugabyteDB Anywhere allows for incremental backups as frequently as every 15 minutes.

#### Universe design and configuration best practices

To safely and effectively use ephemeral disks with YugabyteDB, follow these design and configuration guidelines:

- Have enough replication and redundancy: Always use a minimum replication factor (RF) of 3 for any production universe (and for all tablespaces for which you might override the RF) when using ephemeral disks. Due to the increased risk of double fault scenarios, RF 5 or greater is recommended if your environment and performance tolerances allow. An RF 5 deployment can survive the failure of two fault domains without downtime (at the cost of more storage and write overhead).

- Distribute across zones: Spread nodes across availability zones so that a single zone outage won't result in all replicas of a tablet being dropped, causing data loss. Likewise, if you are using tablespaces to override replication settings for particular tables, spread each tablespace across multiple zones.

- Persistent boot disk for OS and binaries: Configure YugabyteDB nodes so that the YugabyteDB software, configuration files, and logs reside on a persistent disk, and only the data directory is on the ephemeral disk. This design allows for quicker provisioning of nodes back into the cluster. In addition, put logs on an independent persistent disk that's not the boot disk. Keeping logs persistent retains debugging information in case of a failure, and placing them on an independent, non-boot disk avoids the possibility of filling up the boot disk.

- Use dedicated VMs with persistent disks for Masters: Unless [Automatic YB-Master failover](../../yugabyte-platform/manage-deployments/remove-nodes/#automatic-yb-master-failover) is enabled, it is strongly recommended that you place Master nodes on dedicated VMs that use persistent disks, for the following reasons:

  - Master processes hold highly critical data.
  - Manual recovery from the loss of a node with a Master is time consuming and may require assistance from Support.
  - There are no performance gains from having Masters use ephemeral disks, as it does not store any user data, and all its data fits in memory.

- Maintain spare capacity (Free pool nodes): Provision spare nodes that are ready to join the cluster in case of a failure. This reduces the time a tablet remains under-replicated. With extra spare capacity in your cluster, in the event multiple nodes fail serially in sequence over time, the universe can recover automatically (via YugabyteDB's automatic re-replication that starts after 15 minutes of the node being down) rather than manually (relying on human intervention to add nodes and storage capacity).

  - Spare node rotation: If you maintain a free pool of spare nodes, be sure to apply [OS patches](../../yugabyte-platform/manage-deployments/upgrade-nodes/) to those spare VMs regularly.

- Monitoring and alerts: Enable robust monitoring on the cluster so you are immediately alerted to node failures. YugabyteDB Anywhere provides extensive monitoring and alerting using a built-in Prometheus instance.

- Frequent backups: Increase your backup frequency when using ephemeral storage. YugabyteDB Anywhere allows for incremental backups as frequently as every 15 minutes. Also, consider backing up to a diversity of backup storage, and performing regular test restores.

#### Operational best practices and workflows

Working with ephemeral storage requires careful attention to standard operational workflows. The following are best practices for common scenarios.

##### Rolling restart of servers

[Rolling restarts](../../yugabyte-platform/manage-deployments/edit-config-flags/#modify-configuration-flags) in YugabyteDB Anywhere do not reboot the machine or unmount disks, so ephemeral data remains intact. You can perform rolling restarts as usual.

##### Operating system patching and node reboots

In YugabyteDB Anywhere, you perform OS patching for on-premises universes via scripts that call YugabyteDB Anywhere APIs. See [Patch and upgrade the Linux operating system](../../yugabyte-platform/manage-deployments/upgrade-nodes/) for the recommended workflow.

When patching or performing maintenance, if you must reboot the OS on a node, be sure to drain or remove the node from the cluster first. The recommended approach is to remove and then re-add the node.

- Option 1: Blacklist the node Before Reboot (Drain): Blacklist the node, wait for draining to complete, reboot, and then remove the blacklist.
- Option 2: (Recommended) Remove and add node: Remove the node from the cluster, patch or rebuild its OS, and then add the node back to the universe.

When using either approach, work on one node at a time and wait for the cluster to return to full replication of tablets based on their RF before moving to the next node.

##### Unplanned node outage (Failure scenario)

In the event of an unplanned outage, assume the node's data is lost and proceed with replacing the node (in YugabyteDB Anywhere, use the **Replace Node** action). If the node does come back on its own and starts to rejoin, monitor it closely.

##### Pause universe or full-cluster shutdown

This operation is not supported or safe with ephemeral disks. Stopping all nodes means every node's local disk is wiped, resulting in total data loss. Never shut down all nodes simultaneously.

YugabyteDB Anywhere does not perform this action for universes created using an on-premises provider. But you must ensure your own automation scripts also avoid this.

##### Software upgrades (YugabyteDB version upgrades)

In YugabyteDB Anywhere, the [Upgrade Database Version](../../yugabyte-platform/manage-deployments/upgrade-software/) option does not reboot the machine or unmount disks, and is therefore safe. As per standard guidance during this operation, regardless of whether persistent or ephemeral disks are used, exercise caution in case a node fails, as this may lead to a double fault scenario.

##### Scaling and adding/removing nodes

- [Vertical scaling](../../explore/linear-scalability/horizontal-vs-vertical-scaling/#vertical-scale-up) (Instance type change to add memory, CPU or storage): For YugabyteDB Anywhere on-premises universes, vertical scaling (resizing a VM) is a manual operation.

    You cannot detach and re-attach ephemeral disks to a new VM. Instead, perform a full move: add new nodes, and then remove the old ones.

  1. Add a new instance type to the on-premises provider for the new VM type.
  1. Add new nodes of this type to match the current universe count.
  1. Initiate an Edit universe operation to move from the old instance type to the new one. This triggers a full data migration to the new nodes.

  Note that as vertical scaling operations may introduce VM reboots on cloud providers, consider [horizontal scaling](../../explore/linear-scalability/horizontal-vs-vertical-scaling/#horizontal-scale-out) (scale out by adding nodes) instead of vertically scaling nodes.

- Disk scaling: Ephemeral disk sizes are fixed due to cloud provider limitations. To increase cluster capacity, add nodes rather than expanding disks.

- Removing nodes: Always blacklist nodes to safely drain data before removal. Note that ephemeral data is permanently lost when nodes are removed.

##### Additional tips

- Automate: To reduce human error and streamline responses during situations like node outages, write scripts or use the YugabyteDB Anywhere API to automate key workflows, such as adding, removing, and blacklisting nodes.

- Understand Cloud Provider-specific behavior: The behavior of ephemeral disks can vary between cloud providers. For example, some platforms, such as GCP, may have specific features that allow data on an ephemeral disk to be preserved for a short period under certain conditions. See [GCP documentation](https://cloud.google.com/compute/docs/disks/local-ssd#data_persistence) for more information.

- Batched rolling operations

  - YugabyteDB Anywhere rolling operations that can be batched, such as database software upgrades or flag configuration changes, are safe to perform in parallel with ephemeral disks. These operations only restart processes and do not stop or start the underlying VMs.

  - User-driven operations: For user-driven rolling operations like OS patching or vertical scaling, only one node can be processed at a time. Performing these operations on multiple nodes in a single batch is not currently supported, as extreme caution is required to ensure a majority of replicas are not lost, which could lead to downtime or data loss.

- Special considerations for GCP-managed instance groups: GCP's Managed Instance Groups (MIGs) can simplify operations like autoscaling, self-healing, and rolling upgrades. However, you must be cautious when using them with ephemeral disks. If a MIG's health check fails, it may simultaneously replace multiple nodes. This could lead to a "double fault" scenario (losing two or more replicas at once), which can cause cluster or tablespace downtime.

  While an outage of this nature is recoverable with persistent disks, it would be unrecoverable with ephemeral disks.

The following table summarizes features supported and restrictions with ephemeral disks.

| Action | Support Level | Notes / recommended practices |
| :--- | :--- | :--- |
| Rolling operations (Restarts, upgrades, flag changes) | {{<icon/yes>}} | Follow standard rolling procedures carefully. |
| Add/remove node (Scaling) | {{<icon/yes>}} | Primary method; ensure data is safely drained beforehand. |
| Backups and restores | {{<icon/yes>}} (Recommended) | Critical for recovery from catastrophic data loss. |
| Read Replicas / Asynchronous Replication | {{<icon/yes>}} | Normal operation; avoid simultaneous downtime. |
| OS Patches / reboots | {{<icon/partial>}} (Modified procedure required) | Always drain or remove nodes before rebooting. |
| Vertical scaling | {{<icon/partial>}} (Modified procedure required) | Only full node replacements; no smart/in-place resizing. |
| Multiple simultaneous failures | {{<icon/partial>}} (Use caution) | Handle failures sequentially; backup restore if multiple failures occur. |
| Pause universe / full stop | {{<icon/no>}} (Disabled by YugabyteDB Anywhere) | Disallowed—will cause total data loss. |
| Non-rolling all-node operations | {{<icon/no>}} (Disabled by YugabyteDB Anywhere) | Must convert to rolling/per-node actions. |
| Using RF 1 or RF 2 | {{<icon/no>}} | Always use RF of 3 or greater to ensure safe replication. |

## Network

The following is a list of default ports along with the network access required for using YugabyteDB:

- Each of the nodes in the YugabyteDB cluster must be able to communicate with each other using TCP/IP on the following ports:

  - 7100 for YB-Master RPC communication.
  - 9100 for YB-TServer RPC communication.

- To view the cluster dashboard, you need to be able to navigate to the following ports on the nodes:

  - 7000 for viewing the YB-Master Admin UI.

- To access the database from applications or clients, the following ports need to be accessible from the applications or CLI:

  - 5433 for YSQL
  - 9042 for YCQL

This deployment uses YugabyteDB [default ports](../../reference/configuration/default-ports/).

YugabyteDB Anywhere has its own port requirements. Refer to [Networking](../../yugabyte-platform/prepare/networking/).

## Clock synchronization

For YugabyteDB to maintain strict data consistency, clock drift and clock skew across all nodes _must_ be tightly controlled and kept within defined bounds. Any deviation can impact node availability, as YugabyteDB prioritizes consistency over availability and will shut down servers if necessary to maintain integrity. Clock synchronization software, such as [NTP](http://www.ntp.org/) or [chrony](https://chrony.tuxfamily.org/), allows you to reduce clock skew and drift by continuously synchronizing system clocks across nodes in a distributed system like YugabyteDB. The following are some recommendations on how to configure clock synchronization.

### Clock skew

Set a safe value for the maximum clock skew flag (`--max_clock_skew_usec`) for YB-TServers and YB-Masters when starting the YugabyteDB servers. The recommended value is two times the expected maximum clock skew between any two nodes in your deployment.

For example, if the maximum clock skew across nodes is expected to be no more than 250 milliseconds, then set the parameter to 500000 (`--max_clock_skew_usec=500000`).

### Clock drift

The maximum clock drift on any node should be bounded to no more than 500 PPM (or parts per million). This means that the clock on any node should drift by no more than 0.5 ms per second. Note that 0.5 ms per second is the standard assumption of clock drift in Linux.

In practice, the clock drift would have to be orders of magnitude higher in order to cause correctness issues.

## Security checklist

For a list of best practices, see [security checklist](../../secure/security-checklist/).

## Public clouds

YugabyteDB can run on a number of public clouds.

### Amazon Web Services (AWS)

- Use the M [instance family](https://aws.amazon.com/ec2/instance-types/).
- Recommended type is M6i. Use the higher CPU instance types especially for large YSQL workloads.
- Use gp3 EBS (SSD) disks that are at least 250GB in size, larger if more IOPS are needed.
  - Scale up the IOPS as you scale up the size of the disk.
  - In YugabyteDB testing, gp3 EBS SSDs provide the best performance for a given cost among the various EBS disk options.
- Avoid running on [T2 instance types](https://aws.amazon.com/ec2/instance-types/t2/). The T2 instance types are burstable instance types. Their baseline performance and ability to burst are governed by CPU credits, which makes it hard to get steady performance.
- Use VPC peering for multi-region deployments and connectivity to S3 object stores.

### Google Cloud

- Use the N2 high-CPU instance family. As a second choice, the N2 standard instance family can be used.
- Recommended instance types are `n2-highcpu-16` and `n2-highcpu-32`.
- [Local SSDs](https://cloud.google.com/compute/docs/disks/#localssds) are the preferred storage option, as they provide improved performance over attached disks, but the data is not replicated and can be lost if the node fails. This option is ideal for databases such as YugabyteDB that manage their own replication and can guarantee high availability (HA). For more details on these tradeoffs, refer to [Local vs remote SSDs](../../deploy/kubernetes/best-practices/#local-versus-remote-ssds).
  - Each local SSD is 375 GB in size, but you can attach up to eight local SSD devices for 3 TB of total local SSD storage space per instance.
- As a second choice, [remote persistent SSDs](https://cloud.google.com/compute/docs/disks/#pdspecs) perform well. Make sure the size of these SSDs are at least 250GB in size, larger if more IOPS are needed:
  - The number of IOPS scale automatically in proportion to the size of the disk.
- Avoid running on f1 or g1 machine families. These are [burstable, shared core machines](https://cloud.google.com/compute/docs/machine-types#sharedcore) that may not deliver steady performance.

### Azure

- Use v5 options with 16 vCPU in the Storage Optimized (preferred) or General Purpose VM types. For a busy YSQL instance, use 32 vCPU.
- For an application that cannot tolerate P99 spikes, local SSDs (Storage Optimized instances) are the preferred option. For more details on the tradeoffs, refer to [Local vs remote SSDs](../../deploy/kubernetes/best-practices/#local-versus-remote-ssds).
- If local SSDs are not available, use ultra disks to eliminate expected latency on Azure premium disks. Refer to the Azure [disk recommendations](https://azure.microsoft.com/en-us/blog/azure-ultra-disk-storage-microsoft-s-service-for-your-most-i-o-demanding-workloads/) and Azure documentation on [disk types](https://docs.microsoft.com/en-us/azure/virtual-machines/disks-types) for databases.
- Turn on Accelerated Networking, and use VNet peering for multiple VPCs and connectivity to object stores.
