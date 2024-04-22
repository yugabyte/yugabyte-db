---
title: Compare Apache Cassandra with YugabyteDB
headerTitle: Apache Cassandra
linkTitle: Apache Cassandra
description: Compare Apache Cassandra with YugabyteDB.
aliases:
  - /comparisons/cassandra/
menu:
  preview_faq:
    parent: comparisons
    identifier: comparisons-cassandra
    weight: 1120
type: docs
---

If you choose Apache Cassandra as your default operational database, you must realize that you cannot benefit from support for distributed (also known as multi-shard) ACID transactions. You might mistakenly believe that you can use Cassandra features such as quorum writes and reads, lightweight transactions, and secondary indexes to achieve single-key ACID guarantees. This is because the Cassandra marketing and technical documentation over the years has promoted it as a "consistent-enough" database, which is far from the truth. The only good use for Apache Cassandra is in context of its original intent as a inexpensive, eventually-consistent store for large volumes of data. Newer Cassandra-compatible databases such as DataStax Enterprise and ScyllaDB suffer from the same problems as Apache Cassandra because they have not changed the design of the eventually-consistent core.

If you simultaneously require strong consistency, low latency, and high density, you should use a database that is not only Cassandra-compatible but also transactional. This is exactly what YugabyteDB offers. Each of the mentioned problems is solved by YugabyteDB at the core of its architecture.

- Single-key writes go through Raft (which also uses quorum) in YugabyteDB; reads, however are quorumless and hence can be served off a single node both for strongly-consistent and timeline-consistent (also known as bounded staleness) cases.

- Multi-key transactions are also supported through the use of a transaction manager that uses an enhanced 2-phase commit protocol.

- Lightweight transactions are obviated altogether in YugabyteDB because the Raft-based writes on a single key are automatically linearizable.

- Secondary indexes are global similar to the primary indexes so only the nodes storing the secondary indexes are queried.

Following are the details of the key differences between YugabyteDB and [Apache Cassandra](http://cassandra.apache.org/).

## Data consistency

YugabyteDB is a strongly-consistent system that avoids several pitfalls of an eventually-consistent database.

1. Dirty reads: Systems with an eventually-consistent core offer weaker semantics than even asynchronous
replication. To work around this, many Apache Cassandra deployments use quorum read/write modes, but
even those do NOT offer [clean rollback semantics on write failures](https://stackoverflow.com/questions/12156517/whats-the-difference-between-paxos-and-wr-n-in-cassandra) and hence can still lead to dirty reads.

1. Deletes resurfacing: Another problem due to an eventually-consistent core is [deleted values resurfacing](https://stackoverflow.com/questions/35392430/cassandra-delete-not-working).

YugabyteDB avoids these pitfalls by using a theoretically sound replication model based on Raft, with strong consistency on writes and tunable consistency options for reads.

## High read latency of eventual consistency

In eventually-consistent systems, anti-entropy, read-repairs, and so on hurt performance and increase cost. But even in steady state, read operations used from a quorum to serve closer to correct responses and fix inconsistencies. For a replication factor of 3, such a system's read throughput is essentially one third.

With YugabyteDB, strongly-consistent reads from leaders, as well as timeline-consistent or asynchronous reads from followers perform the read operation on only one node instead of three.

## Lightweight transactions

In Apache Cassandra, basic read-modify-write operations (also known as compare-and-set) such as increment, conditional updates, `INSERT …  IF NOT EXISTS` or `UPDATE ... IF EXISTS` use a scheme known as lightweight transactions [which incurs a 4-round-trip cost](https://teddyma.gitbooks.io/learncassandra/content/concurrent/concurrency_control.html) between replicas. With YugabyteDB, these operations only involve one round trip between the quorum members.

## Secondary indexes

Local secondary indexes in Apache Cassandra ([see blog](https://pantheon.io/blog/cassandra-scale-problem-secondary-indexes)) require a fan-out read to all nodes; in other words, index performance keeps dropping as cluster size increases. With YugabyteDB's distributed transactions, secondary indexes are both strongly-consistent and point-reads rather than a read from all nodes/shards in the cluster.

## Data modeling

Apache Cassandra is a flexi-schema database that supports single-key data modeling. On the other hand, YugabyteDB is a multi-model and multi-API database that supports multiple different types of data modeling including flexi-schema and document data via the native JSONB data type support in the Cassandra-compatible YCQL API. Additionally, YugabyteDB supports relational data modeling via the PostgreSQL-compatible YSQL API.

Additional details on the document data modeling of both databases is warranted. Apache Cassandra's JSON support can be misleading. CQL allows `SELECT` and `INSERT` statements to include the JSON keyword. The `SELECT` output is available in the JSON format and the `INSERT` inputs can be specified in the JSON format. However, this JSON support is an ease-of-use abstraction in the CQL layer and the underlying database engine is unaware of it. Because there is no native JSON data type in CQL, the schema does not have any knowledge of the JSON that you have provided. This means the schema definition does not change nor does the schema enforcement. As a Cassandra developer, you previously needed native JSON support and now have no choice but to add a new document database such as MongoDB or Couchbase into your data tier.

With YugabyteDB's native JSON support, developers can now benefit from the structured query language of Cassandra and the document data modeling of MongoDB in a single database.

## Operational stability and challenges when adding nodes

1. Apache Cassandra's eventually-consistent core implies that a new node trying to join a cluster cannot just copy data files from current "leader". A logical read (also known as quorum read) across multiple surviving peers is needed with Apache Cassandra to build the correct copy of the data to bootstrap a new node with. These reads also need to uncompress and recompress the data. With YugabyteDB, a new node can be bootstrapped by copying already compressed data files from the leader of the corresponding shard.

1. Apache Cassandra is implemented in Java. Garbage collection poses additional problems, especially when running with large heap sizes. YugabyteDB, however, is implemented in C++.

1. Operations like anti-entropy and read-pair hurt steady-state stability of cluster as they consume additional system resources. With Yugabyte, which does replication using distributed consensus, these operations are not needed because the replicas stay in sync using Raft, or catch up the deltas cleanly from the transaction log of the current leader.

1. Apache Cassandra needs constant tuning of compactions (because of Java implementation, non-scalable, non-partitioned bloom filters and index metadata, lack of scan-resistant caches, and so on).

1. Adding a node when the cluster is at risk of running out of disk space can give relief in steps over time. The new server will start serving requests as soon as 1 tablet has been transferred. While in Cassandra the old nodes get no benefit until after adding node and then doing `nodetool cleanup` which could take many hours each.

## Operational flexibility

You may need to move your database infrastructure to new hardware or you may want to add a synchronous or asynchronous replica in another region or in a public cloud. With YugabyteDB, these operations are basic one-click intent-based operations that are handled seamlessly by the system completely online. YugabyteDB's core data fabric and its consensus-based replication model enables the data tier to be very agile and recomposable, similarly to containers' and VMs' handing of the application or stateless tier.

## Need to scale beyond single or two-data-center deployments

Current RDBMS (using asynchronous replication) and NoSQL solutions work up to 2 data center deployments, but in a very restrictive manner. With Apache Cassandra's 2-DC deployments, you have to choose between write unavailability on a partition between the data centers, or cope with an asynchronous replication model where during a DC failure some inflight data is lost.

YugabyteDB's distributed consensus based replication design, in three-data-center deployments for example, enables enterprises to be highly available for reads and writes even on a complete data center outage, without having to take a down time or resort to an older or asynchronous copy of the data, which may not have all the changes to the system.

## Relevant blog posts

A few blog posts that highlight how YugabyteDB differs from Apache Cassandra are as follows:

- [Apache Cassandra: The Truth Behind Tunable Consistency, Lightweight Transactions & Secondary Indexes](https://www.yugabyte.com/blog/apache-cassandra-lightweight-transactions-secondary-indexes-tunable-consistency/)
- [Building a Strongly Consistent Cassandra with Better Performance](https://www.yugabyte.com/blog/building-a-strongly-consistent-cassandra-with-better-performance)
- [DynamoDB vs MongoDB vs Cassandra for Fast Growing Geo-Distributed Apps](https://www.yugabyte.com/blog/dynamodb-vs-mongodb-vs-cassandra-for-fast-growing-geo-distributed-apps/)
- [YugabyteDB 1.1 New Feature: Document Data Modeling with the JSON Data Type](https://www.yugabyte.com/blog/yugabyte-db-1-1-new-feature-document-data-modeling-with-json-data-type/)
- [YugabyteDB 1.1 New Feature: Speeding Up Queries with Secondary Indexes](https://www.yugabyte.com/blog/yugabyte-db-1-1-new-feature-speeding-up-queries-with-secondary-indexes/)

## Learn more

- [YCQL API](../../../api/ycql/)
- [YCQL - Cassandra 3.4 compatibility](../../../explore/ycql-language/cassandra-feature-support)
