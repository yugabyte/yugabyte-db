---
title: Active-Active Multi-Master design pattern for  global applications
headerTitle: Active-Active Multi-Master
linkTitle: Active-Active Multi-Master
description: Multi-Master dual cluster for global applications
headcontent: Multi-Master dual cluster for global applications
menu:
  preview:
    identifier: global-apps-active-active-multi-master
    parent: build-global-apps
    weight: 500
type: docs
---

For a highly available system, it is typical to opt for a [Global database](../global-database) that spans multiple regions. The raft-based **synchronous replication** guarantees that at least `1 + RF/2` (`RF` = replication factor) nodes are consistent and up-to-date with the latest data. This means a write can complete only after it has been successfully replicated to other nodes. This increases latency when the cluster spans multiple regions.

For apps that have to be run in multiple regions, you can adopt the **Active-Active Multi-Master** design pattern with which you can setup 2 clusters in different regions where both clusters actively take responsibility for the local reads and writes and populate one another **asynchronously**. Let us understand this in more detail.

## Initial setup

Let's say you have your `RF3` cluster and apps deployed in `us-west`.

![RF3 cluster in one region](/images/develop/global-apps/aa-single-master-1region.png)

This will ensure that the reads and writes are within the same region. Notice the low latency of reads and writes. As the whole cluster is in one region, in case of a region failure, you would lose all the data. Let's see how we can mitigate this.

## Multi-Master

You can set up another cluster in a different region say `us-east` using [xCluster](../../../explore/multi-region-deployments/asynchronous-replication-ysql/#configure-bidirectional-replication).

![Active-Active Multi Master](/images/develop/global-apps/aa-multi-master-setup.png)

The `us-east` cluster is independent of the primary cluster in `us-west` and the data will be populated by **asynchronous replication** between each other. This means that the read and write latencies of each cluster will not be affected by the other but at the same time, the data in each cluster will not be immediately consistent with each other. You can use this pattern to reduce latencies for local users by writing and reading from the closest cluster.

## Caveats

The replication happens at the DocDB layer bypassing the query layer, some standard functionality will not work.

- Avoid `UNIQUE` indexes and constraints, as there is no way to check uniqueness.
- Avoid `TRIGGERS`, as the triggers won't be fired as the query layer is bypassed.
- Avoid `SERIAL` columns as both the clusters would generate the same sequence (use UUID instead).
- Schema changes are not automatically transmitted but have to be applied manually (for now)

Another thing to note in xCluster is that transaction updates are NOT committed atomically from the source to the sink and hence the second cluster could be transactionally inconsistent.
