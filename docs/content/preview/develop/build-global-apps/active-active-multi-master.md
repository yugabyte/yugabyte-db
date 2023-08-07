---
title: Active-Active Multi-Master design pattern for global applications
headerTitle: Active-active multi-master
linkTitle: Active-active multi-master
description: Multi-Master dual cluster for global applications
headcontent: Multi-Master dual cluster for global applications
menu:
  preview:
    identifier: global-apps-active-active-multi-master
    parent: build-global-apps
    weight: 500
type: docs
---

For applications that have to be run in multiple regions, you can adopt the **Active-Active Multi-Master** design pattern, where you set up two clusters in two different regions, and both clusters actively take responsibility for the local reads and writes and populate one another **asynchronously**. Here, failover is manual and incurs some possible loss of data, as the data is asynchronously replicated between the two clusters, but both reads and writes have low latencies. Let us understand this in more detail.

{{<tip>}}
Application instances are active in multiple regions and could read stale data.
{{</tip>}}

## Overview

{{<cluster-setup-tabs list="local,anywhere">}}

Suppose you have your `RF3` cluster and apps deployed in `us-west`.

![RF3 cluster in one region](/images/develop/global-apps/aa-single-master-1region.png)

This will ensure that the reads and writes are in the same region. Notice the low latency of reads and writes. As the whole cluster is in one region, in case of a region failure, you would lose all the data. Let's see how we can mitigate this.

## Multi-master

You can set up another cluster in a different region say `us-east` using [xCluster](../../../explore/multi-region-deployments/asynchronous-replication-ysql/#configure-bidirectional-replication).

![Active-Active Multi-Master](/images/develop/global-apps/aa-multi-master-setup.png)

The `us-east` cluster is independent of the primary cluster in `us-west` and the data will be populated by **asynchronous replication** between each other. This means that the read and write latencies of each cluster will not be affected by the other but at the same time, the data in each cluster will not be immediately consistent with each other. You can use this pattern to reduce latencies for local users by writing and reading from the closest cluster.

## Transactional consistency

The **Active-Active Multi-Master** design pattern does not guarantee any transactional consistency during the replication between the clusters. Conflicts are resolved in the bi-directional replication by adopting the "last-writer wins" strategy.

## Failover

When one of the clusters fails, say `us-west`, the other cluster in `us-east` will be able to handle reads and writes for all applications until the failed region recovers.

![Failover](/images/develop/global-apps/aa-multi-master-failover.png)

## Caveats

The replication happens at the DocDB layer bypassing the query layer, some standard functionality will not work.

- Avoid `UNIQUE` indexes and constraints, as there is no way to check uniqueness.
- Avoid `TRIGGERS`, as the triggers won't be fired as the query layer is bypassed.
- Avoid `SERIAL` columns as both the clusters would generate the same sequence (use UUID instead).
- Schema changes are not automatically transmitted but have to be applied manually (for now).
- Transaction updates are NOT committed atomically across sources and hence the other cluster could be transactionally inconsistent.

## Learn more

- [xCluster architecture](../../../architecture/docdb-replication/async-replication)
- [xCluster deployment](../../../explore/multi-region-deployments/asynchronous-replication-ysql/)
- [Raft consensus protocol](../../../architecture/docdb-replication/replication)