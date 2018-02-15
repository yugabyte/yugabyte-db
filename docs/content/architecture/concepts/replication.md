---
title: Data Replication with Raft Consensus
weight: 950
---

YugaByte DB replicates data in order to survive failures while continuing to maintain consistency of
data and not requiring operator intervention. **Replication Factor** (or RF) is the number of copies
of data in a YugaByte universe. **Fault Tolerance** (or FT) of a YugaByte universe is the maximum
number of node failures it can survive while continuing to preserve correctness of data. FT and RF
are highly correlated. To achieve a FT of k nodes, the universe has to be configured with a RF of
(2k + 1).

## Strong write consistency

Replication of data in YugaByte DB is achieved at the level of tablets, using **tablet-peers**. Each
tablet comprises of a set of tablet-peers - each of which stores one copy of the data belonging to
the tablet. There are as many tablet-peers for a tablet as the replication factor, and they form a
Raft group. The tablet-peers are hosted on different nodes to allow data redundancy on node
failures. Note that the replication of data between the tablet-peers is **strongly consistent**.

The figure below illustrates three tablet-peers that belong to a tablet (tablet 1). The tablet-peers
are hosted on different YB-TServers and form a Raft group for leader election, failure detection and
replication of the write-ahead logs.

![raft_replication](/images/architecture/raft_replication.png)

The first thing that happens when a tablet starts up is to elect one of the tablet-peers as the
**leader** using the [Raft](https://raft.github.io/) protocol. This tablet leader now becomes
responsible for processing user-facing write requests. It  translates the user-issued writes into
DocDB updates, which it replicates among the tablet-peers using Raft to achieve strong consistency.
The set of DocDB updates depends on the user-issued write, and involves locking a set of keys to
establish a strict update order, and optionally reading the older value to modify and update in case
of a read-modify-write operation. The Raft log is used to ensure that the database state-machine of
a tablet is replicated amongst the tablet-peers with strict ordering and correctness guarantees even
in the face of failures or membership changes. This is essential to achieving strong consistency.

Once the Raft log is replicated to a majority of tablet-peers and successfully persisted on the
majority, the write is applied into DocDB and is subsequently available for reads. Details of DocDB,
which is a log structured merge-tree (LSM) database, is covered in a subsequent section. Once the
write is persisted on disk by DocDB, the write entries can be purged from the Raft log. This is
performed as a controlled background operation without any impact to the foreground operations.

## Tunable read consistency

Only the tablet leader can process user-facing write and read requests. Note that while this is the
case for strongly consistent reads, YugaByte DB offers reading from **followers** with relaxed
guarantees which is desired in some deployment models]. All other tablet-peers are called followers
and merely replicate data, and are available as hot standbys that can take over quickly in case the
leader fails.

## Read-only replicas

In addition to the core distributed consensus based replication, YugaByte DB extends Raft to add
read-only replicas (aks observer nodes) that do not participate in writes but get a timeline consistent copy of the data inan asynchronous manner. Nodes in remote  datacenters can thus be added in "read-only" mode. This is
primarily for cases where latency of doing a distributed consensus based write is not tolerable for
some workloads. This read-only node (or timeline-consistent node) is still strictly better than
eventual consistency, because with the latter the application's view of the data can move back and
forth in time and is hard to program to.
