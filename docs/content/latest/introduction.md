---
title: Introduction
linkTitle: Introduction
description: Introduction
image: /images/section_icons/index/introduction.png
aliases:
  - /latest/introduction/overview/
  - /latest/introduction/benefits/
  - /latest/introduction/core-features/
menu:
  latest:
    identifier: introduction
    weight: 20
isTocNested: false
showAsideToc: true
---

## What is YugaByte DB?


<div class="video-wrapper">
{{< vimeo 305074082 >}}
</div>

YugaByte DB is a high-performance SQL database for powering internet-scale, globally-distributed applications. Built using a unique combination of high-performance document store, auto sharding, per-shard distributed consensus replication and multi-shard ACID transactions (inspired by Google Spanner), YugaByte DB serves both scale-out RDBMS and internet-scale OLTP workloads with low query latency, extreme resilience against failures and global data distribution. As a cloud native database, it can be deployed across public and private clouds as well as in Kubernetes environments with ease.

YugaByte DB Community Edition is developed and distributed as an [Apache 2.0 open source project](https://github.com/YugaByte/yugabyte-db/).

## What makes YugaByte DB unique?

YugaByte DB is a single operational database that brings together three must-have needs of user-facing cloud applications, namely ACID transactions, high performance and multi-region scalability. Monolithic SQL databases offer transactions and performance but do not have ability to scale across multi-regions. Distributed NoSQL databases offer performance and multi-region scalablility but give up on transactional guarantees.

Additionally, for the first time ever, application developers have unparalleled freedom when it comes to modeling data for workloads that require internet-scale, transactions and geo-distribution. As highlighted previously, they have two transactional NoSQL APIs and a distributed SQL API to choose from.

YugaByte DB feature highlights are listed below.

### 1. Transactional

- [Distributed acid transactions](../explore/transactional/) that allow multi-row updates across any number of shards at any scale.

- Transactional [document store](../architecture/concepts/docdb/) backed by self-healing, strongly consistent [replication](../architecture/concepts/docdb/replication/).

### 2. High Performance

- Low latency for geo-distributed applications with multiple [read consistency levels](../architecture/concepts/docdb/replication/#tunable-read-consistency) and [read-only replicas](../architecture/concepts/docdb/replication/#read-only-replicas).

- High throughput for ingesting and serving ever-growing datasets.

### 3. Planet Scale

- [Global data distribution](../explore/planet-scale/global-distribution/) that brings consistent data close to users through multi-region and multi-cloud deployments.

- [Auto-sharding](../explore/planet-scale/auto-sharding/) and [auto-rebalancing](../explore/planet-scale/auto-rebalancing/) to ensure uniform load balancing across all nodes even for very large clusters.

### 4. Cloud Native

- Built for the container era with [highly elastic scaling](../explore/cloud-native/linear-scalability/) and infrastructure portability, including [Kubernetes-driven orchestration](../quick-start/install/#kubernetes).

- [Self-healing database](../explore/cloud-native/fault-tolerance/) that automatically tolerates any failures common in the inherently unreliable modern cloud infrastructure.

### 5. Open Source

- Fully functional distributed database available under [Apache 2.0 open source license](https://github.com/YugaByte/yugabyte-db/). Upgrade to [Enterprise Edition](https://www.yugabyte.com/product/compare/) anytime.


## What client APIs are supported by YugaByte DB?

YugaByte DB supports two flavors of distributed SQL.

2. [YCQL](../api/ycql/) - YCQL is a SQL-based flexible-schema API that is best fit for internet-scale OLTP apps needing a semi-relational API highly optimized for write-intensive applications as well as blazing-fast query needs. It supports [distributed ACID transactions](../explore/transactional/acid-transactions/), [strongly consistent secondary indexes](../explore/transactional/secondary-indexes/) and a [native JSON column type](../explore/transactional/json-documents/). YCQL has its roots in the [Apache Cassandra Query Language (CQL)](https://docs.datastax.com/en/cql/3.1/cql/cql_reference/cqlReferenceTOC.html). 

3. [YSQL (Beta)](../api/ysql/) - YSQL is a fully relational SQL API that is wire compatible with the SQL language in [PostgreSQL](https://www.postgresql.org/docs/10/sql-syntax.html). It is best fit for RDBMS workloads that need horizontal scalability and global distribution while also using relational data modeling features such as JOINs, referential integrity, and multi-table transactions.


{{< note title="Note" >}}
The YugaByte DB APIs are completely isolated and independent from one another. This means that the data inserted or managed by one API cannot be queried by th other API. Additionally, there is no common way to access the data across the APIs (external frameworks such as [Presto](../develop/ecosystem-integrations/presto/) can help for simple cases). 

<b>The net impact is that application developers have to select an API first before undertaking detailed database schema/query design and implementation.</b>
{{< /note >}}


## How does YugaByte DB's common document store work?

[DocDB](../architecture/concepts/persistence/), YugaByte DB's distributed document store common across all APIs, builds on top of the popular RocksDB project by transforming RocksDB from a key-value store (with only primitive data types) to a document store (with complex data types). **Every key is stored as a separate document in DocDB, irrespective of the API responsible for managing the key.** DocDB’s [sharding](../architecture/concepts/sharding/), [replication/fault-tolerance](../architecture/concepts/replication/) and [distributed ACID transactions](../architecture/transactions/distributed-txns/) architecture are all based on the the [Google Spanner design](https://research.google.com/archive/spanner-osdi2012.pdf) first published in 2012.

