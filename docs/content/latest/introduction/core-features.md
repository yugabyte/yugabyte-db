---
title: Core Features
linkTitle: Core Features
description: Core Features
aliases:
  - /introduction/core-features/
type: page
menu:
  latest:
    identifier: core-features
    parent: introduction
    weight: 40
---

## 1. Linear scalability

The figure below represents a single YugaByte DB cluster, also known as a Universe. Each node in the universe runs the YugaByte Tablet Server (aka YB-TServer) and optionally the YugaByte Master Server (aka the YB-Master). Note that the number of YB-Masters in an universe equals the replication factor configured for the universe. More details can be found in the [Architecture](/architecture/concepts/universe/) section.

Each node runs on top of a instance that provides a Linux-based compute and a set of persistent disks, preferably locally attached SSDs. The cloud provider can be any of the major public cloud providers, an on-premises datacenter or even Docker engine. Universes can be linearly expanded to add new nodes in an existing availability zone, or a new availabilty zone or even new regions at any point of time. They can also be shrinked to remove unused nodes as per business needs all without taking any downtime or performance slowdown.

![YugaByte DB Architecture](/images/intro/linear-scalability.png)

Shards of tables, also known as [tablets](/architecture/concepts/sharding/), are automatically created and managed via the YB-TServer. Application clients connect to the YB-TServer using either [Apache Cassandra Query Language (CQL)](https://docs.datastax.com/en/cql/3.1/cql/cql_reference/cqlReferenceTOC.html) or the [Redis API](https://redis.io/commands). The YB-Masters manage the metadata of these shards and can be configured to run on nodes different than the Tablet Server. Tablets are stored in [DocDB](../../architecture/concepts/persistence/), YugaByte DB's own Log Structured Merge (LSM) based transactional data store that has been purpose-built for mission-critical use cases. Finally, data for each tablet is [consistently replicated](/architecture/concepts/replication/) onto other nodes using the [Raft distributed consensus algorithm](https://raft.github.io/raft.pdf).

## 2. Fault tolerance

YugaByte DB's architecture is completely decentralized and hence has no single point of failure. All nodes in the universe are identical from a client perspective since all nodes run the YB-TServer. Some nodes additionally run the YB-Master but these servers do not participate in the critical path unless there has been a configuration change (resulting from nodes getting added/removed or underlying infrastructure failures) in the universe. At that time, the client collects the new metadata from the YB-Master leader just once and caches the metadata for future use.

## 3. Multi-active availability

In terms of the [CAP theorem](https://blog.yugabyte.com/a-for-apple-b-for-ball-c-for-cap-theorem-8e9b78600e6d), YugaByte DB is a Consistent and Partition-tolerant (CP) database. It ensures High Availability (HA) for most practical situations even while remaining strongly consistent. While this may seem to be a violation of the CAP theorem, that is not the case. CAP treats availability as a binary option whereas YugaByte DB treats availability as a percentage that can be tuned to achieve high write availability (reads are always available as long as a single node is available). 

- During network partitions or node failures, the replicas of the impacted tablets (whose leaders got partitioned out or lost) form two groups: a majority partition that can still establish a Raft consensus and a minority partition that cannot establish such a consensus (given the lack of quorum). The replicas in the majority partition elect a new leader among themselves in a matter of seconds and are ready to accept new writes after the leader election completes. For these few seconds till the new leader is elected, the DB is unable to accept new writes given the design choice of prioritizing consistency over availabililty. All the leader replicas in the minority partition lose their leadership during these few seconds and hence become followers. 

- Majority partitions are available for both reads and writes. Minority partitions are available for reads only (even if the data may get stale as time passes) but not available for writes. **Multi-active availability** refers to YugaByte DB's ability to serve writes on any node of a non-partitioned cluster and reads on any node of a partitioned cluster.

- The above approach obviates the need for any unpredictable background anti-entropy operations as well as need to establish quorum at read time. As shown in the [YCSB benchmarks against Apache Cassandra](https://forum.yugabyte.com/t/ycsb-benchmark-results-for-yugabyte-and-apache-cassandra-again-with-p99-latencies/99), YugaByte DB delivers predictable p99 latencies as well as 3x read throughput that is also timeline-consistent (given no quorum is needed at read time).

YugaByte DB's architecture is similar to that of [Google Cloud Spanner](https://cloudplatform.googleblog.com/2017/02/inside-Cloud-Spanner-and-the-CAP-Theorem.html) which is also a CP database with high write availability. While Google Cloud Spanner leverages Google's proprietary network infrastructure, YugaByte DB is designed work on commodity infrastructure used by most enterprise users.

## 3. Distributed ACID transactions

Writes (and background data rebalancing in general) are guaranteed to be zero data loss by virtue of single row ACID transactions. Raft distributed consensus algorithm is used for each tablet's replication. Each tablet's Raft consensus group is comprised of a tablet leader and set of tablet-peers (aka tablet followers). Leader owns the interaction with clients for write requests and acknowledges the write as committed only after it synchronously replicates to other node. Loss of the leader makes the remaining members of the group auto-elect a new leader among themselves in a matter of couple seconds. 

![Strongly consistent writes](/images/intro/strongly-consistent-writes.png)

## 4. Low latency, timeline-consistent, always-on reads

YugaByte DB enables a spectrum of consistency options when it comes to reads while keeping writes always strongly consistent via sync replication (based on Raft distributed consensus). The read consistency options that can be set on a per client basis are Strong (default), Bounded staleness, Session, Consistent Prefix. Eventual consistency is not supported given that such a consistency level is not appropriate for mission-critical applications.

Given the use of distributed consensus where reads are either served only by one single node (either by the leader for strong consistency level or by a follower for all other consistency levels), YugaByte DB reads have 3x throughput compared to a traditional NoSQL database that uses quorum to establish the same consistency levels. 

![Tunably consistent reads](/images/intro/tunably-consistent-reads.png)

## 5. Multi-region deployments

YugaByte DB can be easily deployed across multiple region with sync replication for primary data (where replicas act as voting members) and async replication for timeline-consistent data (where replicas act as non-voting/observing members). Note that async replication is a feature restricted to the [Enterprise Edition](https://www.yugabyte.com/product/enterprise/).

## 6. Multi-cloud portability
YugaByte DB is inspired by [Google Cloud Spanner](https://cloud.google.com/spanner/) from an architectural standpoint but instead of using proprietary infrastructure (such as TrueTime) that Spanner uses, YugaByte is built to run on commodity infrastructure while still remaining decoupled from the exact cloud platform. In other words, there is no cloud lock-in with YugaByte DB and it can be run on any of the public clouds or an on-premises/private datacenter.

## 7. Multi-API

YugaByte DB supports 2 popular APIs namely [Apache Cassandra Query Language (CQL)](https://docs.datastax.com/en/cql/3.1/cql/cql_reference/cqlReferenceTOC.html) and [Redis](https://redis.io/commands) command library. Additionally, it extends both these APIs with native datatypes and operations with the goal of simplifying geo-distributed, transactional, high performance application development.

PostgreSQL API support is in Beta.

## 8. Multi-model

YugaByte DB can be used to model multiple different data workloads such as flexible schema, time-series, key-value. The first 2 models are best built using Cassandra Query Language while the last one lends itself to the Redis command library.
