---
date: 2016-03-08T21:07:13+01:00
title: Welcome to YugaByte Docs
type: index
weight: 0
---

## Introduction

YugaByte is a new cloud native, globally distributed database that brings together the best of both NewSQL and NoSQL. On one hand, NewSQL systems are a good fit for mission-critical applications given their strongly consistent cores but are unable to provide geo-redundant availability derived from running across multiple datacenters. On the other hand, NoSQL systems are comparably easier to run across multi-datacenters but are incredibly difficult to develop in the context of mission-critical applications given the loose guarantees of eventual consistency. 

![YugaByte value prop](/images/value-prop.png)

YugaByte ensures that the above long-held compromises are no longer impeding the development and deployment of mission-critical applications. YugaByte has a strongly consistent core similar to NewSQL systems and is also highly available even in multi-datacenter deployments. Additionally, it adds the much needed layer of cloud native operational simplicity so that modern technical operations teams can easily exploit the full potential of their chosen cloud(s).

## Key features

### 1. Multi-active availability 
YugaByte is a Consistent & Partition tolerant (CP) database but with High Availability (HA). Reads are always highly available and writes are highly available as long as a majority of replicas are available.

### 2. Tunable consistency
YugaByte enables a spectrum of consistency options when it comes to reads while keeping writes always strongly consistent via sync replication (based on distributed consensus). The read consistency options that can be set on a per client basis are Strong (default), Session, Consistent Prefix. Bounded staleness option is on the roadmap.

### 3. Multi-API
YugaByte currently supports 2 open APIs namely [Apache Cassandra Query Language (CQL)](https://docs.datastax.com/en/cql/3.1/cql/cql_reference/cqlReferenceTOC.html) and [Redis] (https://redis.io/commands) command library. SQL support is on the roadmap.

### 4. Multi-model
YugaByte can be used to model multiple different data workloads such as Flexible schema, Time-series, Key-value. The first 2 models are best built using CQL while the last one lends itself to the Redis command library.

### 5. Multi-cloud
YugaByte is inspired by Google Spanner from an architectural standpoint but instead of using proprietary infrastructure (such as TrueTime) that Spanner uses, YugaByte is built to run on commodity infrastructure. In other words, there is no cloud lock-in with YugaByte and it can be run on any of the public clouds or an on-premises/private datacenter.


## Key benefits

### Cloud native technical operations

- **Simple scalability**: Linear, fast & reliable scalability from 1 to 1000s nodes with automatic sharding and rebalancing
- **Zero downtime and zero data loss operations**: Highly available under any unplanned infrastructure failure or any planned software/hardware upgrade
- **Multi-datacenter deployments simplified**: 1-click geo-redundant deployments across multiple availability zones & regions on public/private/hybrid clouds as well as across multiple on-premises data centers 
- **Online cross-cloud mobility**: move across cloud infrastructure providers without any lock-in
- **Hardware flexibility**: seamlessly move from one type of compute and storage to another for cost and performance reasons

### Agile application development

- **Open ecosystem**: Choose between 2 popular NoSQL APIs rather than learn a new proprietary language, even port existing applications fast by leveraging the maturity of these ecosystems 
- **Tunable consistency**: Strongly consistent writes and tunably consistent reads 
- **High performance**: A ground-up C++ implementation combined with 3x faster reads compared to simple quorum-based consistency of traditional NoSQL
- **Converged caching**: Avoid explicit management of the memory and consistency associated with an independent cache that typically fronts a persistent database


## Enterprise apps best served

Three types of mission-critical OLTP/HTAP enterprise applications are best served by YugaByte. These map to the three types of data models offered.

### Flexible schema

Distributed OLTP applications (such as retail product catalog, unified customer profile, global identity, transactional systems-of-record, centralized configuration management etc.) that need to be run across multi-datacenters for high availability and also require a semi-structured yet easily changeable schema. These applications can benefit heavily from YugaByte’s tunable consistency options as well as the ability to add/remove attributes in the schema in a completely online manner without any hidden performance issues. Such applications use YugaByte’s CQL support.

### Time-series

HTAP applications (such as application/infrastructure monitoring as well as industrial/consumer Internet-of-Things) that require high write throughput for append-only workloads deployed on cost-efficient tiered storage as well as Apache Spark integration for batch/real-time analytics. These applications also use YugaByte’s CQL support.

### Key-value

Mission-critical key-value applications (such as financial transaction processing, stock quotes and sports feeds) that require low latency reads and writes without the overhead of explicit memory management. Such applications use YugaByte’s Redis API support.
