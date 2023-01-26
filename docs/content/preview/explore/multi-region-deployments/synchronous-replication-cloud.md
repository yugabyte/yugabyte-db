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

In a synchronized multi-region cluster, a minimum of 3 nodes are spread across 3 regions with a replication factor (RF) of 3.

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

{{< note title="Setup for POCs" >}}

To use uniform and topology-aware load balancing features of smart drivers with YugabyteDB Managed clusters, the application must be hosted in a peered application VPC.

{{< /note >}}

## Create a replicate across regions cluster

Before you can create a multi-region cluster in YugabyteDB Managed, you need to [add your billing profile and payment method](../../../yugabyte-cloud/cloud-admin/cloud-billing-profile/), or you can [request a free trial](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431).

To create a multi-region cluster with synchronous replication, refer to [Replicate across regions](../../../yugabyte-cloud/cloud-basics/create-clusters/create-clusters-multisync/).

## Start a workload

Follow the [setup instructions](../../#set-up-yb-workload-simulator) to connect the YB Workload Simulator application, and run a read-write workload. To verify that the application is running correctly, navigate to the application UI at <http://localhost:8080/> to view the cluster network diagram and Latency and Throughput charts for the running workload.

You should now see some read and write load on the [tablet servers page](http://localhost:7000/tablet-servers), as per the following illustration:

![Multi-zone cluster load](/images/ce/online-reconfig-multi-zone-load.png)

The load is distributed evenly across the regions.

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

The following command sets the preferred zone to `aws.us-west-2.us-west-2a`:

```sh
$ ./bin/yb-admin \
    --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
    set_preferred_zones  \
    aws.us-west-2.us-west-2a
```

You should see the read and write load on the [tablet servers page](http://localhost:7000/tablet-servers) move to the preferred region, as per the following illustration:

![Multi-zone cluster load](/images/ce/online-reconfig-multi-zone-pref-load.png)

When complete, the load is handled exclusively by the preferred region.

Note that cross-region latencies are unavoidable in the write path, given the need to ensure region-level automatic failover and repair.

## Learn more

[Replication](../../../architecture/docdb-replication/replication/)
