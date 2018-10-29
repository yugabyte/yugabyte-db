---
title: Apache Cassandra
linkTitle: Apache Cassandra
description: Apache Cassandra
menu:
  v1.0:
    parent: comparisons
    weight: 1080
---

Following are the key areas of differences between YugaByte DB and [Apache Cassandra](http://cassandra.apache.org/). A recent [blog post](https://blog.yugabyte.com/building-a-strongly-consistent-cassandra-with-better-performance-aa96b1ab51d6) also highlights these differences in more detail.

## 1. Data Consistency

YugaByte DB is a strongly consistent system that avoids several pitfalls of an eventually consistent database.

1. Dirty reads: Systems with an eventually consistent core offer weaker semantics than even async
replication. To work around this, many Apache Cassandra deployments use quorum read/write modes, but
even those do NOT offer [clean rollback semantics on write failures](https://stackoverflow.com/questions/12156517/whats-the-difference-between-paxos-and-wr-n-in-cassandra) and hence can still lead to dirty reads.

2. Deletes resurfacing: Another problem due to an eventually consistent core is [deleted values resurfacing](https://stackoverflow.com/questions/35392430/cassandra-delete-not-working). 

YugaByte avoids these pitfalls by using a theoretically sound replication model based on RAFT, with
strong-consistency on writes and tunable consistency options for reads.

## 2. High read penalty of eventual consistency

In eventually consistent systems, anti-entropy, read-repairs, etc. hurt performance and increase cost. But even in steady state, read operations read from a quorum to serve closer to correct responses and fix inconsistencies. For a replication factor of the 3, such a system’s read throughput is essentially ⅓.

With YugaByte DB, strongly consistent reads (from leaders), as well as timeline consistent/async reads
from followers perform the read operation on only 1 node (and not 3).

## 3. Read-modify-write Operations

In Apache Cassandra, simple read-modify-write operations such as “increment”, conditional updates, “INSERT …  IF NOT EXISTS” or “UPDATE ... IF EXISTS” use a scheme known as light-weight transactions [which incurs a 4-round-trip cost](https://teddyma.gitbooks.io/learncassandra/content/concurrent/concurrency_control.html) between replicas. With YugaByte DB, these operations only involve 1-round trip between the quorum members.

## 4. Secondary Indexes

Local secondary indexes in Apache Cassandra ([see blog](https://pantheon.io/blog/cassandra-scale-problem-secondary-indexes)) require a fan-out read to all nodes, i.e. index performance keeps dropping as cluster size increases. With YugaByte’s distributed transactions, secondary indexes will both be strongly-consistent and be point-reads rather than needing a read from all nodes/shards in the cluster.

## 5. Operational Stability / Add Node Challenges

1. Apache Cassandra’s eventually consistent core implies that a new node trying to join a cluster cannot simply copy data files from current “leader”. A logical read (quorum read) across multiple surviving peers is needed with Apache Cassandra to build the correct copy of the data to bootstrap a new node with. These reads need to uncompress/recompress the data back too. With YugaByte, a new node can be bootstrapped by simply copying already compressed data files from the leader of the corresponding shard.

2. Apache Cassandra is implemented in Java. GC pauses plus additional problems, especially when running with large heap sizes (RAM). YugaByte is implemented in C++.

3. Operations like anti-entropy and read-pair hurt steady-state stability of cluster as they consume additional system resources. With YugaByte, which does replication using distributed consensus, these operations are not needed since the replicas stay in sync using RAFT or catch up the deltas cleanly from the transaction log of the current leader.

4. Apache Cassandra needs constant tuning of compactions (because of Java implementation, non-scalable, non-partitioned bloom filters and index-metadata, lack of scan-resistant caches, and so on.).

## 6. Operational Flexibility 

At times, you may need to move your database infrastructure to new hardware or you may want to add a sync/async replica in another region or in public cloud. With YugaByte DB, these operations are simple 1-click intent based operations that are handled seamlessly by the system in a completely online manner. YugaByte’s core data fabric and its consensus based replication model enables the "data tier” to be very agile/recomposable much like containers/VMs have done for the application or stateless tier.

## 7. Need to Scale Beyond Single or 2-DC Deployments

The current RDBMS solutions (using async replication) and NoSQL solutions work up to 2-DC deployments; but in a very restrictive manner. With Apache Cassandra’s 2-DC deployments, one has to chose between write unavailability on a partition between the DCs, or cope with async replication model where on a DC failure some inflight data is lost. 

YugaByte DB’s distributed consensus based replication design, in 3-DC deployments for example, enables enterprise to “have their cake and eat it too”. It gives use cases the choice to be highly available for reads and writes even on a complete DC outage, without having to take a down time or resort to an older/asynchronous copy of the data (which may not have all the changes to the system).
