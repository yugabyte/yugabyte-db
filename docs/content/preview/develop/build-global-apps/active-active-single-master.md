---
title: Active-Active Single-Master design pattern for  global applications
headerTitle: Active-active single-master
linkTitle: Active-active single-master
description: An active and a stand by cluster for global applications
headcontent: An active and a stand by cluster for global applications
menu:
  preview:
    identifier: global-apps-active-active-single-master
    parent: build-global-apps
    weight: 400
type: docs
---

For apps that run in a single region but need a safety net, you can adopt the **Active-Active Single-Master** design pattern with which you can setup 2 clusters in different regions where one cluster actively takes responsibility for all reads and writes and at the same time populates another cluster **asynchronously**. The second cluster can be promoted to primary in the case of a disaster. This setup would be very useful when you have just 2 regions and want to deploy the database in just one region for low latency but have another copy of the database in the other region for failover. Let us understand this in more detail.

{{<tip>}}
Only one application instance is active at any time and does consistent reads. A replica cluster serves reads and can be promoted to primary in case of failover.
{{</tip>}}

## Overview

{{<cluster-setup-tabs list="local,anywhere">}}

Let's say you have your `RF3` cluster and apps deployed in `us-west`.

![RF3 cluster in one region](/images/develop/global-apps/aa-single-master-1region.png)

This will ensure that the reads and writes are in the same region. Notice the low latency of reads and writes. As the whole cluster is in one region, in case of a region failure, you would lose all the data. Let's see how we can mitigate this.

## Secondary replica cluster

You can set up a secondary cluster in a different region say `us-east` using [xCluster](../../../architecture/docdb-replication/async-replication).

![Active-Active Single Master](/images/develop/global-apps/aa-single-master-setup.png)

The `us-east` cluster(_sink_) is independent of the primary cluster in `us-west` and the data will be populated by **asynchronous replication** from the primary cluster(_source_). This means that the read and write latencies of the primary cluster will not be affected but at the same time, the data in the second cluster will not be immediately consistent with the primary cluster. The _sink_ cluster acts as a **Replica cluster** and can take over as primary in case of a disaster. This can also be used for [blue/green](https://en.wikipedia.org/wiki/Blue-green_deployment) deploy testing.

## Local reads

As the second cluster has the same schema and the data(with a little lag), it can serve stale reads for local applications.

![Active-Active Single Master](/images/develop/global-apps/aa-single-master-reads.png)

But the writes still have to go to the primary cluster in `us-west`.

## Transactional consistency

You can preserve and guarantee transactional atomicity and global ordering when propagating change data from one universe to another by adding the `transactional` flag when setting up the [xCluster replication](../../../deploy/multi-dc/async-replication-transactional/#set-up-unidirectional-transactional-replication). This is the default behavior.

You can relax the transactional atomicity guarantee for lower replication lag.

## Failover

When the primary cluster in `us-west` fails, the secondary cluster in `us-east` can be promoted to become the primary and can start serving both reads and writes.

![Active-Active Single Master - Failover](/images/develop/global-apps/aa-single-master-failover.png)

## Caveats

The replication happens at the DocDB layer bypassing the query layer, some standard functionality will not work.

- Avoid `UNIQUE` indexes and constraints, as there is no way to check uniqueness.
- Avoid `TRIGGERS`, as the triggers won't be fired as the query layer is bypassed.
- Avoid `SERIAL` columns, as both the clusters would generate the same sequence (use UUID instead).
- Schema changes are not automatically transmitted but have to be applied manually (for now).

Another thing to note in xCluster is that transaction updates are NOT committed atomically from the source to the sink and hence the second cluster could be transactionally inconsistent.

## Learn more

- [xCluster architecture](../../../architecture/docdb-replication/async-replication)
- [xCluster deployment](../../../explore/multi-region-deployments/asynchronous-replication-ysql/)
- [Raft consensus protocol](../../../architecture/docdb-replication/replication)