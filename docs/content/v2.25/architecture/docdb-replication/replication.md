---
title: Replication in DocDB
headerTitle: Synchronous replication
linkTitle: Synchronous
description: Learn how YugabyteDB uses the Raft consensus in DocDB to replicate data across multiple independent fault domains like nodes, zones, regions, and clouds.
headContent: Synchronous replication using the Raft consensus protocol
aliases:
  - /preview/architecture/concepts/docdb/replication/
menu:
  preview:
    identifier: architecture-docdb-replication-default
    parent: architecture-docdb-replication
    weight: 200
type: docs
---

Using the [Raft distributed consensus protocol](../raft), DocDB automatically replicates data synchronously across the primary cluster in order to survive failures while maintaining data consistency and avoiding operator intervention.

## Replication factor

YugabyteDB replicates data across [fault domains](../../key-concepts#fault-domain) (which, depending on the deployment, could be nodes, availability zones, or regions) in order to tolerate faults. The replication factor (RF) is the number of copies of data in a YugabyteDB cluster.

## Fault tolerance

The fault tolerance (FT) of a YugabyteDB cluster is the maximum number of fault domain failures it can survive while continuing to preserve correctness of data. Fault tolerance and replication factor are correlated as follows:

* To achieve a FT of `f` fault domains, the primary cluster has to be configured with a RF of at least `2f + 1`.

The following diagram shows a cluster with FT 1. Data is replicated across 3 nodes, and the cluster can survive the failure of one fault domain. To make the cluster able to survive the failure of a zone or region, you would place the nodes in different zones or regions.

![Raft group](/images/architecture/replication/raft-group.png)

To survive the outage of 2 fault domains, a cluster needs at least 2 * 2 + 1 fault domains; that is, an RF of 5. With RF >= 5, if 2 fault domains are offline, the remaining 3 fault domains can continue to serve reads and writes without interruption.

| Replication factor | Fault tolerance | Can survive failure of |
| :--- | :--- | :--- |
| 1 or 2 | 0 | 0 fault domains |
| 3 or 4 | 1 | 1 fault domain |
| 5 or 6 | 2 | 2 fault domains |
| 7 or 8 | 3 | 3 fault domains |

## Tablet peers

Replication of data in DocDB is achieved at the level of tablets, using tablet peers, with each table sharded into a set of tablets, as demonstrated in the following diagram:

![Tablets in a table](/images/architecture/replication/tablets_in_a_docsb_table.png)

Each tablet comprises of a set of tablet peers, each of which stores one copy of the data belonging to the tablet. There are as many tablet peers for a tablet as the replication factor, and they form a Raft group. The tablet peers are hosted on different nodes to allow data redundancy to protect against node failures. The replication of data between the tablet peers is strongly consistent.

The following diagram shows three tablet peers that belong to a tablet called `tablet 1`. The tablet peers are hosted on different YB-TServers and form a Raft group for leader election, failure detection, and replication of the write-ahead logs.

![Raft Replication](/images/architecture/raft_replication.png)

## Raft replication

As soon as a tablet initiates, it elects one of the tablet peers as the tablet leader using the [Raft](../raft) protocol. The tablet leader becomes responsible for processing user-facing write requests by translating the user-issued writes into the document storage layer of DocDB. In addition, the tablet leader replicates among the tablet peers using Raft to achieve strong consistency. Setting aside the tablet leader, the remaining tablet peers of the Raft group are called tablet followers.

The set of DocDB updates depends on the user-issued write, and involves locking a set of keys to establish a strict update order, and optionally reading the older value to modify and update in case of a read-modify-write operation. The Raft log is used to ensure that the database state-machine of a tablet is replicated amongst the tablet peers with strict ordering and correctness guarantees even in the face of failures or membership changes. This is essential to achieving strong consistency.

After the Raft log is replicated to a majority of tablet-peers and successfully persisted on the majority, the write is applied into the DocDB document storage layer and is subsequently available for reads. After the write is persisted on disk by the document storage layer, the write entries can be purged from the Raft log. This is performed as a controlled background operation without any impact to the foreground operations.

## Multi-zone deployment

The replicas of data can be placed across multiple [fault domains](../../key-concepts#fault-domain). The following examples of a multi-zone deployment with three zones and the replication factor assumed to be 3 demonstrate how replication across fault domains is performed in a cluster.

In the case of a multi-zone deployment, the data in each of the tablets in a node is replicated across multiple zones using the Raft consensus algorithm. All the read and write queries for the rows that belong to a given tablet are handled by that tablet's leader, as per the following diagram:

![Replication across zones](/images/architecture/replication/raft-replication-across-zones.png)

As a part of the Raft replication, each tablet peer first elects a tablet leader responsible for serving reads and writes. The distribution of tablet leaders across different zones is determined by a user-specified data placement policy, which, in the preceding scenario, ensures that in the steady state, each of the zones has an equal number of tablet leaders. The following diagram shows how the tablet leaders are dispersed:

![Tablet leader placement](/images/architecture/replication/optimal-tablet-leader-placement.png)

{{<note>}}
Tablet leaders are balanced across **zones** and the **nodes** in a zone.
{{</note>}}

## Tolerating a zone outage

As soon as a zone outage occurs, YugabyteDB assumes that all nodes in that zone become unavailable simultaneously. This results in one-third of the tablets (which have their tablet leaders in the zone that just failed) not being able to serve any requests. The other two-thirds of the tablets are not affected. For the affected one-third, YugabyteDB automatically performs a failover to instances in the other two zones. Once again, the tablets being failed over are distributed across the two remaining zones evenly, as per the following diagram:

![Automatic failover](/images/architecture/replication/automatic-failover-zone-outage.png)

{{<note>}}
Failure of **followers** has no impact on reads and writes. Only the tablet **leaders** serve reads and writes.
{{</note>}}

## RPO and RTO on zone outage

The recovery point objective (RPO) for each of these tablets is 0, meaning no data is lost in the failover to another zone. The recovery time objective (RTO) is 3 seconds, which is the time window for completing the failover and becoming operational out of the new zones, as per the following diagram:

![RPO vs RTO](/images/architecture/replication/rpo-vs-rto-zone-outage.png)

## Follower reads

Only the tablet leader can process user-facing write and read requests. Note that while this is the case for strongly consistent reads, YugabyteDB offers reading from followers with relaxed guarantees, which is desired in [some deployment models](../../../develop/build-global-apps/follower-reads/). All other tablet peers are called followers and merely replicate data. They are available as hot standbys that can take over quickly in case the leader fails.
