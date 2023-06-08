---
title: HA design patterns for global applications
headerTitle: HA design patterns for global applications
linkTitle: 2. High availability patterns
description: Build highly available global applications
headcontent: Design highly available global applications
image: /images/section_icons/quick_start/sample_apps.png
menu:
  preview:
    identifier: global-apps-design-patterns-ha
    weight: 2020
rightNav:
  hideH3: true
  hideH4: true
type: docs
---
<!--
Stretch cluster with sync replication
Unidirectional async replication (transaction and non-transactional)
Bidirectional async replication
Read replicas
-->

For most applications, a single-region multi-zone deployment would suffice. But global applications that are designed to serve users across multiple geographies and be highly available have to be deployed in multiple regions.

Deploying applications in multiple data centers and splitting data across them can be a complex undertaking. You can leverage some of our battle-tested design paradigms, which offer solutions to common problems faced in global applications. By adopting such design patterns, your application development can be significantly accelerated. These proven paradigms save time and resources that would otherwise be spent reinventing the wheel.

Let's look at some basic design patterns that you can adopt with YugabyteDB for global applications distributed across multiple regions.

## Stretch cluster

To be ready for region failures and be highly available, you can deploy your cluster across multiple regions. A **stretch cluster** is a cluster that stretches across multiple regions. Let’s consider a stretch cluster that is distributed across three regions: `us-east`, `us-central`, and `us-west`. Automatically the leaders are placed across the three regions and the corresponding followers are placed in other regions in a load-balanced manner as shown in the following illustration.

![RF3 Stretch cluster](/images/develop/global-apps/rf3-stretch.png)

The **stretch** cluster as shown in the above diagram is automatically resilient to a single region failure. When a region fails, followers in other regions are promoted to leaders within seconds and will continue to serve requests without any data loss. This is because the raft-based **synchronous replication** guarantees that at least `1 + RF/2` (`RF` = replication factor) nodes are consistent and up-to-date with the latest data. This enables the newly elected leader to serve the latest data immediately without any downtime for your users.

As the leaders are distributed across all the regions, processing even a simple query might end up going to all the regions resulting in higher latencies. But you can set up [preferred zones for leaders](../global-performance#reducing-latency-with-preferred-leaders) and [follower reads](../global-performance#reducing-read-latency-with-follower-reads) to diminish the latency issues.

## Active-Active Multi-Master

For use cases that do not require synchronous replication or cannot justify the operating costs associated with managing three or more data centers, YugabyteDB supports two-data-center (2DC) deployments that use asynchronous cross-cluster ([xCluster](../../../architecture/docdb-replication/async-replication)) replication built on top of [change data capture (CDC)](../../../architecture/docdb-replication/change-data-capture).

You can set up two separate clusters and connect them via xCluster. Writes will be **replicated asynchronously both ways** and conflict resolution will be done using the last-writer-wins scheme. xCluster is a very useful and simple paradigm to achieve high availability and reduced latency at a low cost (as compared to a 3DC setup). But it has its downsides.

![Bidirectional async replication with xCluster](/images/develop/global-apps/xcluster-twoway.png)

The replication happens at the DocDB layer bypassing the query layer, some standard functionality will not work.

- Avoid `UNIQUE` indexes and constraints, as there is no way to check uniqueness.
- Avoid `TRIGGERS`, as the triggers won't be fired as the query layer is bypassed.
- Avoid `SERIAL` columns as both the clusters would generate the same sequence (use UUID instead).
- Schema changes are not automatically transmitted but have to be applied manually (for now)

Another thing to note in xCluster is that transaction updates are NOT committed atomically from the source to the target and hence the target cluster could be transactionally inconsistent.

## Active-Active Single-Master

Just like how you can set up bi-directional replication with [xCluster](../../../architecture/docdb-replication/async-replication), you can set up an **asynchronous unidirectional replication** from the source to the target. This is useful if you want a separate standby cluster for disaster recovery or [blue/green](https://en.wikipedia.org/wiki/Blue-green_deployment) deploy testing.

![Unidirectional async replication with xCluster](/images/develop/global-apps/xcluster-oneway.png)

The same limitations mentioned in the [Bidirectional async replication](#bidirectional-async-replication-with-xcluster) section apply here too.

## Read Replica

Read Replicas are a read-only extension to the primary cluster. With read replicas, the primary data of the cluster is copied across one or more nodes in a different region. Read replicas do not add to write latencies because data is **asynchronously replicated** to replicas. To read data from a read replica, you need to enable follower reads for the cluster.

![Unidirectional async replication with read replicas](/images/develop/global-apps/read-replica.png)

Read replicas act as observers and do not participate in the primary cluster's [RAFT](../../../architecture/docdb-replication/replication) consensus. They neither affect the fault tolerance of the primary cluster nor contribute to failover. Due to this, they can have a different replication factor than the primary cluster.

The key thing to remember about read replicas is that even though they may not have the latest data, they are both timeline consistent and transactionally consistent.

{{<tip>}}
For more design patterns, see  [Design Patterns](../../../../explore/transactions/isolation-levels/)
{{</tip>}}

