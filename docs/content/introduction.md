---
date: 2016-03-08T21:07:13+01:00
title: Introduction
weight: 2
---

## Overview

### What is YugaByte DB?

YugaByte DB is an open source, cloud-native database for mission-critical enterprise applications. It is meant to be a system-of-record/authoritative database that applications can rely on for correctness and availability. It allows applications to easily scale up and scale down in the cloud, on-premises or across hybrid environments without creating operational complexity or increasing the risk of outages.

In terms of data model and APIs, YugaByte currently supports **Apache Cassandra Query Language** & its client drivers natively. In addition, it also supports an automatically sharded, clustered & elastic **Redis-as-a-Database** in a Redis driver compatible manner. **Distributed transactions** to support **strongly consistent secondary indexes**, multi-table/row ACID operations and SQL support is on the roadmap.

### What makes YugaByte DB unique?

Mission-critical applications are typically composed of microservices with diverse workloads such as key/value, flexible schema, graph or relational. The access patterns vary as well. SaaS services or mobile/web applications keeping customer records, order history or messages need zero-data loss, geo-replication, low-latency reads/writes and a consistent customer experience. While fast data infrastructure use cases (such as IoT, finance, timeseries data) need near real-time & high-volume ingest, low-latency reads, and native integration with analytics frameworks like Apache Spark.

YugaByte offers polyglot persistence to power these diverse workloads and access patterns in a unified database, while providing strong correctness guarantees and high availability. You are no longer forced to create infrastructure silos for each workload or choose between different flavors SQL and NoSQL databases. YugaByte breaks down the barrier between SQL and NoSQL by offering both.

Another theme common across these microservices is the move to a cloud-native architecture, be it on the public cloud, on-premises or hybrid environment. The primary driver for this move is making the infrastructure agile, scalable, re-configurabile with zero downtime, geo-distributed and portable across clouds. While the container ecosystem led by Docker & Kubernetes has enabled enterprises to realize this vision for the stateless tier, the data tier has remained a big challenge.  YugaByte is purpose-built to address these challenges, but for the data tier, and serves as the stateful complement to containers.

## Core features

### Linear scalability

The figure below represents a single YugaByte cluster, also known as a Universe. Each node in the universe runs the YugaByte Tablet Server (aka YB-TServer) and optionally the YugaByte Master Server (aka the YB-Master). Note that the number of YB-Masters in an universe equals the replication factor configured for the universe.

Each node runs on top of a instance that provides a Linux-based compute and a set of persistent disks, preferably locally attached SSDs. The cloud provider can be any of the major public cloud providers, an on-premises datacenter or even Docker engine. Universes can be linearly expanded to add new nodes in an existing availability zone, or a new availabilty zone or even new regions at any point of time. They can also be shrinked to remove unused nodes as per business needs all without taking any downtime or performance slowdown.

![YugaByte Architecture](/images/linear-scalability.png)

Shards of tables, also known as Tablets, are automatically created and managed via the YB-TServer. Application clients connect to the YB-TServer using either [Apache Cassandra Query Language (CQL)] (https://docs.datastax.com/en/cql/3.1/cql/cql_reference/cqlReferenceTOC.html) or the [Redis API] (https://redis.io/commands). The YB-Masters manage the metadata of these shards and can be configured to run on nodes different than the Tablet Server. Tablets are stored in DocDB, YugaByte's own Log Structured Merge (LSM) based transactional data store that has been purpose-built for mission-critical use cases. Finally, data for each tablet is consistently replicated onto other nodes using the [Raft distributed consensus algorithm] (https://raft.github.io/raft.pdf).

### Decentralized, no single point of failure

All nodes in the universe are identical from a client perspective since all nodes run the YB-TServer. Some nodes additionally run the YB-Master but these servers do not participate in the critical path unless there has been a configuration change (resulting from nodes getting added/removed or underlying infrastructure failures) in the universe. At that time, the client collects the new metadata from the YB-Master leader just once and caches the metadata for future use.

### Multi-active availability

In terms of the CAP theorem, YugaByte DB is a Consistent and Partition-tolerant (CP) database. It provides High Availability (HA) for most practical situations even while remaining strongly consistent. While this may seem a violation of the CAP theorem, that is not the case since CAP treats availability as a binary option whereas YugaByte treats availability as a percentage that can be tuned to achieve high write availability (reads are always available as long as a single node is available). During network partitions, the replicas for the impacted tablets form two groups: majority partition that can still establish a Raft consensus and a minority partition that cannot establish such a consensus (given the lack of quorum). Majority partitions are available for both reads and writes. Minority partitions are available for reads only (even if the data may get stale as time passes) but not available for writes. **Multi-active availability** here refers to YugaByte's ability to serve reads in any partition of a cluster. Note that requiring majority of replicas to synchronously agree on the value written is by design to ensure strong write consistency and thus obviate the need for any unpredictable background anti-entropy operations. 

### Strong write consistency

Writes (and data replication in general) are always strongly consistent in YugaByte. The foundation for this consistency comes through the use of Raft distributed consensus algorithm for each tablet's replication. Each tablet's Raft consensus group is comprised of a tablet leader and set of tablet-peers (aka tablet followers). Loss of the leader makes the remaining members of the group auto-elect a new leader among themselves in a matter of couple seconds. Leader takes ownership of interacting with client for the write request and acknowledges the write as complete only after it synchronously replicates to other node. 

![Strongly consistent writes](/images/strongly-consistent-writes.png)

### Tunable read consistency

YugaByte enables a spectrum of consistency options when it comes to reads while keeping writes always strongly consistent via sync replication (based on Raft distributed consensus). The read consistency options that can be set on a per client basis are Strong (default), Bounded staleness, Session, Consistent Prefix. Eventual consistency is not supported given that such a consistency level is not appropriate for mission-critical applications.

Given the use of distributed consensus where reads are either served only by one single node (either by the leader for strong consistency level or by a follower for all other consistency levels), YugaByte reads have 3x throughput compared to a traditional NoSQL database that uses quorum to establish the same consistency levels. 

![Tunably consistent reads](/images/tunably-consistent-reads.png)

### Multi-datacenter ready with sync and async replication options
YugaByte can be easily deployed across multiple datacenters with sync replication for primary data (where replicas act as voting members) and async replication for timeline consistent data (where replicas act as non-voting/observing members). Note that async replication is a feature restricted to [YugaByte Enterprise Edition](/#product-editions).

### Multi-API
YugaByte currently supports 2 popular APIs namely [Apache Cassandra Query Language (CQL)](https://docs.datastax.com/en/cql/3.1/cql/cql_reference/cqlReferenceTOC.html) and [Redis] (https://redis.io/commands) command library. SQL support is in the works.

### Multi-model
YugaByte can be used to model multiple different data workloads such as flexible schema, time-series, key-value. The first 2 models are best built using CQL while the last one lends itself to the Redis command library.

### Cloud-native
YugaByte is inspired by [Google Cloud Spanner](https://cloud.google.com/spanner/) from an architectural standpoint but instead of using proprietary infrastructure (such as TrueTime) that Spanner uses, YugaByte is built to run on commodity infrastructure while still remaining decoupled from the exact cloud platform. In other words, there is no cloud lock-in with YugaByte and it can be run on any of the public clouds or an on-premises/private datacenter.

## Key benefits

### Agile application development

- **Popular APIs and mature ecosystems**: Choose between 2 popular NoSQL APIs rather than learn a new proprietary language, even port existing applications fast by leveraging the maturity of these ecosystems. SQL support is on the roadmap.
- **Tunable consistency**: Strongly consistent writes and tunably consistent reads 
- **High performance**: A ground-up C++ implementation combined with 3x read throughput compared to simple quorum-based consistency of traditional NoSQL
- **Converged caching**: Avoid explicit management of the memory and consistency associated with an independent cache that typically fronts a persistent database

### Cloud-native technical operations

- **Simple scalability**: Linear, fast & reliable scalability from 1 to 1000s nodes with automatic sharding and re-balancing
- **Zero downtime and zero data loss operations**: Highly available under any unplanned infrastructure failure or any planned software/hardware upgrade
- **Multi-datacenter/multi-cloud deployments simplified**: 1-click geo-redundant deployments across multiple availability zones & regions on public/private/hybrid clouds as well as across multiple on-premises data centers 
- **Online cross-cloud mobility**: move across cloud infrastructure providers without any lock-in
- **Hardware flexibility**: seamlessly move from one type of compute and storage to another for cost and performance reasons

## Get started 

The easiest way to get started with YugaByte is to quick start a Community Edition local cluster using the instructions provided [here](/community-edition/quick-start/).

