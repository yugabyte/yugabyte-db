---
date: 2016-03-09T20:08:11+01:00
title: Learn
weight: 60
---

## Linearly scalable

The figure below represents a single instance of the YugaByte database. A group of such instances form a YugaByte cluster, also known as a Universe. Each instance runs on top of a node that provides a Linux-based compute and a set of persistent disks, preferably locally attached SSDs. The cloud provider for the node can be any of the major public cloud providers, an on-premises datacenter or even Docker engine. Universes can be linearly expanded to add new instances, availabilty zones and even regions at any point of time. They can also be shrinked to remove unused instances as per business needs all without taking any downtime or performance slowdown.

![YugaByte Architecture](/images/architecture.png)

Clients connect to the database using either [Apache Cassandra Query Language (CQL)] (https://docs.datastax.com/en/cql/3.1/cql/cql_reference/cqlReferenceTOC.html) or the [Redis API] (https://redis.io/commands). Shards of tables, also known as Tablets, are automatically created and managed via the Tablet Server. The Master Servers manage the metadata of these shards and can be run on a node different than the Tablet Server. At the lowest level, data is stored in a Log Structured Merge (LSM) based transactional data store that has been purpose-built for flexible schema, time series and key-value use cases. Finally, data for each tablet is consistently replicated onto other nodes using the [Raft distributed consensus algorithm] (https://raft.github.io/raft.pdf). 


## Decentralized

All nodes in the universe are identical from a client perspective.

## Multi-active availability

YugaByte’s unique distributed storage and replication architecture provides high availability for most practical situations even while remaining strongly consistent. During network partitions, the replicas for the impacted tablets form two groups: majority partition that can still establish a Raft consensus and a minority partition that cannot establish such a consensus given the lack of quorum. Majority partitions are available for both reads and writes. Minority partitions are available for reads only (even if the data may get stale as time passes) but not available for writes. Requiring majority of replicas to synchronously agree on the value written is by design to ensure strong write consistency and thus obviate the need for any performance impacting anti-entropy operations.

## Strongly consistent writes

Writes (and data replication in general) are always strongly consistent in YugaByte. The foundation for this consistency comes through the use of Raft distributed consensus algorithm. Each Raft consensus group is comprised of a Raft leader and set of Raft followers. Loss of the leaders to the remaining members of the group auto-electing a new leader among them in a matter of seconds. Leader takes ownership of interacting with client for the write request and acknowledges the write as complete only after it synchronously replicates to other node. 

![Strongly consistent writes](/images/strongly-consistent-writes.png)

## Tunably consistent reads

YugaByte enables a spectrum of consistency options when it comes to reads while keeping writes always strongly consistent via sync replication (based on Raft distributed consensus). The read consistency options that can be set on a per client basis are Strong (default), Bounded staleness, Session, Consistent Prefix. Eventual consistency is not supported given that such a consistency level is not appropriate for mission-critical applications.

Given the use of distributed consensus where reads are either served only by one single node (either by the leader for strong consistency level or by a follower for all other consistency levels), YugaByte reads are 3x faster compared to a traditional NoSQL database that uses quorum to establish the same consistency levels. 

![Tunably consistent reads](/images/tunably-consistent-reads.png)

## YugaWare

An integral part of YugaByte is YugaWare, it’s admin console responsible for all day-to-day operations. Through its built-in orchestration capabilities, YugaWare handles cluster management functions such as create, expand, shrink universe for all supported cloud providers including on-premises datacenters. These operations are available via the UI for new users as well as a REST API for advanced users needing programmable deployments. It also provides performance and availability monitoring based on metrics (pulled from Prometheus metric exporters running on each instance) and health checks.

