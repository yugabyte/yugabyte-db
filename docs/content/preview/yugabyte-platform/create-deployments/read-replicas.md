---
title: Create a read replica cluster in YugabyteDB Anywhere
headerTitle: Create a read replica cluster
linkTitle: Read replica cluster
description: Use YugabyteDB Anywhere to create a read replica cluster.
headcontent: Reduce read latencies in remote regions
menu:
  preview_yugabyte-platform:
    identifier: create-read-replica-cluster
    parent: create-deployments
    weight: 40
type: docs
---

If your user base is geographically distributed, you can add a read replica cluster to the universe to improve read latency in regions that are far from your primary region.

Read replicas are a read-only extension to the primary cluster. With read replicas, the primary data of the cluster is copied to one or more nodes in a different region. Read replicas do not add to write latencies because writes aren't synchronously replicated to replicas - the data is replicated to the replicas asynchronously. To read data from a read replica, you need to enable follower reads.

For more information on read replicas and follower reads in YugabyteDB, see the following:

- [Read replicas](../../../architecture/docdb-replication/read-replicas/)
- [Follower reads](../../../explore/ysql-language-features/going-beyond-sql/follower-reads-ysql/)

You can customize the number of read replicas in the read replica cluster. Multiple replicas ensure the availability of the replica in case of a node outage. Replicas do not participate in the primary cluster [RAFT](../../../architecture/docdb-replication/replication/#raft-replication) consensus, and do not affect the fault tolerance of the primary cluster or contribute to failover. The number of read replicas can't exceed the number of nodes in the read replica cluster.

You can delete, modify, and scale read replica clusters. Adding or removing nodes incurs a load on the read replica cluster. Perform scaling operations when the cluster isn't experiencing heavy traffic. Scaling during times of heavy traffic can temporarily degrade performance and increase the length of time of the scaling operation.

## Limitations

- Currently, YugabyteDB Anywhere supports one only one read replica cluster per universe.
- You can add up to 15 read replicas to the read replica cluster.

## Create a universe with a read replica cluster

To create a universe with a read replica cluster, do the following:

1. Navigate to **Dashboard** and click **Create Universe**.
1. Use the **Primary Cluster** tab to enter the values to create a primary cluster. Refer to [Create a multi-zone universe](../create-universe-multi-zone/).

    {{< tip title="Tip" >}}

Add the `leader_failure_max_missed_heartbeat_periods` configuration flag for YB-Master and YB-TServer with a value of 10. As the data is globally replicated, remote procedure call (RPC) latencies are higher. You can use this flag to increase the failure detection interval in such a high-RPC latency deployment.

    {{< /tip >}}

1. Click **Configure Read Replica**.
1. Specify the following on the **Read Replica** tab to create a read replica cluster:

    - Specify the regions where you want to place replicas.
    - Specify the number of nodes and the number of read replicas. The number of nodes must be greater than or equal to the number of replicas.
    - Customize the availability zones if desired.
    - Configure the instance type to use for your read replica cluster.
    - You can choose to use the same flags as the primary cluster, or set custom flags for the read replica cluster. Read replicas only have YB-TServers. You can also set flags after universe creation. Refer to [Edit configuration flags](../../manage-deployments/edit-config-flags/).

1. To finish the process, click **Create**.

To see a list of nodes, navigate to **Nodes**. Notice that the nodes are grouped into primary cluster and read replicas, and read replica nodes have a `readonly1` identifier appended to their name.

## Add, remove, edit a read replica cluster

YugabyteDB Anywhere allows you to dynamically add, modify, and remove a read replica cluster from an existing universe.

To add a read replica to a universe, do the following:

1. Navigate to the universe and click **Actions > Add Read Replica**.
1. Use the **Configure read replica** page to enter the read replica details.
1. Click **Add Read Replica**.

To edit a read replica, do the following:

1. Navigate to the universe and click **Actions > Edit Read Replica**.
1. Use the **Configure read replica** page to enter the read replica details.
1. Click **Save**.

To delete a read replica cluster, do the following:

1. Navigate to the universe and click **Actions > Edit Read Replica**.
1. Click **Delete this configuration**.
