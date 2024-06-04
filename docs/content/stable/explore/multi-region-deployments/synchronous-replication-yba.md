---
title: Synchronous multi region (3+ regions) in YugabyteDB Anywhere
headerTitle: Synchronous multi region (3+ regions)
linkTitle: Synchronous (3+ regions)
description: Global data distributed using synchronous replication across regions using YugabyteDB Anywhere.
headcontent: Distribute data synchronously across regions
menu:
  stable:
    identifier: explore-multi-region-deployments-sync-replication-2-yba
    parent: explore-multi-region-deployments
    weight: 710
type: docs
---

For protection in the event of the failure of an entire cloud region, you can deploy YugabyteDB across multiple regions with a synchronously replicated multi-region universe. In a synchronized multi-region universe, a minimum of three nodes are [replicated](../../../architecture/docdb-replication/replication/) across three regions with a replication factor (RF) of 3. In the event of a region failure, the universe continues to serve data requests from the remaining regions. YugabyteDB automatically performs a failover to the nodes in the other two regions, and the tablets being failed over are evenly distributed across the two remaining regions.

This deployment provides the following advantages:

- Resilience - putting the universe nodes in different regions provides a higher degree of failure independence.
- Consistency - all writes are synchronously replicated. Transactions are globally consistent.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../synchronous-replication-ysql/" class="nav-link">
      <img src="/icons/database.svg" alt="Server Icon">
      Local
    </a>
  </li>
  <li>
    <a href="../synchronous-replication-cloud/" class="nav-link">
      <img src="/icons/cloud.svg" alt="Cloud Icon">
      YugabyteDB Managed
    </a>
  </li>
  <li>
    <a href="../synchronous-replication-yba/" class="nav-link active">
      <img src="/icons/server.svg" alt="Server Icon">
      YugabyteDB Anywhere
    </a>
  </li>
</ul>

## Create a synchronized multi-region universe

Before you can [create a multi-region universe in YugabyteDB Anywhere](../../../yugabyte-platform/create-deployments/create-universe-multi-region/), you need to [install](../../../yugabyte-platform/install-yugabyte-platform/) YugabyteDB Anywhere and [configure](../../../yugabyte-platform/configure-yugabyte-platform/) it to run in AWS.

## Start a workload

Follow the [setup instructions](../../#set-up-yb-workload-simulator) to install the YB Workload Simulator application, connect it, and then [start a workload](../../#start-a-read-and-write-workload).

To verify that the application is running correctly, navigate to the application UI at <http://localhost:8080/> to view the universe network diagram, as well as latency and throughput charts for the running workload.

## View the universe activity

You can use YugabyteDB Anywhere to view per-node statistics for the universe, as follows:

1. Navigate to **Universes** and select your universe.

1. Select **Nodes** to view the total read and write operations for each node. <!-- , as shown in the following illustration: -->

   <!-- ![Read and write operations with 3 nodes](/images/ce/multisync-managed-nodes.png) -->

   Note that both the reads and the writes are approximately the same across all the nodes, indicating uniform load.

1. Select **Metrics** to view charts such as YSQL operations per second and latency. <!-- , as shown in the following illustration: -->

   <!-- ![Performance charts for 3 nodes](/images/ce/transactions_anywhere_chart.png) -->

## Tune latencies

Latency in a multi-region universe depends on the distance and network packet transfer times between the nodes of the universe as well as between the universe and the client. Because the [tablet leader](../../../architecture/key-concepts/#tablet-leader) [replicates write operations](../../../architecture/docdb-replication/raft/#replication-of-the-write-operation) across a majority of tablet peers before sending a response to the client, all writes involve cross-region communication between tablet peers.

For best performance and lower data transfer costs, you want to minimize transfers between providers and between provider regions. You do this by placing your universe as close to your applications as possible, as follows:

- Use the same cloud provider as your application.
- Place your universe in the same region as your application.
- Peer your universe with the Virtual Private Cloud (VPC) hosting your application.

### Follower reads

YugabyteDB offers tunable global reads that allow read requests to trade off some consistency for lower read latency. By default, read requests in a YugabyteDB universe are handled by the leader of the Raft group associated with the target tablet to ensure strong consistency. If you are willing to sacrifice some consistency in favor of lower latency, you can choose to read from a tablet follower that is closer to the client rather than from the leader. YugabyteDB also allows you to specify the maximum staleness of data when reading from tablet followers.

For more information, see [Follower reads examples](../../going-beyond-sql/follower-reads-ysql/).

### Preferred region

If application reads and writes are known to be originating primarily from a single region, you can designate a preferred region, which pins the tablet leaders to that single region. As a result, the preferred region handles all read and write requests from clients. Non-preferred regions are used only for hosting tablet follower replicas.

For multi-row or multi-table transactional operations, colocating the leaders in a single zone or region can help reduce the number of cross-region network hops involved in executing a transaction.

Set a particular zone in the region to which you are connected as preferred, as follows:

1. Navigate to your universes's **Overview** and click **Actions > Edit Universe**.

1. Under **Availability Zones**, find the zone and select its corresponding **Preferred**.

1. Click **Save**.

To verify that the load is moving to the preferred zone in the region, select **Nodes**. <!-- , as per the following illustration: -->

<!-- ![Read and write operations with preferred region](/images/ce/multisync-managed-nodes-preferred.png) -->

When complete, the load is handled exclusively by the preferred region. <!-- , as per the following illustration: -->

<!-- ![Performance charts with preferred region](/images/ce/multisync-managed-charts-preferred.png) -->

With the tablet leaders now all located in the region to which the application is connected, latencies decrease and throughput increases.

Note that cross-region latencies are unavoidable in the write path, given the need to ensure region-level automatic failover and repair.
