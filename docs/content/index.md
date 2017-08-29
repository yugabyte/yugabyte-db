---
date: 2016-03-08T21:07:13+01:00
title: Introduction
type: index
weight: 1
---

YugaByte is a new open-source, cloud-native database for mission-critical applications that brings together the best of both NewSQL and NoSQL. 

- NewSQL databases are a good fit for mission-critical applications given their strongly consistent cores. However, these databases are typically limited to single datacenter deployments given the need for a highly reliable network. 
- NoSQL databases are comparably easier to run across multi-datacenters given their ability to scale both reads and writes linearly. However, these databases are incredibly difficult to develop and reason about given the loose guarantees of eventually consistent data replication. 

![YugaByte value prop](/images/value-prop.png)

YugaByte ensures that the above long-held compromises are no longer impeding the development and deployment of mission-critical applications. YugaByte has a strongly consistent core similar to NewSQL systems and is also highly available even in multi-datacenter deployments similar to NoSQL systems. Additionally, it adds the much needed layer of cloud native operational simplicity so that modern technical operations teams can easily exploit the full potential of their chosen cloud(s).

## Key features 

### Linear scalability

The figure below represents a single node of the YugaByte database. A group of such nodes form a YugaByte cluster, also known as a Universe. Each node runs on top of a instance that provides a Linux-based compute and a set of persistent disks, preferably locally attached SSDs. The cloud provider for the instance can be any of the major public cloud providers, an on-premises datacenter or even Docker engine. Universes can be linearly expanded to add new nodes in an existing availability zone, or a new availabilty zone or even new regions at any point of time. They can also be shrinked to remove unused nodes as per business needs all without taking any downtime or performance slowdown.

![YugaByte Architecture](/images/architecture.png)

Clients connect to the database using either [Apache Cassandra Query Language (CQL)] (https://docs.datastax.com/en/cql/3.1/cql/cql_reference/cqlReferenceTOC.html) or the [Redis API] (https://redis.io/commands). Shards of tables, also known as Tablets, are automatically created and managed via the YB-TServer (aka YugaByte Tablet Server). The YB-Masters (aka the YugaByte Master Servers) manage the metadata of these shards and can be run on a node different than the Tablet Server. At the lowest level, data is stored in a Log Structured Merge (LSM) based transactional data store that has been purpose-built for flexible schema, time series and key-value use cases. Finally, data for each tablet is consistently replicated onto other nodes using the [Raft distributed consensus algorithm] (https://raft.github.io/raft.pdf). 

### Decentralized

All nodes in the universe are identical from a client perspective since all nodes run the YugaByte Tablet Server. Some nodes additionally run the YugaByte Master Server running on them but these Master Servers do not participate in the critical path unless there has been a configuration change (such as nodes added or removed) in the universe. At that time, the client collects the new metadata from the Master Server leader just once and caches the metadata for future use. 

### Multi-active availability

YugaByte’s unique distributed storage and replication architecture provides high availability for most practical situations even while remaining strongly consistent. While this may seem as a violation of the CAP theorem, that is not the case in reality. During network partitions, the replicas for the impacted tablets form two groups: majority partition that can still establish a Raft consensus and a minority partition that cannot establish such a consensus given the lack of quorum. Majority partitions are available for both reads and writes. Minority partitions are available for reads only (even if the data may get stale as time passes) but not available for writes. Requiring majority of replicas to synchronously agree on the value written is by design to ensure strong write consistency and thus obviate the need for any performance impacting anti-entropy operations.

### Strong write consistency

Writes (and data replication in general) are always strongly consistent in YugaByte. The foundation for this consistency comes through the use of Raft distributed consensus algorithm. Each Raft consensus group is comprised of a Raft leader and set of Raft followers. Loss of the leaders to the remaining members of the group auto-electing a new leader among them in a matter of seconds. Leader takes ownership of interacting with client for the write request and acknowledges the write as complete only after it synchronously replicates to other node. 

![Strongly consistent writes](/images/strongly-consistent-writes.png)

### Tunable read consistency

YugaByte enables a spectrum of consistency options when it comes to reads while keeping writes always strongly consistent via sync replication (based on Raft distributed consensus). The read consistency options that can be set on a per client basis are Strong (default), Bounded staleness, Session, Consistent Prefix. Eventual consistency is not supported given that such a consistency level is not appropriate for mission-critical applications.

Given the use of distributed consensus where reads are either served only by one single node (either by the leader for strong consistency level or by a follower for all other consistency levels), YugaByte reads have 3x throughput compared to a traditional NoSQL database that uses quorum to establish the same consistency levels. 

![Tunably consistent reads](/images/tunably-consistent-reads.png)

### Multi-datacenter ready with sync and async replication options
YugaByte can be easily deployed across multiple datacenters. 

### Multi-API
YugaByte currently supports 2 open APIs namely [Apache Cassandra Query Language (CQL)](https://docs.datastax.com/en/cql/3.1/cql/cql_reference/cqlReferenceTOC.html) and [Redis] (https://redis.io/commands) command library. SQL support is on the roadmap.

### Multi-model
YugaByte can be used to model multiple different data workloads such as Flexible schema, Time-series, Key-value. The first 2 models are best built using CQL while the last one lends itself to the Redis command library.

### Multi-cloud
YugaByte is inspired by [Google Cloud Spanner](https://cloud.google.com/spanner/) from an architectural standpoint but instead of using proprietary infrastructure (such as TrueTime) that Spanner uses, YugaByte is built to run on commodity infrastructure. In other words, there is no cloud lock-in with YugaByte and it can be run on any of the public clouds or an on-premises/private datacenter.


## Key benefits

### Cloud-native technical operations

- **Simple scalability**: Linear, fast & reliable scalability from 1 to 1000s nodes with automatic sharding and re-balancing
- **Zero downtime and zero data loss operations**: Highly available under any unplanned infrastructure failure or any planned software/hardware upgrade
- **Multi-datacenter deployments simplified**: 1-click geo-redundant deployments across multiple availability zones & regions on public/private/hybrid clouds as well as across multiple on-premises data centers 
- **Online cross-cloud mobility**: move across cloud infrastructure providers without any lock-in
- **Hardware flexibility**: seamlessly move from one type of compute and storage to another for cost and performance reasons

### Agile application development

- **Popular APIs and mature ecosystems**: Choose between 2 popular NoSQL APIs rather than learn a new proprietary language, even port existing applications fast by leveraging the maturity of these ecosystems. SQL support is on the roadmap.
- **Tunable consistency**: Strongly consistent writes and tunably consistent reads 
- **High performance**: A ground-up C++ implementation combined with 3x read throughput compared to simple quorum-based consistency of traditional NoSQL
- **Converged caching**: Avoid explicit management of the memory and consistency associated with an independent cache that typically fronts a persistent database


## Enterprise apps best served

Three types of mission-critical Online Transaction Processing (OLTP) or Hybrid Transactional & Analytical Processing (HTAP) enterprise applications are best served by YugaByte. These map to the three types of data models offered.

### Flexible schema

Distributed OLTP applications (such as retail product catalog, unified customer profile, global identity, transactional systems-of-record, centralized configuration management etc.) that need to be run across multi-datacenters for high availability and also require a semi-structured yet easily changeable schema. These applications can benefit heavily from YugaByte’s tunable consistency options as well as the ability to add/remove attributes in the schema in a completely online manner without any hidden performance issues. Such applications use YugaByte’s CQL support.

### Time-series

HTAP applications (such as application/infrastructure monitoring as well as industrial/consumer Internet-of-Things) that ingest time-series metrics and events essentially require high write throughput for append-only workloads deployed on cost-efficient tiered storage as well as Apache Spark integration for batch/real-time analytics. These applications can directly leverage YugaByte’s CQL support or use other open-source time-series frameworks such as [KairosDB](https://kairosdb.github.io/) that can use Apache Cassandra as a backing store.

### Key-value

Mission-critical key-value applications (such as financial transaction processing, stock quotes and sports feeds) that require low latency reads and writes without the overhead of explicit memory management. Such applications use YugaByte’s Redis API support.

## Product editions

YugaByte offers two ways to get started with YugaByte DB. 

![Product Editions](/images/editions.png)

### Community Edition
Community Edition is the best choice for the individual developer looking to develop applications and deploy the DB into production with traditional DevOps tools. 

### Enteprise Edition
Enterprise Edition includes all the features of the Community Edition as well as additional features such as built-in cloud-native operations, advanced tunable consistency and enterprise security. It is the simplest way to run the DB in mission-critical production environments with one or more datacenters (across both public cloud and on-premises datacenters). 


