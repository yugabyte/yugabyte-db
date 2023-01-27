---
title: Synchronous replication (3+ regions)
headerTitle: Synchronous replication (3+ regions)
linkTitle: Synchronous (3+ regions)
description: Global data distributed using synchronous replication across regions.
headcontent: Distribute data across regions
menu:
  preview:
    identifier: explore-multi-region-deployments-sync-replication-2-cloud
    parent: explore-multi-region-deployments
    weight: 710
type: docs
---

YugabyteDB can be deployed in a globally distributed manner to serve application queries from the region closest to end users with low latencies, as well as to survive any outages to ensure high availability.

In a synchronized multi-region cluster, a minimum of 3 nodes are [replicated](../../../architecture/docdb-replication/replication/) across 3 regions with a replication factor (RF) of 3.

This deployment provides the following advantages:

- Resilience. Putting cluster nodes in different regions provides a higher degree of failure independence. In the event of a region failure, the database cluster continues to serve data requests from the remaining regions. YugabyteDB automatically performs a failover to the nodes in the other two regions, and the tablets being failed over are evenly distributed across the two remaining regions.

- Consistency. All writes are synchronously replicated. Transactions are globally consistent.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../synchronous-replication-cloud/" class="nav-link active">
      <img src="/icons/cloud.svg" alt="Cloud Icon">
      Use a cloud cluster
    </a>
  </li>
  <li>
    <a href="../synchronous-replication-ysql/" class="nav-link">
      <img src="/icons/database.svg" alt="Server Icon">
      Use a local cluster
    </a>
  </li>
</ul>

## Create a replicate across regions cluster

Before you can create a multi-region cluster in YugabyteDB Managed, you need to [add your billing profile and payment method](../../../yugabyte-cloud/cloud-admin/cloud-billing-profile/), or you can [request a free trial](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431).

To create a multi-region cluster with synchronous replication, refer to [Replicate across regions](../../../yugabyte-cloud/cloud-basics/create-clusters/create-clusters-multisync/). For best results, set up your environment as follows:

- Multi-region clusters must be [deployed in a VPC](../../../yugabyte-cloud/cloud-basics/cloud-vpcs/cloud-add-vpc/). In AWS, you need a VPC for each region.
- Set up a [peering connection](../../../yugabyte-cloud/cloud-basics/cloud-vpcs/cloud-add-peering/) to a VPC where you can host the YB Workload Simulator application. If your cluster is deployed in AWS (that is, has a separate VPC for each region), peer the application VPC with each cluster VPC.
- Copy the YB Workload Simulator application to the peered VPC and run it from there.

YB Workload Simulator uses the YugabyteDB JDBC Smart Driver. You can run the application from your computer by [enabling Public Access](../../../yugabyte-cloud/cloud-secure-clusters/add-connections/#enabling-public-access) on the cluster, but to use the load balancing features of the driver, an application must be deployed in a VPC that has been peered with the cluster VPC. For more information, refer to [Using smart drivers with YugabyteDB Managed](../../../drivers-orms/smart-drivers/#using-smart-drivers-with-yugabytedb-managed).

## Start a workload

Follow the [setup instructions](../../#set-up-yb-workload-simulator) to connect the YB Workload Simulator application, and run a read-write workload.

To verify that the application is running correctly, navigate to the application UI at <http://localhost:8080/> to view the cluster network diagram and Latency and Throughput charts for the running workload.

To view a table of per-node statistics for the cluster, in YugabyteDB Managed, do the following:

1. On the **Clusters** page, select the cluster.

1. Select **Nodes** to view the total read and write IOPS per node and other statistics as shown in the following illustration.

![Read and write IOPS with 3 nodes](/images/ce/transactions_cloud_observe1.png)

Note that both the reads and the writes are roughly the same across all the nodes, indicating uniform load across the nodes.

To view your cluster metrics such as YSQL operations/second and Latency, in YugabyteDB Managed, select the cluster [Performance](/preview/yugabyte-cloud/cloud-monitor/overview/#performance-metrics) tab. You should see similar charts as shown in the following illustration:

![Performance charts for 3 nodes](/images/ce/transactions_cloud_chart.png)

## Tuning latencies

Latency in a multi-region cluster depends on the distance/network packet transfer times between the nodes of the cluster and between the cluster and the client. Because the tablet leader replicates write operations across a majority of tablet peers before sending a response to the client, all writes involve cross-region communication between tablet peers.

For best performance as well as lower data transfer costs, you want to minimize transfers between providers, and between provider regions. You do this by locating your cluster as close to your applications as possible:

- Use the same cloud provider as your application.
- Locate your cluster in the same region as your application.

### Follower reads

YugabyteDB offers tunable global reads that allow read requests to trade off some consistency for lower read latency. By default, read requests in a YugabyteDB cluster are handled by the leader of the Raft group associated with the target tablet to ensure strong consistency. In situations where you are willing to sacrifice some consistency in favor of lower latency, you can choose to read from a tablet follower that is closer to the client rather than from the leader. YugabyteDB also allows you to specify the maximum staleness of data when reading from tablet followers.

For more information on follower reads, refer to the [Follower reads](../../ysql-language-features/going-beyond-sql/follower-reads-ysql/) example.

### Preferred region

If application reads are known to be originating dominantly from a single region, you can designate a preferred region, which pins the shard leaders to that single region. As a result, the preferred region handles all read and write requests from clients. Non-preferred regions are used only for hosting shard follower replicas.

For multi-row or multi-table transactional operations, colocating the leaders in a single zone or region can help reduce the number of cross-region network hops involved in executing a transaction.

You can set one of the regions as preferred as follows:

1. On the cluster **Settings** tab or under **Actions**, choose **Edit Infrastructure** to display the **Edit Infrastructure** dialog.

1. For one of the regions, choose **Set as preferred region for reads and writes**.

1. Click **Confirm and Save Changes** when you are done.

The operation can take several minutes, during which time some cluster operations are not available.

Verify that the load is moving to the preferred region on the **Nodes** tab.

![Read and write IOPS with preferred region](/images/ce/add-node-cloud.png)

When complete, the load is handled exclusively by the preferred region.

Note that cross-region latencies are unavoidable in the write path, given the need to ensure region-level automatic failover and repair.
