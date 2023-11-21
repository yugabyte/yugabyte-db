---
title: Replication in DocDB
headerTitle: Synchronous replication
linkTitle: Synchronous
description: Learn how YugabyteDB uses the Raft consensus in DocDB to replicate data across multiple independent fault domains like nodes, zones, regions, and clouds.
headContent: Synchronous replication using the Raft consensus protocol
menu:
  v2.18:
    identifier: architecture-docdb-replication-default
    parent: architecture-docdb-replication
    weight: 1144
type: docs
---

Using the Raft distributed consensus protocol, DocDB automatically replicates data synchronously across the primary cluster in order to survive failures while maintaining data consistency and avoiding operator intervention.

## Concepts

A number of concepts are central to replication.

### Fault domains

A fault domain comprises a group of nodes that are prone to correlated failures. The following are examples of fault domains:

* Zones or racks
* Regions or datacenters
* Cloud providers

Data is typically replicated across fault domains to be resilient to the outage of all nodes in one fault domain.

### Fault tolerance

The fault tolerance (FT) of a YugabyteDB universe is the maximum number of node failures it can survive while continuing to preserve correctness of data.

### Replication factor

YugabyteDB replicates data across nodes (or fault domains) in order to tolerate faults. The replication factor (RF) is the number of copies of data in a YugabyteDB universe. FT and RF are correlated. To achieve a FT of `k` nodes, the universe has to be configured with a RF of (2k + 1).

## Tablet peers

Replication of data in DocDB is achieved at the level of tablets, using tablet peers, with each table sharded into a set of tablets, as demonstrated in the following diagram:

![Tablets in a table](/images/architecture/replication/tablets_in_a_docsb_table.png)

Each tablet comprises of a set of tablet peers, each of which stores one copy of the data belonging to the tablet. There are as many tablet peers for a tablet as the replication factor, and they form a Raft group. The tablet peers are hosted on different nodes to allow data redundancy to protect against node failures. The replication of data between the tablet peers is strongly consistent.

The following diagram depicts three tablet peers that belong to a tablet called `tablet 1`. The tablet peers are hosted on different YB-TServers and form a Raft group for leader election, failure detection, and replication of the write-ahead logs.

![RAFT Replication](/images/architecture/raft_replication.png)

### Raft replication

As soon as a tablet initiates, it elects one of the tablet peers as the tablet leader using the [Raft](https://raft.github.io/) protocol. The tablet leader becomes responsible for processing user-facing write requests by translating the user-issued writes into the document storage layer of DocDB. In addition, the tablet leader replicates among the tablet peers using Raft to achieve strong consistency. Setting aside the tablet leader, the remaining tablet peers of the Raft group are called tablet followers.

The set of DocDB updates depends on the user-issued write, and involves locking a set of keys to establish a strict update order, and optionally reading the older value to modify and update in case of a read-modify-write operation. The Raft log is used to ensure that the database state-machine of a tablet is replicated amongst the tablet peers with strict ordering and correctness guarantees even in the face of failures or membership changes. This is essential to achieving strong consistency.

After the Raft log is replicated to a majority of tablet-peers and successfully persisted on the majority, the write is applied into the DocDB document storage layer and is subsequently available for reads. After the write is persisted on disk by the document storage layer, the write entries can be purged from the Raft log. This is performed as a controlled background operation without any impact to the foreground operations.

## Replication in the primary cluster

The replicas of data can be placed across multiple fault domains. The following examples of a multi-zone deployment with three zones and the replication factor assumed to be 3 demonstrate how replication across fault domains is performed in a cluster.

### Multi-zone deployment

In the case of a multi-zone deployment, the data in each of the tablets in a node is replicated across multiple zones using the Raft consensus algorithm. All the read and write queries for the rows that belong to a given tablet are handled by that tablet's leader, as per the following diagram:

![Replication across zones](/images/architecture/replication/raft-replication-across-zones.png)

As a part of the Raft replication, each tablet peer first elects a tablet leader responsible for serving reads and writes. The distribution of tablet leaders across different zones is determined by a user-specified data placement policy, which, in the preceding scenario, ensures that in the steady state, each of the zones has an equal number of tablet leaders. The following diagram shows how the tablet leaders are dispersed:

![Tablet leader placement](/images/architecture/replication/optimal-tablet-leader-placement.png)

### Tolerating a zone outage

As soon as a zone outage occurs, YugabyteDB assumes that all nodes in that zone become unavailable simultaneously. This results in one-third of the tablets (which have their tablet leaders in the zone that just failed) not being able to serve any requests. The other two-thirds of the tablets are not affected. The following illustration shows the tablet peers in the zone that failed:

![Tablet peers in a failed zone](/images/architecture/replication/tablet-leaders-vs-followers-zone-outage.png)

For the affected one-third, YugabyteDB automatically performs a failover to instances in the other two zones. Once again, the tablets being failed over are distributed across the two remaining zones evenly, as per the following diagram:

![Automatic failover](/images/architecture/replication/automatic-failover-zone-outage.png)

### RPO and RTO on zone outage

The recovery point objective (RPO) for each of these tablets is 0, meaning no data is lost in the failover to another zone. The recovery time objective (RTO) is 3 seconds, which is the time window for completing the failover and becoming operational out of the new zones, as per the following diagram:

![RPO vs RTO](/images/architecture/replication/rpo-vs-rto-zone-outage.png)

## Follower reads

Only the tablet leader can process user-facing write and read requests. Note that while this is the case for strongly consistent reads, YugabyteDB offers reading from followers with relaxed guarantees, which is desired in some deployment models. All other tablet peers are called followers and merely replicate data. They are available as hot standbys that can take over quickly in case the leader fails.
