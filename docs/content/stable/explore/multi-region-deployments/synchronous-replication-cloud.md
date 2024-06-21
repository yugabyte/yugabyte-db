---
title: Synchronous multi region (3+ regions) in YugabyteDB Aeon
headerTitle: Synchronous multi region (3+ regions)
linkTitle: Synchronous (3+ regions)
description: Global data distributed using synchronous replication across regions using YugabyteDB Aeon.
headcontent: Distribute data synchronously across regions
menu:
  stable:
    identifier: explore-multi-region-deployments-sync-replication-2-cloud
    parent: explore-multi-region-deployments
    weight: 710
type: docs
---

For protection in the event of the failure of an entire cloud region, you can deploy YugabyteDB across multiple regions with a synchronously replicated multi-region cluster. In a synchronized multi-region cluster, a minimum of 3 nodes are [replicated](../../../architecture/docdb-replication/replication/) across 3 regions with a replication factor (RF) of 3. In the event of a region failure, the database cluster continues to serve data requests from the remaining regions. YugabyteDB automatically performs a failover to the nodes in the other two regions, and the tablets being failed over are evenly distributed across the two remaining regions.

This deployment provides the following advantages:

- Resilience. Putting cluster nodes in different regions provides a higher degree of failure independence.
- Consistency. All writes are synchronously replicated. Transactions are globally consistent.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../synchronous-replication-ysql/" class="nav-link">
      <img src="/icons/database.svg" alt="Server Icon">
      Local
    </a>
  </li>
  <li>
    <a href="../synchronous-replication-cloud/" class="nav-link active">
      <img src="/icons/cloud.svg" alt="Cloud Icon">
      YugabyteDB Aeon
    </a>
  </li>
  <li>
    <a href="../synchronous-replication-yba/" class="nav-link">
      <img src="/icons/server.svg" alt="Server Icon">
      YugabyteDB Anywhere
    </a>
  </li>
</ul>

## Create a Replicate across regions cluster

Before you can create a multi-region cluster in YugabyteDB Aeon, you need to [add your billing profile and payment method](../../../yugabyte-cloud/cloud-admin/cloud-billing-profile/), or you can [request a free trial](../../../yugabyte-cloud/managed-freetrial/).

To create a multi-region cluster with synchronous replication, refer to [Replicate across regions](../../../yugabyte-cloud/cloud-basics/create-clusters/create-clusters-multisync/). For best results, set up your environment as follows:

- Each region in a multi-region cluster must be [deployed in a VPC](../../../yugabyte-cloud/cloud-basics/cloud-vpcs/cloud-add-vpc/). In AWS, this means creating a VPC for each region. In GCP, make sure your VPC includes the regions where you want to deploy the cluster. You need to create the VPCs before you deploy the cluster.
- Set up a [peering connection](../../../yugabyte-cloud/cloud-basics/cloud-vpcs/cloud-add-peering/) to an application VPC where you can host the YB Workload Simulator application. If your cluster is deployed in AWS (that is, has a separate VPC for each region), peer the application VPC with each cluster VPC.
- Copy the YB Workload Simulator application to the peered VPC and run it from there.

YB Workload Simulator uses the YugabyteDB JDBC Smart Driver. You can run the application from your computer by [enabling Public Access](../../../yugabyte-cloud/cloud-secure-clusters/add-connections/#enabling-public-access) on the cluster, but to use the load balancing features of the driver, an application must be deployed in a VPC that has been peered with the cluster VPC. For more information, refer to [Using smart drivers with YugabyteDB Aeon](../../../drivers-orms/smart-drivers/#using-smart-drivers-with-yugabytedb-managed).

## Start a workload

Follow the [setup instructions](../../#set-up-yb-workload-simulator) to install the YB Workload Simulator application.

### Configure the smart driver

The YugabyteDB JDBC Smart Driver performs uniform load balancing by default, meaning it uniformly distributes application connections across all the nodes in the cluster. However, in a multi-region cluster, it's more efficient to target regions closest to your application.

If you are running the workload simulator from a peered VPC, you can configure the smart driver with [topology load balancing](../../../drivers-orms/smart-drivers/#topology-aware-connection-load-balancing) to limit connections to the closest region.

To turn on topology load balancing, start the application as usual, adding the following flag:

```sh
-Dspring.datasource.hikari.data-source-properties.topologyKeys=<cloud.region.zone>
```

Where `cloud.region.zone` is the location of the cluster region where your application is hosted.

After you are connected, [start a workload](../../#start-a-read-and-write-workload).

## View cluster activity

To verify that the application is running correctly, navigate to the application UI at <http://localhost:8080/> to view the cluster network diagram and Latency and Throughput charts for the running workload.

To view a table of per-node statistics for the cluster, in YugabyteDB Aeon, do the following:

1. On the **Clusters** page, select the cluster.

1. Select **Nodes** to view the total read and write operations for each node as shown in the following illustration.

![Read and write operations with 3 nodes](/images/ce/multisync-managed-nodes.png)

Note that read/write operations are roughly the same across all the nodes, indicating uniform load across the nodes.

To view your cluster metrics such as YSQL Operations/Second and YSQL Average Latency, in YugabyteDB Aeon, select the cluster [Performance](../../../yugabyte-cloud/cloud-monitor/overview/#performance-metrics) tab. You should see similar charts as shown in the following illustration:

![Performance charts for 3 regions](/images/ce/multisync-managed-charts.png)

## Tuning latencies

Latency in a multi-region cluster depends on the distance and network packet transfer times between the nodes of the cluster and between the cluster and the client. Because the [tablet leader](../../../architecture/key-concepts/#tablet-leader) replicates write operations across a majority of tablet peers before sending a response to the client, all writes involve cross-region communication between tablet peers.

For best performance as well as lower data transfer costs, you want to minimize transfers between providers, and between provider regions. You do this by locating your cluster as close to your applications as possible:

- Use the same cloud provider as your application.
- Locate your cluster in the same region as your application.
- Peer your cluster with the VPC hosting your application.

### Follower reads

YugabyteDB offers tunable global reads that allow read requests to trade off some consistency for lower read latency. By default, read requests in a YugabyteDB cluster are handled by the leader of the Raft group associated with the target tablet to ensure strong consistency. In situations where you are willing to sacrifice some consistency in favor of lower latency, you can choose to read from a tablet follower that is closer to the client rather than from the leader. YugabyteDB also allows you to specify the maximum staleness of data when reading from tablet followers.

For more information on follower reads, refer to the [Follower reads](../../going-beyond-sql/follower-reads-ysql/) example.

### Preferred region

If application reads and writes are known to be originating primarily from a single region, you can designate a preferred region, which pins the tablet leaders to that single region. As a result, the preferred region handles all read and write requests from clients. Non-preferred regions are used only for hosting tablet follower replicas.

For multi-row or multi-table transactional operations, colocating the leaders in a single zone or region can help reduce the number of cross-region network hops involved in executing a transaction.

Set the region you are connected to as preferred as follows:

1. On the cluster **Settings** tab or under **Actions**, choose **Edit Infrastructure** to display the **Edit Infrastructure** dialog.

1. For the region that your application is connected to, choose **Set as preferred region for reads and writes**.

1. Click **Confirm and Save Changes** when you are done.

The operation can take several minutes, during which time some cluster operations are not available.

Verify that the load is moving to the preferred region on the **Nodes** tab.

![Read and write operations with preferred region](/images/ce/multisync-managed-nodes-preferred.png)

When complete, the load is handled exclusively by the preferred region.

![Performance charts with preferred region](/images/ce/multisync-managed-charts-preferred.png)

With the tablet leaders now all located in the region to which the application is connected, latencies decrease and throughput increases.

Note that cross-region latencies are unavoidable in the write path, given the need to ensure region-level automatic failover and repair.
