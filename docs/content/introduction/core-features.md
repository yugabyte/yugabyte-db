---
date: 2016-03-08T21:07:13+01:00
title: Core Features
weight: 4
---

## Linear scalability

The figure below represents a single YugaByte DB cluster, also known as a Universe. Each node in the universe runs the YugaByte Tablet Server (aka YB-TServer) and optionally the YugaByte Master Server (aka the YB-Master). Note that the number of YB-Masters in an universe equals the replication factor configured for the universe. More details can be found [here](/architecture/concepts/universe/).

Each node runs on top of a instance that provides a Linux-based compute and a set of persistent disks, preferably locally attached SSDs. The cloud provider can be any of the major public cloud providers, an on-premises datacenter or even Docker engine. Universes can be linearly expanded to add new nodes in an existing availability zone, or a new availabilty zone or even new regions at any point of time. They can also be shrinked to remove unused nodes as per business needs all without taking any downtime or performance slowdown.

![YugaByte Architecture](/images/linear-scalability.png)

Shards of tables, also known as [tablets](/architecture/concepts/sharding/), are automatically created and managed via the YB-TServer. Application clients connect to the YB-TServer using either [Apache Cassandra Query Language (CQL)](https://docs.datastax.com/en/cql/3.1/cql/cql_reference/cqlReferenceTOC.html) or the [Redis API](https://redis.io/commands). The YB-Masters manage the metadata of these shards and can be configured to run on nodes different than the Tablet Server. Tablets are stored in [DocDB](/architecture/concepts/persistence/), YugaByte DB's own Log Structured Merge (LSM) based transactional data store that has been purpose-built for mission-critical use cases. Finally, data for each tablet is [consistently replicated](/architecture/concepts/replication/) onto other nodes using the [Raft distributed consensus algorithm](https://raft.github.io/raft.pdf).

## Fault tolerance

YugaByte DB's architecture is completely decentralized and hence has no single point of failure. All nodes in the universe are identical from a client perspective since all nodes run the YB-TServer. Some nodes additionally run the YB-Master but these servers do not participate in the critical path unless there has been a configuration change (resulting from nodes getting added/removed or underlying infrastructure failures) in the universe. At that time, the client collects the new metadata from the YB-Master leader just once and caches the metadata for future use.

## Multi-active availability

In terms of the CAP theorem, YugaByte DB is a Consistent and Partition-tolerant (CP) database. It provides High Availability (HA) for most practical situations even while remaining strongly consistent. While this may seem a violation of the CAP theorem, that is not the case since CAP treats availability as a binary option whereas YugaByte treats availability as a percentage that can be tuned to achieve high write availability (reads are always available as long as a single node is available). During network partitions, the replicas for the impacted tablets form two groups: majority partition that can still establish a Raft consensus and a minority partition that cannot establish such a consensus (given the lack of quorum). Majority partitions are available for both reads and writes. Minority partitions are available for reads only (even if the data may get stale as time passes) but not available for writes. **Multi-active availability** here refers to YugaByte's ability to serve reads in any partition of a cluster. Note that requiring majority of replicas to synchronously agree on the value written is by design to ensure strong write consistency and thus obviate the need for any unpredictable background anti-entropy operations. 

## Zero data loss writes

Writes (and background data rebalancing in general) are guaranteed to be zero data loss. This guarantee comes through the use of Raft distributed consensus algorithm for each tablet's replication. Each tablet's Raft consensus group is comprised of a tablet leader and set of tablet-peers (aka tablet followers). Leader owns the interaction with clients for write requests and acknowledges the write as committed only after it synchronously replicates to other node. Loss of the leader makes the remaining members of the group auto-elect a new leader among themselves in a matter of couple seconds. 

![Strongly consistent writes](/images/strongly-consistent-writes.png)

## Low latency, always-on reads

YugaByte DB enables a spectrum of consistency options when it comes to reads while keeping writes always strongly consistent via sync replication (based on Raft distributed consensus). The read consistency options that can be set on a per client basis are Strong (default), Bounded staleness, Session, Consistent Prefix. Eventual consistency is not supported given that such a consistency level is not appropriate for mission-critical applications.

Given the use of distributed consensus where reads are either served only by one single node (either by the leader for strong consistency level or by a follower for all other consistency levels), YugaByte DB reads have 3x throughput compared to a traditional NoSQL database that uses quorum to establish the same consistency levels. 

![Tunably consistent reads](/images/tunably-consistent-reads.png)

## Multi-API

YugaByte DB currently supports 2 popular APIs namely [Apache Cassandra Query Language (CQL)](https://docs.datastax.com/en/cql/3.1/cql/cql_reference/cqlReferenceTOC.html) and [Redis] (https://redis.io/commands) command library. SQL support is in the works.

## Multi-model

YugaByte DB can be used to model multiple different data workloads such as flexible schema, time-series, key-value. The first 2 models are best built using CQL while the last one lends itself to the Redis command library.

## Multi-datacenter deployments

YugaByte DB can be easily deployed across multiple datacenters with sync replication for primary data (where replicas act as voting members) and async replication for timeline consistent data (where replicas act as non-voting/observing members). Note that async replication is a feature restricted to [YugaByte Enterprise Edition](https://www.yugabyte.com/product/).

## Multi-cloud portability
YugaByte DB is inspired by [Google Cloud Spanner](https://cloud.google.com/spanner/) from an architectural standpoint but instead of using proprietary infrastructure (such as TrueTime) that Spanner uses, YugaByte is built to run on commodity infrastructure while still remaining decoupled from the exact cloud platform. In other words, there is no cloud lock-in with YugaByte DB and it can be run on any of the public clouds or an on-premises/private datacenter.
