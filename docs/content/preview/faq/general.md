---
title: YugabyteDB FAQS
headerTitle: General FAQ
linkTitle: General FAQ
description: YugabyteDB FAQ — How does YugabyteDB work? When is YugabyteDB database a good fit? What APIs does YugabyteDB support? And other frequently asked questions.
aliases:
  - /preview/faq/product/
  - /preview/introduction/overview/
  - /preview/introduction/benefits/
menu:
  preview_faq:
    identifier: faq-general
    parent: faq
    weight: 10
type: docs
unversioned: true
rightNav:
  hideH3: true
  hideH4: true
---

## YugabyteDB

### What is YugabyteDB

YugabyteDB is a high-performant, highly available and scalable distributed SQL database designed for powering global, internet-scale applications. It is fully compatible with [PostgreSQL](https://www.postgresql.org/) and provides strong [ACID](/preview/architecture/key-concepts/#acid) guarantees for distributed transactions. It can be deployed in a single region, multi-region, and multi-cloud setups.

{{<link dest="/preview/explore/" text="Explore YugabyteDB" icon-before="true">}}

### What makes YugabyteDB unique

YugabyteDB stands out as a unique database solution due to its combination of features that bring together the strengths of both traditional SQL databases and modern NoSQL systems. It is [horizontally scalable](/preview/explore/linear-scalability/), supports global geo-distribution, supports [SQL (YSQL)](/preview/explore/ysql-language-features/sql-feature-support/) and [NoSQL (YCQL)](/preview/explore/ycql-language/) APIs, is [highly performant](/preview/benchmark/) and gurantees strong transactional consistency.

{{<link dest="/preview/architecture/design-goals/" text="Design goals" icon-before="true">}}

### Is YugabyteDB open source?

YugabyteDB is 100% open source. It is licensed under Apache 2.0.

{{<link dest="https://github.com/yugabyte/yugabyte-db" text="Source code" icon-before="true">}}

### How many major releases YugabyteDB has had so far?

YugabyteDB released its first beta, [v0.9](https://www.yugabyte.com/blog/yugabyte-has-arrived/) in November 2017. Since then, several stable and preview versions have been released. The current stable version is {{<release "stable">}} and the current preview version is {{<release "preview">}}.

{{<link dest="/preview/releases/ybdb-releases/" text="Releases" icon-before="true">}}

### What is the difference between preview and stable versions

Preview releases include features under active development and are recommended for development and testing only. Stable releases undergo rigorous testing for a longer period of time and are ready for production use.

{{<link dest="/preview/releases/versioning/#feature-maturity" text="Release versioning" icon-before="true">}}

### What are the upcoming features

The roadmap for upcoming releases and the list of recently released featured can be found on the [yugabyte-db](https://github.com/yugabyte/yugabyte-db) repository on GitHub.

{{<link dest="https://github.com/yugabyte/yugabyte-db#whats-being-worked-on" text="Current roadmap" icon-before="true">}}

### Which companies are currently using YugabyteDB in production?

Global organizations of all sizes leverage YugabyteDB to fulfill their application requirements.

{{<link dest="https://www.yugabyte.com/success-stories/" text="Success Stories" icon-before="true">}}

### How do I report a security vulnerability?

Follow the steps in the [vulnerability disclosure policy](../../secure/vulnerability-disclosure-policy) to report a vulnerability to our security team. The policy outlines our commitments to you when you disclose a potential vulnerability, the reporting process, and how we will respond.

### What are YugabyteDB Anywhere and YugabyteDB Aeon?

**[YugabyteDB](../../)** is the 100% open source core database. It is the best choice for startup organizations with strong technical operations expertise looking to deploy to production with traditional DevOps tools.

**[YugabyteDB Anywhere](../../yugabyte-platform/)** is commercial software for running a self-managed YugabyteDB-as-a-Service. It has built-in cloud native operations, enterprise-grade deployment options, and world-class support.

**[YugabyteDB Aeon](../../yugabyte-cloud/)** is a fully-managed cloud service on Amazon Web Services (AWS), Microsoft Azure, and Google Cloud Platform (GCP). [Sign up](https://cloud.yugabyte.com/) to get started.

{{<link dest="https://www.yugabyte.com/compare-products/" text="Compare deployment options" icon-before="true">}}

### When is YugabyteDB a good fit?

YugabyteDB is a good fit for fast-growing, cloud-native applications that need to serve business-critical data reliably, with zero data loss, high availability, and low latency. Common use cases include:

- Distributed Online Transaction Processing (OLTP) applications needing multi-region scalability without compromising strong consistency and low latency. For example, user identity, Retail product catalog, Financial data service.

- Hybrid Transactional/Analytical Processing (HTAP) (also known as Translytical) applications needing real-time analytics on transactional data. For example, user personalization, fraud detection, machine learning.

- Streaming applications needing to efficiently ingest, analyze, and store ever-growing data. For example, IoT sensor analytics, time series metrics, real-time monitoring.

### When is YugabyteDB not a good fit?

YugabyteDB is not a good fit for traditional Online Analytical Processing (OLAP) use cases that need complete ad-hoc analytics. Use an OLAP store such as [Druid](http://druid.io/druid.html) or a data warehouse such as [Snowflake](https://www.snowflake.net/).

### What is a YugabyteDB universe

A YugabyteDB [universe](/preview/architecture/key-concepts/#universe) comprises one [primary cluster](/preview/architecture/key-concepts/#primary-cluster) and zero or more [read replica clusters](/preview/architecture/key-concepts/#read-replica-cluster) that collectively function as a resilient and scalable distributed database. It is common to have just a primary cluster and hence the terms cluster and universe are sometimes used interchangeably but it is worthwhile to note that they are different.

{{<link dest="/preview/architecture/key-concepts/" text="More key concepts" icon-before="true">}}

### Are there any performance benchmarks available?

YugabyteDB is regularly benchmarked using a variety of standard benchmarks like [TPC-C](/preview/benchmark/tpcc/), [YCSB](/preview/benchmark/ycsb-ysql/), and [sysbench](/preview/benchmark/sysbench-ysql/).

{{<link dest="/preview/benchmark/" text="Benchmark" icon-before="true">}}

### How is YugabyteDB tested for correctness

Apart from the rigorous failure testing, YugabyteDB passes most of the scenarios in [Jepsen](https://jepsen.io/) testing. Jepsen is a methodology and toolset used to verify the correctness of distributed systems, particularly in the context of consistency models and fault tolerance and has become a standard for stress-testing distributed databases, data stores, and other distributed systems.

{{<link dest="/preview/benchmark/resilience/jepsen-testing/" text="Jepsen testing" icon-before="true">}}

### How does YugabyteDB compare to other databases

We have published detailed comparison information against multiple SQL and NoSQL databases:

- **SQL** - [CockroachDB](../comparisons/cockroachdb/), [TiDB](../comparisons/tidb/), [Vitess](../comparisons/vitess/), [Amazon Aurora](../comparisons/amazon-aurora/), [Google Spanner](../comparisons/google-spanner/)
- **NOSQL** - [MongoDB](../comparisons/mongodb/), [FoundationDB](../comparisons/foundationdb/), [Cassandra](../comparisons/cassandra/), [DynamoDB](../comparisons/amazon-dynamodb/), [CosmosDB](../comparisons/azure-cosmos/)

{{<link dest="../comparisons/" text="Compare YugabyteDB to other databases" icon-before="true">}}

## PostgreSQL support

### How compatible is YugabyteDB with PostgreSQL

YugabyteDB is [wire-protocol, syntax, feature and runtime](https://www.yugabyte.com/postgresql/postgresql-compatibility/) compatible with PostgreSQL. But that said, supporting all PostgreSQL features in a distributed system is not always feasible.

{{<link dest="/preview/explore/ysql-language-features/postgresql-compatibility/#unsupported-postgresql-features" text="PostgreSQL compatibility" icon-before="true">}}

### Can I use my existing PostgreSQL tools and drivers with YugabyteDB

Yes. YugabyteDB is [fully compatible](#how-compatible-is-yugabytedb-with-postgresql) with PostgreSQL and automatically works well with most of PostgreSQL tools.

{{<link dest="/preview/integrations/" text="Integrations" icon-before="true">}}

### Are PostgreSQL extensions supported

YugabyteDB pre-bundles many popular extensions and these should be readily available on your cluster. But given the distributed nature of YugabyteDB, not all extensions are supported by default.

{{<link dest="/preview/explore/ysql-language-features/pg-extensions/" text="PostgreSQL extensions" icon-before="true">}}

### How can I migrate from PostgreSQL

YugabyteDB is fully compatible with PostgreSQL and hence most PostgreSQL applications should work as is. To address corner cases, we have published a [comprehensive guide](https://docs.yugabyte.com/stable/manage/data-migration/migrate-from-postgres/) to help you migrate from PostgreSQL.

{{<link dest="/preview/manage/data-migration/" text="Migrate data" icon-before="true">}}

## Architecture

### How does YugabyteDB distribute data

The table data is split into [tablets](/preview/architecture/key-concepts/#tablet) and the table rows are mapped to the tablets via [sharding](/preview/explore/linear-scalability/data-distribution/). The tablets themselves are distributed across the various nodes in the cluster.

{{<link dest="/preview/explore/linear-scalability/data-distribution/" text="Data distribution" icon-before="true">}}

### How does YugabyteDB scale

YugabyteDB scales seamlessly when new nodes are added to the cluster without any service disruption. Table data is [stored distributed](#how-does-yugabytedb-distribute-data) in tablets. When new nodes are added, the rebalancer moves certain tablets to other nodes and keeps the number of tablets on each node more or less the same. As data grows, these tablets also split into two and are moved to other nodes.

{{<link dest="/preview/explore/linear-scalability/" text="Horizontal scalability" icon-before="true">}}

### How does YugabyteDB provide high availability

YugabyteDB replicates [tablet](/preview/architecture/key-concepts/#tablet) data onto [followers](/preview/architecture/key-concepts/#tablet-follower) of the tablet via [RAFT](/preview/architecture/docdb-replication/raft/) consensus. This ensures that a consistent copy of the data is available in case of failures. On failures, one of the tablet followers is promoted to be the [leader](/preview/architecture/key-concepts/#tablet-leader).

{{<link dest="/preview/explore/fault-tolerance/" text="Resiliency and high availability" icon-before="true">}}

### How is data consistency maintained across multiple nodes

Every write (insert, update, delete) to the data is replicated via [RAFT](/preview/architecture/docdb-replication/raft/) consensus to [tablet followers](/preview/architecture/key-concepts/#tablet-follower) as per the [replication factor (RF)](/stable/architecture/key-concepts/#replication-factor-rf) of the cluster. Before acknowledging the write operation back to the client, YugabyteDB ensures that the data is replicated to a quorum (RF/2 + 1) of followers.

{{<link dest="/preview/architecture/docdb-replication/replication/" text="Synchronous replication" icon-before="true">}}

### What is tablet splitting

Data is stored in [tablets](/preview/architecture/key-concepts/#tablet). As the tablet grows, the tablet splits into two. This enables some data to be moved to other nodes in the cluster.

{{<link dest="/preview/architecture/docdb-sharding/tablet-splitting/" text="Tablet splitting" icon-before="true">}}

### Are indexes colocated with tables

Indexes are not typically colocated with the base table. The sharding of indexes is based on the primary key of the index and is independent of how the main table is sharded/distributed which is based on the primary key of the table.

{{<link dest="/preview/explore/ysql-language-features/indexes-constraints/" text="Explore indexes" icon-before="true">}}
<br>
{{<link dest="/preview/explore/colocation/" text="Colocating tables" icon-before="true">}}

### How can YugabyteDB be both CP and ensure high availability at the same time?

In terms of the [CAP theorem](https://www.yugabyte.com/blog/a-for-apple-b-for-ball-c-for-cap-theorem-8e9b78600e6d), YugabyteDB is a consistent and partition-tolerant (CP) database. It ensures high availability (HA) for most practical situations even while remaining strongly consistent. While this may seem to be a violation of the CAP theorem, that is not the case. CAP treats availability as a binary option whereas YugabyteDB treats availability as a percentage that can be tuned to achieve high write availability (reads are always available as long as a single node is available).

{{<link dest="/preview/architecture/design-goals/#partition-tolerance-cap" text="Partition Tolerance - CAP" icon-before="true">}}
