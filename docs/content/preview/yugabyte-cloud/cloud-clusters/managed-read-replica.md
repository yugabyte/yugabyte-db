---
title: Read replicas in YugabyteDB Aeon
headerTitle: Read replicas
linkTitle: Read replicas
description: Add read replicas to YugabyteDB Aeon clusters.
headcontent: Reduce read latencies in remote regions
menu:
  preview_yugabyte-cloud:
    identifier: managed-readreplica
    parent: cloud-clusters
    weight: 150
type: docs
---

If your user base is geographically distributed, you can add [read replicas](../../cloud-basics/create-clusters-topology/#read-replicas) to improve read latency in regions that are far from your primary region.

{{< youtube id="aar4vW6Z1Zg" title="Add read replicas to a cluster in YugabyteDB Aeon" >}}

Read Replicas are a read-only extension to the primary cluster. With read replicas, the primary data of the cluster is copied across one or more nodes in a different region. Read replicas do not add to write latencies because writes aren't synchronously replicated to replicas - the data is replicated to read replicas asynchronously. To read data from a read replica, you need to enable follower reads for the cluster.

For more information on read replicas and follower reads in YugabyteDB, see the following:

- [Read replicas](../../../architecture/docdb-replication/read-replicas/)
- [Follower reads](../../../explore/going-beyond-sql/follower-reads-ysql/)

Each read replica cluster can have its own [replication factor](../../../architecture/docdb-replication/replication/#replication-factor). The replication factor determines how many copies of your primary data the read replica has; multiple copies ensure the availability of the replica in case of a node outage. Replicas do not participate in the primary cluster [Raft](../../../architecture/docdb-replication/replication/#raft-replication) consensus, and do not affect the fault tolerance of the primary cluster or contribute to failover.

You can delete, modify, and scale read replicas. Adding or removing nodes incurs a load on the replica. Perform scaling operations when the replica isn't experiencing heavy traffic. Scaling during times of heavy traffic can temporarily degrade performance and increase the length of time of the scaling operation.

The **Regions** section on the cluster **Settings > Infrastructure** tab summarizes the cluster configuration, including the number of nodes, vCPUs, memory, and disk per node, and VPC for each region of the primary cluster and its replicas.

## Prerequisites

Read replicas require the following:

- Primary cluster that is deployed in a VPC.
- Read replicas must be deployed in a VPC. Create a VPC for each region where you want to deploy a read replica. Refer to [VPC networking](../../cloud-basics/cloud-vpcs/).

## Limitations

- Partition-by-region clusters do not support read replicas.
- If another [locking cluster operation](../#locking-operations) is already running, you must wait for it to finish.
- Some scaling operations require a rolling restart or, in the case of read replicas with a replication factor of 1, downtime.

## Add or edit read replicas

To add or edit read-replicas:

1. On the **Clusters** page, select your cluster.

1. Under **Actions**, choose **Add Read Replicas** or **Edit Read Replicas**.

    ![Add Read Replicas](/images/yb-cloud/managed-add-read-replicas.png)

1. For each replica, set the following options:

    **Region** - Choose the [region](../../cloud-basics/create-clusters-overview/#cloud-provider-regions) where you want to deploy the replica.

    **VPC** - Choose the VPC in which to deploy the nodes. You need to create VPCs before deploying a replica. Refer to [VPC networking](../../cloud-basics/cloud-vpcs/).

    **Replication Factor** - Enter the number of copies of your data. Replication factor refers to the number of copies of your data in your read replica. This is independent of the of the fault tolerance of the primary cluster, and does not contribute to failover.

    To ensure the availability of your read replica in case of node outages, set the replication factor greater than 1. Read replicas with a replication factor of 1 are subject to downtime if there is a node outage, and during [infrastructure operations](../#locking-operations) that require a restart.

    **Nodes** - Choose the number of nodes to deploy in the region. The number of nodes can't be less than the replication factor.

    **vCPU/Node**: Enter the number of virtual CPUs per node.

    **Disk size/Node**: Enter the disk size per node in GB.

    **Disk IOPS/Node** (AWS only): Enter the disk input output (I/O) operations per second (IOPS) per node. The node throughput will be scaled according to this IOPS value.

    Memory per node depends on the [instance type](../../cloud-basics/create-clusters-overview/#instance-types) available for the selected region.

    Monthly costs for the cluster are estimated automatically.

1. To add a read replica, click **Add Region**. To delete a read replica, click the Trash icon.

1. Click **Confirm and Save Changes** when you are done.

Depending on the number of nodes, adding replicas can take several minutes or more, during which time some cluster operations will not be available.
