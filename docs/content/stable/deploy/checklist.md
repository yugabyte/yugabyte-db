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

### Disks

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
