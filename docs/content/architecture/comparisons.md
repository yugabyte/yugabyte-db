---
date: 2016-03-09T20:08:11+01:00
title: Comparisons with Other Databases
weight: 74
---

## YugaByte vs. Apache Cassandra

### Data consistency
YugaByte is a strongly-consistent system and avoids several pitfalls of an eventually consistent database.

1. Dirty Reads: Systems with an eventually consistent core offer weaker semantics than even async
replication. To work around this, many Apache Cassandra deployments use quorum read/write modes, but
even those do NOT offer [clean semantics on write
failures](https://stackoverflow.com/questions/12156517/whats-the-difference-between-paxos-and-wr-n-in-cassandra).
2. Another problem due to an eventual consistency core is [deleted values
   resurfacing](https://stackoverflow.com/questions/35392430/cassandra-delete-not-working). 

YugaByte avoids these pitfalls by using a theoretically sound replication model based on RAFT, with
strong-consistency on writes and tunable consistency options for reads.

### **3x read penalty of eventual consistency**
In eventually consistent systems, anti-entropy, read-repairs, etc. hurt performance and increase cost. But even in steady state, read operations read from a quorum to serve closer to correct responses & fix inconsistencies. For a replication factor of the 3, such a system’s read throughput is essentially ⅓.

*With YugaByte, strongly consistent reads (from leaders), as well as timeline consistent/async reads
from followers perform the read operation on only 1 node (and not 3).*

### Read-modify-write operations
In Apache Cassandra, simple read-modify-write operations such as “increment”, conditional updates,
“INSERT …  IF NOT EXISTS” or “UPDATE ... IF EXISTS” use a scheme known as light-weight transactions
[which incurs a 4-round-trip
cost](https://teddyma.gitbooks.io/learncassandra/content/concurrent/concurrency_control.html) between replicas. With YugaByte these operations only involve
1-round trip between the quorum members.

### Secondary indexes
Local secondary indexes in Apache Cassandra ([see
blog](https://pantheon.io/blog/cassandra-scale-problem-secondary-indexes)) require a fan-out read to all nodes, i.e.
index performance keeps dropping as cluster size increases. With YugaByte’s distributed
transactions, secondary indexes will both be strongly-consistent and be point-reads rather than
needing a read from all nodes/shards in the cluster.

### Operational stability / Add node challenges

1. Apache Cassandra’s eventually consistent core implies that a new node trying to join a cluster
cannot simply copy data files from current “leader”. A logical read (quorum read) across multiple
surviving peers is needed with Apache Cassandra to build the correct copy of the data to bootstrap a
new node with. These reads need to uncompress/recompress the data back too. With YugaByte, a new
node can be bootstrapped by simply copying already compressed data files from the leader of the
corresponding shard.

2. Apache Cassandra is implemented in Java. GC pauses plus additional problems, especially when running
with large heap sizes (RAM). YugaByte is implemented in C++.
3. Operations like anti-entropy and read-pair hurt steady-state stability of cluster as they consume
additional system resources. With YugaByte, which does replication using distributed consensus,
these operations are not needed since the replicas stay in sync using RAFT or catch up the deltas
cleanly from the transaction log of the current leader.
4. Apache Cassandra needs constant tuning of compactions (because of Java implementation,
non-scaleable, non-partitioned bloom filters and index-metadata, lack of scan-resistant caches, and
so on.).

### Operational flexibility (Moving to new hardware? Want to add a sync/async replica in another region or in public cloud?)
With YugaByte, these operations are simple 1-click intent based operations that are handled
seamlessly by the system in a completely online manner. YugaByte’s core data fabric and its
consensus based replication model enables the "data tier” to be very agile/recomposable much like
containers/VMs have done for the application or stateless tier.

### Addresses need to go beyond single or 2-DC deployments
The current RDBMS solutions (using async replication) and NoSQL solutions work up to 2-DC
deployments; but in a very restrictive manner. With Apache Cassandra’s 2-DC deployments, one has to
chose between write unavailability on a partition between the DCs, or cope with async replication
model where on a DC failure some inflight data is lost. 

YugaByte’s distributed consensus based replication design, in 3-DC deployments for example, enables
enterprise to “have their cake and eat it too”. It gives use cases the choice to be highly available
for reads and writes even on a complete DC outage, without having to take a down time or resort to
an older/asynchronous copy of the data (which may not have all the changes to the system).

## YugaByte vs. Redis

* YugaByte’s Redis is a persistent database rather than an in-memory cache. [While Redis has a
check-pointing feature for persistence, it is a highly inefficient operation that does a process
fork. It is also not an incremental operation; the entire memory state is written to disk causing
serious overall performance impact.]

* YugaByte’s Redis is an auto sharded, clustered with built-in support for strongly consistent
replication and multi-DC deployment flexibility. Operations such as add node, remove node are
simple, throttled and intent-based and leverage YugaByte’s core engine (YBase) and associated
architectural benefits.

* Unlike the normal Redis, the entire data set does not need to fit in memory. In YugaByte, the hot
data lives in RAM, and colder data is automatically tiered to storage and on-demand paged in at
block granularity from storage much like traditional database.

* Applications that use Redis only as a cache and use a separate backing database as the main system
of record, and need to deal with dev pain points around keeping the cache and DB consistent and
operational pain points at two levels of infrastructure (sharding, load-balancing, geo-redundancy)
etc. can leverage YugaByte’s Redis as a unified cache + database offering.

* Scan resistant block cache design ensures long scan (e.g., of older data) do not impact reads for
recent data.

## YugaByte vs. Apache HBase

* **Simpler software stack**: HBase relies on HDFS (another complex piece of infrastructure for data
replication) and on Zookeeper for leader election, failure detection, and so on. Running HBase
smoothly requires a lot of on-going operational overheads.

* **HA / Fast-failover**: In HBase, on a region-server death, the unavailability window of shards/regions
on the server can be in the order of 60 seconds or more. This is because the HBase master first
needs to wait for the server’s ephemeral node in Zookeeper to expire, followed by time take to split
the transaction logs into per-shard recovery logs, and the time taken to replay the edits from the
transaction log by a new server before the shard is available to take IO. In contrast, in YugaByte,
the tablet-peers are hot standbys, and within a matter of few heartbeats (about few seconds) detect
failure of the leader, and initiate leader election.

* **C++ implementation**: Avoids GC tuning; can run better on large memory machines.
Richer data model: YugaByte offers a multi-model/multi-API through CQL & Redis (and SQL in future).
Rather than deal with just byte keys and values, YugaByte offers a rich set of scalar (int, text,
decimal, binary, timestamp, etc.) and composite types (such as collections, UDTs, etc.).

* **Multi-DC deployment** Flexlible deployment choices across multiple DCs or availability zones. HBase provides
strong-consistency only within a single datacenter and offers only async replication alternative for
cross-DC deployments.
