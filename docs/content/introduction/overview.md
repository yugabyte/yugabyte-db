---
title: Overview
weight: 30
---

## What is YugaByte DB?

YugaByte DB is an open source, transactional, high performance database for planet-scale applications. It is meant to be a system-of-record/authoritative database that geo-distributed applications can rely on for correctness and availability. It allows applications to easily scale up and scale down across multiple regions in the public cloud, on-premises datacenters or across hybrid environments without creating operational complexity or increasing the risk of outages.

In terms of data model and APIs, YugaByte DB currently supports Apache Cassandra Query Language (CQL) & its client drivers natively. It extends CQL by adding [distributed ACID transactions](/explore/transactions/) and strongly consistent secondary indexes. PostgreSQL support is on the roadmap.

YugaByte DB also supports an auto-sharded, clustered, elastic Redis-as-a-Database in a Redis driver compatible manner. It also extends Redis with a new native [Time Series](https://blog.yugabyte.com/extending-redis-with-a-native-time-series-data-type-e5483c7116f8) data type.

## What makes YugaByte DB unique?

YugaByte DB is a single operational database that brings together 3 must-have needs of user-facing cloud applications, namely ACID transactions, high performance and multi-region scalability. Monolithic SQL databases offer transactions and performance but do not have ability to scale across multi-regions. Distributed NoSQL databases offer performance and multi-region scalablility but give up on transactional guarantees.

### 1. Transactional

- [Distributed acid transactions](/explore/transactions/) that allow multi-row updates across any number of shards at any scale.
- Adaptive, fault-tolerant [storage system](/architecture/concepts/persistence/) that's backed by a self-healing, strongly consistent [replication](/architecture/concepts/replication/).

### 2. High Performance

- Low latency for geo-distributed OLTP applications with multiple [read consistency levels](/architecture/concepts/replication/#tunable-read-consistency) and [read-only replicas](/architecture/concepts/replication/#read-only-replicas).

- High throughput for ingesting and serving ever-growing datasets.

### 3. Planet-Scale

- [Global data distribution](https://www.yugabyte.com/solutions/deployments/multi-region/) that brings consistent data close to users through multi-region and multi-cloud deployments.

- Built for the container era with highly elastic scaling and infrastructure portability, including [Kubernetes-driven orchestration](/quick-start/install/#kubernetes).


