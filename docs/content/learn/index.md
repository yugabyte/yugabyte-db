---
date: 2016-03-09T20:08:11+01:00
title: Learn
weight: 40
---

## Linearly scalable

The figure below represents a single instance of the YugaByte database. A group of such instances form a YugaByte cluster, also known as a Universe. Each instance runs on top of a node that provides a Linux-based compute and a set of persistent disks, preferably locally attached SSDs. The cloud provider for the node can be any of the major public cloud providers, an on-premises data center or even Docker engine. 

![YugaByte Architecture](/images/architecture.png)

Clients connect to the database using either [Apache Cassandra Query Language (CQL)] (https://docs.datastax.com/en/cql/3.1/cql/cql_reference/cqlReferenceTOC.html) or the [Redis API] (https://redis.io/commands). Shards of tables, also known as Tablets, are automatically created and managed via the Tablet Server. The Master Servers manage the metadata of these shards and can be run on a node different than the Tablet Server. At the lowest level, data is stored in a Log Structured Merge (LSM) based transactional data store that has been purpose-built for flexible schema, time series and key-value use cases. Finally, data for each tablet is consistently replicated onto other nodes using the [Raft distributed consensus algorithm] (https://raft.github.io/raft.pdf). 

Universes can be linearly expanded to add new instances, availabilty zones and even regions. They can also be shrinked to remove unused instances as per business needs all without taking any downtime or performance slowdown.

## Decentralized

All nodes in the universe are identical from a client perspective.

## Highly available

YugaByte’s unique distributed storage and replication architecture described above provides high availability for most practical situations even while remaining strongly consistent. During network partitions, the replicas for the impacted tablets form two groups: majority partition that can still establish a Raft consensus and a minority partition that cannot establish such a consensus given the lack of quorum. Majority partitions are available for both writes and reads. Minority partitions are available for reads only (even if the data may get stale as time passes) but not available for writes. This write unavailability is by design to ensure strong consistency on writes and thus obviate the need for any performance impacting anti-entropy operations.

## YugaWare

An integral part of YugaByte is YugaWare, it’s admin console responsible for all day-to-day operations. Through its built-in orchestration capabilities, YugaWare handles cluster management functions such as create, expand, shrink universe for all supported cloud providers including on-premises data centers. These operations are available via the UI for new users as well as a REST API for advanced users needing programmable deployments. It also provides performance and availability monitoring based on metrics (pulled from Prometheus metric exporters running on each instance) and health checks.

