---
title: Introduction
linkTitle: Introduction
description: Introduction
image: /images/section_icons/index/introduction.png
menu:
  v1.1:
    identifier: introduction
    weight: 20
isTocNested: false
showAsideToc: true
---

## What is YugaByte DB?


<div class="video-wrapper">
{{< vimeo 305074082 >}}
</div>

YugaByte DB is a transactional, high-performance database for building internet-scale, globally-distributed applications. Built using a unique combination of distributed document store, auto sharding, per-shard distributed consensus replication and multi-shard ACID transactions (inspired by Google Spanner), it is world's only distributed database that is both non-relational (support for Redis-compatible KV & Cassandra-compatible flexible schema transactional NoSQL APIs) and relational (support for PostgreSQL-compatible distributed SQL API) at the same time. 

YugaByte DB is purpose-built to power fast-growing online services on public, private and hybrid clouds with transactional integrity, high availabilty, low latency, high throughput and multi-region scalability while also providing unparalleled data modeling freedom to application architects. Enterprises gain more functional depth and agility without any cloud lock-in when compared to proprietary cloud databases such as Amazon DynamoDB, Microsoft Azure Cosmos DB and Google Cloud Spanner. Enterprises also benefit from stronger data integrity guarantees, more reliable scaling and higher performance than those offered by legacy open source NoSQL databases such as MongoDB and Apache Cassandra. 

YugaByte DB Community Edition is developed and distributed as an [Apache 2.0 open source project](https://github.com/YugaByte/yugabyte-db/).

## What makes YugaByte DB unique?

YugaByte DB is a single operational database that brings together three must-have needs of user-facing cloud applications, namely ACID transactions, high performance and multi-region scalability. Monolithic SQL databases offer transactions and performance but do not have ability to scale across multi-regions. Distributed NoSQL databases offer performance and multi-region scalablility but give up on transactional guarantees.

Additionally, for the first time ever, application developers have unparalleled freedom when it comes to modeling data for workloads that require internet-scale, transactions and geo-distribution. As highlighted previously, they have two transactional NoSQL APIs and a distributed SQL API to choose from.

YugaByte DB feature highlights are listed below.

### 1. Transactional

- [Distributed acid transactions](../explore/transactional/) that allow multi-row updates across any number of shards at any scale.

- Transactional [document store](../architecture/concepts/persistence/) backed by self-healing, strongly consistent [replication](../architecture/concepts/replication/).

### 2. High Performance

- Low latency for geo-distributed applications with multiple [read consistency levels](../architecture/concepts/replication/#tunable-read-consistency) and [read-only replicas](../architecture/concepts/replication/#read-only-replicas).

- High throughput for ingesting and serving ever-growing datasets.

### 3. Planet Scale

- [Global data distribution](../explore/planet-scale/global-distribution/) that brings consistent data close to users through multi-region and multi-cloud deployments.

- [Auto-sharding](../explore/planet-scale/auto-sharding/) and [auto-rebalancing](../explore/planet-scale/auto-rebalancing/) to ensure uniform load balancing across all nodes even for very large clusters.

### 4. Cloud Native

- Built for the container era with [highly elastic scaling](../explore/cloud-native/linear-scalability/) and infrastructure portability, including [Kubernetes-driven orchestration](../quick-start/install/#kubernetes).

- [Self-healing database](../explore/cloud-native/fault-tolerance/) that automatically tolerates any failures common in the inherently unreliable modern cloud infrastructure.

### 5. Open Source

- Fully functional distributed database available under [Apache 2.0 open source license](https://github.com/YugaByte/yugabyte-db/). Upgrade to [Enterprise Edition](https://www.yugabyte.com/product/compare/) anytime.

- Multi-API/multi-model database that extends existing popular and open APIs including Cassandra, Redis and PostgreSQL.


## What client APIs are supported by YugaByte DB?

YugaByte DB supports both Transactional NoSQL and Distributed SQL APIs. 

1. [YEDIS](../api/yedis/) - YEDIS is a transactional key-value API that is wire compatible with [Redis](https://redis.io/commands) commands library and client drivers. YEDIS extends Redis with a new native [Time Series](https://blog.yugabyte.com/extending-redis-with-a-native-time-series-data-type-e5483c7116f8) data type.

2. [YCQL](../api/ycql/) - YCQL is a transactional flexible-schema API that is wire compatible with [Apache Cassandra Query Language (CQL)](https://docs.datastax.com/en/cql/3.1/cql/cql_reference/cqlReferenceTOC.html) and its client drivers. YCQL extends CQL by adding [distributed ACID transactions](../explore/transactional/acid-transactions/), [strongly consistent secondary indexes](../explore/transactional/secondary-indexes/) and a [native JSON column type](../explore/transactional/json-documents/).

3. [YSQL (Beta)](../api/ysql/) - YSQL is a distributed SQL API that is wire compatible with the SQL language in [PostgreSQL](https://www.postgresql.org/docs/10/sql-syntax.html).


{{< note title="Note" >}}
The three YugaByte DB APIs are completely isolated and independent from one another. This means that the data inserted or managed by one API cannot be queried by a different API. Additionally, there is no common way to access the data across all the APIs (external frameworks such as [Presto](../develop/ecosystem-integrations/presto/) can help for simple cases). 

<b>The net impact is that application developers have to select an API first before undertaking detailed database schema/query design and implementation.</b>
{{< /note >}}


## Which API should I select for my application?

**For internet-scale transactional workloads, the question of which API to select is a trade-off between data modeling richness and query performance.**

- On one end of the spectrum is the [YEDIS](../api/yedis/) API that is completely optimized for single key access patterns, has simpler data modeling constructs and provides blazing-fast (sub-ms) query performance. 

- On the other end of the spectrum is the [YSQL](../api/ysql/) API that supports complex multi-key relationships (through JOINS and foreign keys) and provides normal (single-digit ms) query performance. This is expected since multiple keys can be located on multiple shards hosted on multiple nodes, resulting in higher latency than a key-value API that accesses only a single key at any time. 

- At the middle of the spectrum is the [YCQL](../api/ycql/) API that is still optimized for majority single-key workloads but has richer data modeling features such as globally consistent secondary indexes (powered by distributed ACID transactions) that can accelerate internet-scale application development significantly.

## How does YugaByte DB's common document store work?

[DocDB](../architecture/concepts/persistence/), YugaByte DB's distributed document store common across all APIs, builds on top of the popular RocksDB project by transforming RocksDB from a key-value store (with only primitive data types) to a document store (with complex data types). **Every key is stored as a separate document in DocDB, irrespective of the API responsible for managing the key.** DocDB’s [sharding](../architecture/concepts/sharding/), [replication/fault-tolerance](../architecture/concepts/replication/) and [distributed ACID transactions](../architecture/transactions/distributed-txns/) architecture are all based on the the [Google Spanner design](https://research.google.com/archive/spanner-osdi2012.pdf) first published in 2012.

