---
title: Introduction
linkTitle: Introduction
description: Introduction
image: /images/section_icons/index/introduction.png
section: GET STARTED
block_indexing: true
menu:
  v1.3:
    identifier: introduction
    weight: 150
isTocNested: true
showAsideToc: true
---

## What is YugabyteDB?

<!--
<div class="video-wrapper">
{{< vimeo 305074082 >}}
</div>
-->

YugabyteDB is a high-performance distributed SQL database for powering global, internet-scale applications. Built using a unique combination of high-performance document store, , per-shard distributed consensus replication and multi-shard ACID transactions (inspired by Google Spanner), YugabyteDB serves both scale-out RDBMS and internet-scale OLTP workloads with low query latency, extreme resilience against failures and global data distribution. As a cloud native database, it can be deployed across public and private clouds as well as in Kubernetes environments with ease.

YugabyteDB is developed and distributed as an [Apache 2.0 open source project](https://github.com/yugabyte/yugabyte-db/).

## What makes YugabyteDB unique?

YugabyteDB is a transactional database that brings together three must-have needs of cloud native microservices, namely SQL as a flexible query language, low-latency read performance and globally-distributed write scalability. Monolithic SQL databases offer SQL and low-latency reads but do not have ability to scale writes across multiple nodes and/or regions. Distributed NoSQL databases offer performance and write scalablility but give up on SQL semantics such as multi-key access, ACID transactions and strong consistency.

YugabyteDB feature highlights are listed below.

### 1. SQL and ACID transactions

- SQL [JOINs](../quick-start/explore-ysql/#3-joins) and [distributed transactions](../quick-start/explore-ysql/#4-distributed-transactions) that allow multi-row access across any number of shards at any scale.

- Transactional [document store](../architecture/concepts/docdb/) backed by self-healing, strongly consistent [replication](../architecture/concepts/docdb/replication/).

### 2. High performance and massive scalability

- Low latency for geo-distributed applications with multiple [read consistency levels](../architecture/concepts/docdb/replication/#tunable-read-consistency) and [read replicas](../architecture/concepts/docdb/replication/#read-only-replicas).

- Linearly scalable throughput for ingesting and serving ever-growing datasets.

### 3. Global data consistency

- [Global data distribution](../explore/global-distribution/) that brings consistent data close to users through multi-region and multi-cloud deployments.

- [Auto-sharding & auto-rebalancing](../explore/auto-sharding/) to ensure uniform load across all nodes even for very large clusters.

### 4. Cloud native

- Built for the container era with [highly elastic scaling](../explore/linear-scalability/) and infrastructure portability, including [Kubernetes-driven orchestration](../quick-start/install/#kubernetes).

- [Self-healing database](../explore/fault-tolerance/) that automatically tolerates any failures common in the inherently unreliable modern cloud infrastructure.

### 5. Open source

- Fully functional distributed database available under [Apache 2.0 open source license](https://github.com/yugabyte/yugabyte-db/). 

### 6. Built-in enterprise features

- Starting [v1.3](https://blog.yugabyte.com/announcing-yugabyte-db-v1-3-with-enterprise-features-as-open-source/), only open source distributed SQL database to have built-in enterprise features such as Distributed Backups, Data Encryption, and Read Replicas. Upcoming features such as Change Data Capture and 2 Data Center Deployments are also included in open source.

## What client APIs are supported by YugabyteDB?

YugabyteDB supports two flavors of distributed SQL.

### 1. Yugabyte SQL (YSQL)

[YSQL](../api/ysql/), currently in Beta, is a fully relational SQL API that is wire compatible with the SQL language in PostgreSQL. It is best fit for RDBMS workloads that need horizontal write scalability and global data distribution while also using relational modeling features such as JOINs, distributed transactions and referential integrity (such as foreign keys). Get started by [exploring YSQL features](../quick-start/explore-ysql/).

### 2. Yugabyte Cloud QL (YCQL)

[YCQL](../api/ycql/) is a SQL-based flexible-schema API that is best fit for internet-scale OLTP apps needing a semi-relational API highly optimized for write-intensive applications as well as blazing-fast queries. It supports distributed transactions, strongly consistent secondary indexes and a native JSON column type. YCQL has its roots in the Cassandra Query Language. Get started by [exploring YCQL features](../api/ycql/quick-start/).

{{< note title="Note" >}}
The YugabyteDB APIs are isolated and independent from one another today. This means that the data inserted or managed by one API cannot be queried by the other API. Additionally, there is no common way to access the data across the APIs (external frameworks such as [Presto](../develop/ecosystem-integrations/presto/) can help for simple cases). 

<b>The net impact is that application developers have to select an API first before undertaking detailed database schema/query design and implementation.</b>
{{< /note >}}

## How does YugabyteDB's common document store work?

[DocDB](../architecture/concepts/docdb/), YugabyteDB's distributed document store common across all APIs, is built using a custom integration of Raft replication, distributed ACID transactions and the RocksDB storage engine. Specifically, DocDB enhances RocksDB by transforming it from a key-value store (with only primitive data types) to a document store (with complex data types). **Every key is stored as a separate document in DocDB, irrespective of the API responsible for managing the key.** DocDB’s [sharding](../architecture/concepts/docdb/sharding/), [replication/fault-tolerance](../architecture/concepts/docdb/replication/) and [distributed ACID transactions](../architecture/transactions/distributed-txns/) architecture are all based on the [Google Spanner design](https://research.google.com/archive/spanner-osdi2012.pdf) first published in 2012. [How We Built a High Performance Document Store on RocksDB?](https://blog.yugabyte.com/how-we-built-a-high-performance-document-store-on-rocksdb/) provides an in-depth look into DocDB.

## What are the trade-offs involved in using YugabyteDB?

Trade-offs depend on the type of database used as baseline for comparison.

### Monolithic SQL

Examples: PostgreSQL, MySQL, Oracle, Amazon Aurora.

**Benefits of YugabyteDB**

- Scale write throughput linearly across multiple nodes and/or geographic regions. 
- Automatic failover and native repair.

**Trade-offs**

- Transactions and JOINs can now span multiple nodes, thereby increasing latency.

Learn more: [Distributed PostgreSQL on a Google Spanner Architecture – Query Layer](https://blog.yugabyte.com/distributed-postgresql-on-a-google-spanner-architecture-query-layer/)

### Traditional NewSQL

Examples: Vitess, Citus

**Benefits of YugabyteDB**

- Distributed transactions across any number of nodes.
- No single point of failure given all nodes are equal.

**Trade-offs**

- None

Learn more: [Rise of Globally Distributed SQL Databases – Redefining Transactional Stores for Cloud Native Era](https://blog.yugabyte.com/rise-of-globally-distributed-sql-databases-redefining-transactional-stores-for-cloud-native-era/)

### Transactional NoSQL 

Examples: MongoDB, Amazon DynamoDB, FoundationDB, Azure Cosmos DB.

**Benefits of YugabyteDB**

- Flexibility of SQL as query needs change in response to business changes.
- Distributed transactions across any number of nodes.
- Low latency, strongly consistent reads given that read-time quorum is avoided altogether.

**Trade-offs**

- None

Learn more: [Why are NoSQL Databases Becoming Transactional?](https://blog.yugabyte.com/nosql-databases-becoming-transactional-mongodb-dynamodb-faunadb-cosmosdb/)

### Eventually Consistent NoSQL

Examples: Apache Cassandra, Couchbase.

**Benefits of YugabyteDB**

- Flexibility of SQL as query needs change in response to business changes.
- Strongly consistent, zero data loss writes.
- Strongly consistent as well as timeline-consistent reads without resorting to eventual consistency-related penalties such as read repairs and anti-entropy.

**Trade-offs**

- Extremely short unavailability during the leader election time for all shard leaders lost during a node failure or network partition. 

Learn more: [Apache Cassandra: The Truth Behind Tunable Consistency, Lightweight Transactions & Secondary Indexes](https://blog.yugabyte.com/apache-cassandra-lightweight-transactions-secondary-indexes-tunable-consistency/)
