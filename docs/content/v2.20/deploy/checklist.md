---
title: Deployment checklist for YugabyteDB clusters
headerTitle: Deployment checklist
linkTitle: Deployment checklist
description: Checklist to review system requirements, configuration details, and so on, when deploying the YugabyteDB database to production or for performance testing.
menu:
  v2.20:
    identifier: checklist
    parent: deploy
    weight: 605
type: docs
---

A YugabyteDB cluster consists of two distributed services - the [YB-TServer](../../architecture/concepts/yb-tserver/) service and the [YB-Master](../../architecture/concepts/yb-master/) service. Because the YB-Master service serves the role of the cluster metadata manager, it should be brought up first, followed by the YB-TServer service. To bring up these distributed services, the respective servers (YB-Master or YB-TServer) need to be started across different nodes. There is a number of topics to consider and recommendations to follow when starting these services.

## Basics

- YugabyteDB supports both x86 and ARM (aarch64) CPU architectures.
- YugabyteDB is supported on a variety of [operating systems](../../reference/configuration/operating-systems/). For production workloads, the recommended operating systems are AlmaLinux 8 and RHEL 8.
- The appropriate system limits should be set using [`ulimit`](../manual-deployment/system-config/#ulimits) on each node running a YugabyteDB server.
- [NTP or chrony](../manual-deployment/system-config/#ntp) should be used to synchronize time among the machines.

## Replication

YugabyteDB internally replicates data in a consistent manner using the Raft consensus protocol to survive node failure without compromising data correctness. This distributed consensus replication is applied at a per-shard (also known as tablet) level similar to Google Spanner.

The replication factor (RF) corresponds to the number of copies of the data. You need at least as many nodes as the RF, which means one node for RF1, three nodes for RF3, and so on. With a RF of 3, your cluster can tolerate one node failure. With a RF of 5, it can tolerate two node failures. More generally, if RF is n, YugabyteDB can survive (n - 1) / 2 failures without compromising correctness or availability of data.

When deploying a cluster, keep in mind the following:

- The RF should be an odd number to ensure majority consensus can be established during failures.
- The default replication factor is 3.
- The number of YB-Master servers running in a cluster should match RF. Run each server on a separate machine to prevent losing availability on failures. You need to specify the RF using the `--replication_factor` flag when bringing up the YB-Master servers.
- The number of YB-TServer servers running in the cluster should not be less than the RF. Run each server on a separate machine to prevent losing availability on failures.

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

- 16+ cores
- 32GB+ RAM
- Add more CPU (compared to adding more RAM) to improve performance.

**Additional considerations**

For typical Online Transaction Processing (OLTP) workloads, YugabyteDB performance improves with more aggregate CPU in the cluster. You can achieve this by using larger nodes or adding more nodes to a cluster. Note that if you do not have enough CPUs, this will manifest itself as higher latencies and eventually dropped requests.

Memory depends on your application query pattern. Writes require memory but only up to a certain point (for example, 4GB, but if you have a write-heavy workload you may need a little more). Beyond that, more memory generally helps improve the read throughput and latencies by caching data in the internal cache. If you do not have enough memory to fit the read working set, then you will typically experience higher read latencies because data has to be read from disk. Having a faster disk could help in some of these cases.

YugabyteDB explicitly manages a block cache, and does not need the entire data set to fit in memory. It does not rely on the OS to keep data in its buffers. If you provide YugabyteDB sufficient memory, data accessed and present in block cache stays in memory.

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
YugabyteDB can also leverage multiple disks per node and has been tested beyond 10 TB of storage per node.

Write-heavy applications usually require more disk IOPS (especially if the size of each record is larger), therefore in this case the total IOPS that a disk can support matters. On the read side, if the data does not fit into the cache and data needs to be read from the disk in order to satisfy queries, the disk performance (latency and IOPS) will start to matter.

YugabyteDB uses per-tablet [size tiered compaction](../../architecture/concepts/yb-tserver/). Therefore the typical space amplification in YugabyteDB tends to be in the 10-20% range.

YugabyteDB stores data compressed by default. The effectiveness of compression depends on the data set. For example, if the data has already been compressed, then the additional compression at the storage layer of YugabyteDB will not be very effective.

It is recommended to plan for about 20% headroom on each node to allow space for miscellaneous overheads such as temporary additional space needed for compactions, metadata overheads, and so on.

### Network

The following is a list of default ports along with the network access required for using YugabyteDB:

- Each of the nodes in the YugabyteDB cluster must be able to communicate with each other using TCP/IP on the following ports:

  - 7100 for YB-Master RPC communication.
  - 9100 for YB-TServer RPC communication.

- To view the cluster dashboard, you need to be able to navigate to the following ports on the nodes:

  - 7000 for viewing the YB-Master Admin UI.

- To use the database from the app, the following ports need to be accessible from the app or CLI:

  - 5433 for YSQL
  - 9042 for YCQL
  - 6379 for YEDIS

This deployment uses YugabyteDB [default ports](../../reference/configuration/default-ports/).

Note that for YugabyteDB Anywhere, the SSH port is changed for added security.

## Clock synchronization

For YugabyteDB to preserve data consistency, the clock drift and clock skew across different nodes must be bounded. This can be achieved by running clock synchronization software, such as [NTP](http://www.ntp.org/) or [chrony](https://chrony.tuxfamily.org/). The following are some recommendations on how to configure clock synchronization.

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

- Use the C5 or I3 instance families.
- Recommended types are i3.4xlarge, i3.8xlarge, c5.4xlarge, c5.8xlarge, and c6g.4xlarge/8xlarge. Use the higher CPU instance types especially for large YSQL workloads.
- For the C5 instance family, use gp3 EBS (SSD) disks that are at least 250GB in size, larger if more IOPS are needed:
  - Scale up the IOPS as you scale up the size of the disk.
  - In YugabyteDB testing, gp3 EBS SSDs provide the best performance for a given cost among the various EBS disk options.
- Avoid running on [T2 instance types](https://aws.amazon.com/ec2/instance-types/t2/). The T2 instance types are burstable instance types. Their baseline performance and ability to burst are governed by CPU credits, which makes it hard to get steady performance.
- Use VPC peering for multi-region deployments and connectivity to S3 object stores.

### Google Cloud

- Use the N2 high-CPU instance family. As a second choice, the N2 standard instance family can be used.
- Recommended instance types are `n2-highcpu-16` and `n2-highcpu-32`.
- [Local SSDs](https://cloud.google.com/compute/docs/disks/#localssds) are the preferred storage option, as they provide improved performance over attached disks, but the data is not replicated and can be lost if the node fails. This option is ideal for databases such as YugabyteDB that manage their own replication and can guarantee high availability (HA). For more details on these tradeoffs, refer to [Local vs remote SSDs](../../deploy/kubernetes/best-practices/#local-vs-remote-ssds).
  - Each local SSD is 375 GB in size, but you can attach up to eight local SSD devices for 3 TB of total local SSD storage space per instance.
- As a second choice, [remote persistent SSDs](https://cloud.google.com/compute/docs/disks/#pdspecs) perform well. Make sure the size of these SSDs are at least 250GB in size, larger if more IOPS are needed:
  - The number of IOPS scale automatically in proportion to the size of the disk.
- Avoid running on f1 or g1 machine families. These are [burstable, shared core machines](https://cloud.google.com/compute/docs/machine-types#sharedcore) that may not deliver steady performance.

### Azure

- Use v5 options with 16 vCPU in the Storage Optimized (preferred) or General Purpose VM types. For a busy YSQL instance, use 32 vCPU.
- For an application that cannot tolerate P99 spikes, local SSDs (Storage Optimized instances) are the preferred option. For more details on the tradeoffs, refer to [Local vs remote SSDs](../../deploy/kubernetes/best-practices/#local-vs-remote-ssds).
- If local SSDs are not available, use ultra disks to eliminate expected latency on Azure premium disks. Refer to the Azure [disk recommendations](https://azure.microsoft.com/en-us/blog/azure-ultra-disk-storage-microsoft-s-service-for-your-most-i-o-demanding-workloads/) and Azure documentation on [disk types](https://docs.microsoft.com/en-us/azure/virtual-machines/disks-types) for databases.
- Turn on Accelerated Networking, and use VNet peering for multiple VPCs and connectivity to object stores.
