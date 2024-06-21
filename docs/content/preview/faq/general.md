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

### Contents

##### YugabyteDB

- [What is YugabyteDB?](#what-is-yugabytedb)
- [What makes YugabyteDB unique?](#what-makes-yugabytedb-unique)
- [How many major releases YugabyteDB has had so far?](#how-many-major-releases-yugabytedb-has-had-so-far)
- [Is YugabyteDB open source?](#is-yugabytedb-open-source)
- [Can I deploy YugabyteDB to production?](#can-i-deploy-yugabytedb-to-production)
- [Which companies are currently using YugabyteDB in production?](#which-companies-are-currently-using-yugabytedb-in-production)
- [What is the definition of the "Beta" feature tag?](#what-is-the-definition-of-the-beta-feature-tag)
- [How do YugabyteDB, YugabyteDB Anywhere, and YugabyteDB Aeon differ from each other?](#how-do-yugabytedb-yugabytedb-anywhere-and-yugabytedb-aeon-differ-from-each-other)
- [How do I report a security vulnerability?](#how-do-i-report-a-security-vulnerability)

##### Evaluating YugabyteDB

- [What are the trade-offs involved in using YugabyteDB?](#what-are-the-trade-offs-involved-in-using-yugabytedb)
- [When is YugabyteDB a good fit?](#when-is-yugabytedb-a-good-fit)
- [When is YugabyteDB not a good fit?](#when-is-yugabytedb-not-a-good-fit)
- [Any performance benchmarks available?](#any-performance-benchmarks-available)
- [What about correctness testing?](#what-about-correctness-testing)
- [How does YugabyteDB compare to other SQL and NoSQL databases?](#how-does-yugabytedb-compare-to-other-sql-and-nosql-databases)

##### Architecture

- [How does YugabyteDB's common document store work?](#how-does-yugabytedb-s-common-document-store-work)
- [How can YugabyteDB be both CP and ensure high availability at the same time?](#how-can-yugabytedb-be-both-cp-and-ensure-high-availability-at-the-same-time)
- [Why is a group of YugabyteDB nodes called a universe instead of the more commonly used term clusters?](#why-is-a-group-of-yugabytedb-nodes-called-a-universe-instead-of-the-more-commonly-used-term-clusters)
- [Why is consistent hash sharding the default sharding strategy?](#why-is-consistent-hash-sharding-the-default-sharding-strategy)

## YugabyteDB

### What is YugabyteDB?

<!--
<div class="video-wrapper">
{{< vimeo 305074082 >}}
</div>
-->

YugabyteDB is a high-performance distributed SQL database for powering global, internet-scale applications. Built using a unique combination of high-performance document store, per-shard distributed consensus replication and multi-shard ACID transactions (inspired by Google Spanner), YugabyteDB serves both scale-out RDBMS and internet-scale OLTP workloads with low query latency, extreme resilience against failures and global data distribution. As a cloud native database, it can be deployed across public and private clouds as well as in Kubernetes environments with ease.

YugabyteDB is developed and distributed as an [Apache 2.0 open source project](https://github.com/yugabyte/yugabyte-db/).

### What makes YugabyteDB unique?

YugabyteDB is a transactional database that brings together four must-have needs of cloud native apps - namely SQL as a flexible query language, low-latency performance, continuous availability, and globally-distributed scalability. Other databases do not serve all 4 of these needs simultaneously.

- Monolithic SQL databases offer SQL and low-latency reads, but neither have the ability to tolerate failures, nor can they scale writes across multiple nodes, zones, regions, and clouds.

- Distributed NoSQL databases offer read performance, high availability, and write scalability, but give up on SQL features such as relational data modeling and ACID transactions.

YugabyteDB feature highlights are listed below.

#### SQL and ACID transactions

- SQL [JOINs](../../quick-start/explore/ysql/#join) and [distributed transactions](../../explore/transactions/distributed-transactions-ysql/) that allow multi-row access across any number of shards at any scale.

- Transactional [document store](../../architecture/docdb/) backed by self-healing, strongly-consistent, synchronous [replication](../../architecture/docdb-replication/replication/).

#### High performance and massive scalability

- Low latency for geo-distributed applications with multiple [read consistency levels](../../architecture/docdb-replication/replication/#follower-reads) and [read replicas](../../architecture/docdb-replication/read-replicas/).

- Linearly scalable throughput for ingesting and serving ever-growing datasets.

#### Global data consistency

- [Global data distribution](../../explore/multi-region-deployments/) that brings consistent data close to users through multi-region and multi-cloud deployments. Optional two-region multi-master and master-follower configurations powered by CDC-driven asynchronous replication.

- [Auto-sharding and auto-rebalancing](../../architecture/docdb-sharding/sharding/) to ensure uniform load across all nodes even for very large clusters.

#### Cloud native

- Built for the container era with [highly elastic scaling](../../explore/linear-scalability/) and infrastructure portability, including [Kubernetes-driven orchestration](../../quick-start/kubernetes/).

- [Self-healing database](../../explore/fault-tolerance/) that automatically tolerates any failures common in the inherently unreliable modern cloud infrastructure.

#### Open source

- Fully functional distributed database available under [Apache 2.0 open source license](https://github.com/yugabyte/yugabyte-db/).

#### Built-in enterprise features

- Starting in [v1.3](https://www.yugabyte.com/blog/announcing-yugabyte-db-v1-3-with-enterprise-features-as-open-source/), YugabyteDB is the only open-source distributed SQL database to have built-in enterprise features such as Distributed Backups, Data Encryption, and Read Replicas. New features such as [Change Data Capture (CDC)](../../architecture/docdb-replication/change-data-capture/) and [2 Data Center Deployments](../../architecture/docdb-replication/async-replication/) are also included in open source.

### How many major releases YugabyteDB has had so far?

YugabyteDB has had the following major (stable) releases:

- [v2.20](https://www.yugabyte.com/blog/release-220-announcement/) in November 2023
- [v2.18](https://www.yugabyte.com/blog/release-218-announcement/) in May 2023
- [v2.16](https://www.yugabyte.com/blog/yugabytedb-216/) in December 2022
- [v2.14](https://www.yugabyte.com/blog/announcing-yugabytedb-2-14-higher-performance-and-security/) in July 2022.
- [v2.12](https://www.yugabyte.com/blog/announcing-yugabytedb-2-12/) in February 2022. (There was no v2.10 release.)
- [v2.8](https://www.yugabyte.com/blog/announcing-yugabytedb-2-8/) in November 2021.
- [v2.6](https://www.yugabyte.com/blog/announcing-yugabytedb-2-6/) in July 2021.
- [v2.4](https://www.yugabyte.com/blog/announcing-yugabytedb-2-4/) in January 2021.
- [v2.2](https://www.yugabyte.com/blog/announcing-yugabytedb-2-2-distributed-sql-made-easy/) in July 2020.
- [v2.1](https://www.yugabyte.com/blog/yugabytedb-2-1-is-ga-scaling-new-heights-with-distributed-sql/) in February 2020.
- [v2.0](https://www.yugabyte.com/blog/announcing-yugabyte-db-2-0-ga:-jepsen-tested,-high-performance-distributed-sql/) in September 2019.
- [v1.3](https://www.yugabyte.com/blog/announcing-yugabyte-db-v1-3-with-enterprise-features-as-open-source/) in July 2019.
- [v1.2](https://www.yugabyte.com/blog/announcing-yugabyte-db-1-2-company-update-jepsen-distributed-sql/) in March 2019.
- [v1.1](https://www.yugabyte.com/blog/announcing-yugabyte-db-1-1-and-company-update/) in September 2018.
- [v1.0](https://www.yugabyte.com/blog/announcing-yugabyte-db-1-0) in May 2018.
- [v0.9 Beta](https://www.yugabyte.com/blog/yugabyte-has-arrived/) in November 2017.

Releases, including upcoming releases, are outlined on the [Releases Overview](/preview/releases/) page. The roadmap for this release can be found on [GitHub](https://github.com/yugabyte/yugabyte-db#whats-being-worked-on).

### Is YugabyteDB open source?

Starting with [v1.3](https://www.yugabyte.com/blog/announcing-yugabyte-db-v1-3-with-enterprise-features-as-open-source/), YugabyteDB is 100% open source. It is licensed under Apache 2.0 and the source is available on [GitHub](https://github.com/yugabyte/yugabyte-db).

### Can I deploy YugabyteDB to production?

Yes, both YugabyteDB APIs are production ready. [YCQL](https://www.yugabyte.com/blog/yugabyte-db-1-0-a-peek-under-the-hood/) achieved this status starting with v1.0 in May 2018 while [YSQL](https://www.yugabyte.com/blog/announcing-yugabyte-db-2-0-ga:-jepsen-tested,-high-performance-distributed-sql/) became production ready starting v2.0 in September 2019.

### Which companies are currently using YugabyteDB in production?

Reference deployments are listed in [Success Stories](https://www.yugabyte.com/success-stories/).

### What is the definition of the "Beta" feature tag?

Some features are marked Beta in every release. Following are the points to consider:

- Code is well tested. Enabling the feature is considered safe. Some of these features enabled by default.

- Support for the overall feature will not be dropped, though details may change in incompatible ways in a subsequent beta or GA release.

- Recommended only for non-production use.

Please do try our beta features and give feedback on them on our [Slack community]({{<slack-invite>}}) or by filing a [GitHub issue](https://github.com/yugabyte/yugabyte-db/issues).

### How do YugabyteDB, YugabyteDB Anywhere, and YugabyteDB Aeon differ from each other?

[YugabyteDB](../../) is the 100% open source core database. It is the best choice for the startup organizations with strong technical operations expertise looking to deploy to production with traditional DevOps tools.

[YugabyteDB Anywhere](../../yugabyte-platform/) is commercial software for running a self-managed YugabyteDB-as-a-Service. It has built-in cloud native operations, enterprise-grade deployment options, and world-class support. It is the simplest way to run YugabyteDB in mission-critical production environments with one or more regions (across both public cloud and on-premises data centers).

[YugabyteDB Aeon](../../yugabyte-cloud/) is Yugabyte's fully-managed cloud service on Amazon Web Services (AWS), Microsoft Azure, and Google Cloud Platform (GCP). [Sign up](https://www.yugabyte.com/cloud/) to get started.

For a more detailed comparison between the above, see [Compare Products](https://www.yugabyte.com/compare-products/).

### How do I report a security vulnerability?

Please follow the steps in the [vulnerability disclosure policy](../../secure/vulnerability-disclosure-policy) to report a vulnerability to our security team. The policy outlines our commitments to you when you disclose a potential vulnerability, the reporting process, and how we will respond.

## Evaluating YugabyteDB

### What are the trade-offs involved in using YugabyteDB?

Trade-offs depend on the type of database used as baseline for comparison.

#### Distributed SQL

Examples: Amazon Aurora, Google Cloud Spanner, CockroachDB, TiDB

**Benefits of YugabyteDB**

- Low-latency reads and high-throughput writes.
- Cloud-neutral deployments with a Kubernetes-native database.
- 100% Apache 2.0 open source even for enterprise features.

**Trade-offs**

- None

Learn more: [What is Distributed SQL?](https://www.yugabyte.com/tech/distributed-sql/)

#### Monolithic SQL

Examples: PostgreSQL, MySQL, Oracle, Amazon Aurora.

**Benefits of YugabyteDB**

- Scale write throughput linearly across multiple nodes and/or geographic regions.
- Automatic failover and native repair.
- 100% Apache 2.0 open source even for enterprise features.

**Trade-offs**

- Transactions and JOINs can now span multiple nodes, thereby increasing latency.

Learn more: [Distributed PostgreSQL on a Google Spanner Architecture – Query Layer](https://www.yugabyte.com/blog/distributed-postgresql-on-a-google-spanner-architecture-query-layer/)

#### Traditional NewSQL

Examples: Vitess, Citus

**Benefits of YugabyteDB**

- Distributed transactions across any number of nodes.
- No single point of failure given all nodes are equal.
- 100% Apache 2.0 open source even for enterprise features.

**Trade-offs**

- None

Learn more: [Rise of Globally Distributed SQL Databases – Redefining Transactional Stores for Cloud Native Era](https://www.yugabyte.com/blog/rise-of-globally-distributed-sql-databases-redefining-transactional-stores-for-cloud-native-era/)

#### Transactional NoSQL

Examples: MongoDB, Amazon DynamoDB, FoundationDB, Azure Cosmos DB.

**Benefits of YugabyteDB**

- Flexibility of SQL as query needs change in response to business changes.
- Distributed transactions across any number of nodes.
- Low latency, strongly consistent reads given that read-time quorum is avoided altogether.
- 100% Apache 2.0 open source even for enterprise features.

**Trade-offs**

- None

Learn more: [Why are NoSQL Databases Becoming Transactional?](https://www.yugabyte.com/blog/nosql-databases-becoming-transactional-mongodb-dynamodb-faunadb-cosmosdb/)

#### Eventually Consistent NoSQL

Examples: Apache Cassandra, Couchbase.

**Benefits of YugabyteDB**

- Flexibility of SQL as query needs change in response to business changes.
- Strongly consistent, zero data loss writes.
- Strongly consistent as well as timeline-consistent reads without resorting to eventual consistency-related penalties such as read repairs and anti-entropy.
- 100% Apache 2.0 open source even for enterprise features.

**Trade-offs**

- Extremely short unavailability during the leader election time for all shard leaders lost during a node failure or network partition.

Learn more: [Apache Cassandra: The Truth Behind Tunable Consistency, Lightweight Transactions & Secondary Indexes](https://www.yugabyte.com/blog/apache-cassandra-lightweight-transactions-secondary-indexes-tunable-consistency/)

### When is YugabyteDB a good fit?

YugabyteDB is a good fit for fast-growing, cloud native applications that need to serve business-critical data reliably, with zero data loss, high availability, and low latency. Common use cases include:

- Distributed Online Transaction Processing (OLTP) applications needing multi-region scalability without compromising strong consistency and low latency. For example, user identity, Retail product catalog, Financial data service.

- Hybrid Transactional/Analytical Processing (HTAP), also known as Translytical, applications needing real-time analytics on transactional data. For example, user personalization, fraud detection, machine learning.

- Streaming applications needing to efficiently ingest, analyze, and store ever-growing data. For example, IoT sensor analytics, time series metrics, real-time monitoring.

See some success stories at [yugabyte.com](https://www.yugabyte.com/success-stories/).

### When is YugabyteDB not a good fit?

YugabyteDB is not a good fit for traditional Online Analytical Processing (OLAP) use cases that need complete ad-hoc analytics. Use an OLAP store such as [Druid](http://druid.io/druid.html) or a data warehouse such as [Snowflake](https://www.snowflake.net/).

### Any performance benchmarks available?

[Yahoo Cloud Serving Benchmark (YCSB)](https://github.com/brianfrankcooper/YCSB/wiki) is a popular benchmarking framework for NoSQL databases. We benchmarked the Yugabyte Cloud QL (YCQL) API against standard Apache Cassandra using YCSB. YugabyteDB outperformed Apache Cassandra by increasing margins as the number of keys (data density) increased across all the 6 YCSB workload configurations.

[Netflix Data Benchmark (NDBench)](https://github.com/Netflix/ndbench) is another publicly available, cloud-enabled benchmark tool for data store systems. We ran NDBench against YugabyteDB for 7 days and observed P99 and P995 latencies that were orders of magnitude less than that of Apache Cassandra.

Details for both the above benchmarks are published in [Building a Strongly Consistent Cassandra with Better Performance](https://www.yugabyte.com/blog/building-a-strongly-consistent-cassandra-with-better-performance-aa96b1ab51d6).

### What about correctness testing?

[Jepsen](https://jepsen.io/) is a widely used framework to evaluate the behavior of databases under different failure scenarios. It allows for a database to be run across multiple nodes, and create artificial failure scenarios, as well as verify the correctness of the system under these scenarios. YugabyteDB 1.2 passes [formal Jepsen testing](https://www.yugabyte.com/blog/yugabyte-db-1-2-passes-jepsen-testing/).

### How does YugabyteDB compare to other SQL and NoSQL databases?

See [Compare YugabyteDB to other databases](../comparisons/)

- [Amazon Aurora](../comparisons/amazon-aurora/)
- [Google Cloud Spanner](../comparisons/google-spanner/)
- [MongoDB](../comparisons/mongodb/)
- [CockroachDB](../comparisons/cockroachdb/)

## Architecture

### How does YugabyteDB's common document store work?

[DocDB](../../architecture/docdb/), YugabyteDB's distributed document store is common across all APIs, and built using a custom integration of Raft replication, distributed ACID transactions, and the RocksDB storage engine. Specifically, DocDB enhances RocksDB by transforming it from a key-value store (with only primitive data types) to a document store (with complex data types). **Every key is stored as a separate document in DocDB, irrespective of the API responsible for managing the key.** DocDB's [sharding](../../architecture/docdb-sharding/sharding/), [replication/fault-tolerance](../../architecture/docdb-replication/replication/), and [distributed ACID transactions](../../architecture/transactions/distributed-txns/) architecture are all based on the [Google Spanner design](https://research.google.com/archive/spanner-osdi2012.pdf) first published in 2012. [How We Built a High Performance Document Store on RocksDB?](https://www.yugabyte.com/blog/how-we-built-a-high-performance-document-store-on-rocksdb/) provides an in-depth look into DocDB.

### How can YugabyteDB be both CP and ensure high availability at the same time?

In terms of the [CAP theorem](https://www.yugabyte.com/blog/a-for-apple-b-for-ball-c-for-cap-theorem-8e9b78600e6d), YugabyteDB is a consistent and partition-tolerant (CP) database. It ensures high availability (HA) for most practical situations even while remaining strongly consistent. While this may seem to be a violation of the CAP theorem, that is not the case. CAP treats availability as a binary option whereas YugabyteDB treats availability as a percentage that can be tuned to achieve high write availability (reads are always available as long as a single node is available).

- During network partitions or node failures, the replicas of the impacted tablets (whose leaders got partitioned out or lost) form two groups: a majority partition that can still establish a Raft consensus and a minority partition that cannot establish such a consensus (given the lack of quorum). The replicas in the majority partition elect a new leader among themselves in a matter of seconds and are ready to accept new writes after the leader election completes. For these few seconds till the new leader is elected, the DB is unable to accept new writes given the design choice of prioritizing consistency over availability. All the leader replicas in the minority partition lose their leadership during these few seconds and hence become followers.

- Majority partitions are available for both reads and writes. Minority partitions are not available for writes, but may serve stale reads (up to a staleness as configured by the [--max_stale_read_bound_time_ms](../../reference/configuration/yb-tserver/#max-stale-read-bound-time-ms) flag). **Multi-active availability** refers to YugabyteDB's ability to dynamically adjust to the state of the cluster and serve consistent writes at any replica in the majority partition.

- The approach above obviates the need for any unpredictable background anti-entropy operations as well as need to establish quorum at read time. As shown in the [YCSB benchmarks against Apache Cassandra](https://forum.yugabyte.com/t/ycsb-benchmark-results-for-yugabyte-and-apache-cassandra-again-with-p99-latencies/99), YugabyteDB delivers predictable p99 latencies as well as 3x read throughput that is also timeline-consistent (given no quorum is needed at read time).

On one hand, the YugabyteDB storage and replication architecture is similar to that of [Google Cloud Spanner](https://cloudplatform.googleblog.com/2017/02/inside-Cloud-Spanner-and-the-CAP-Theorem.html), which is also a CP database with high write availability. While Google Cloud Spanner leverages Google's proprietary network infrastructure, YugabyteDB is designed work on commodity infrastructure used by most enterprise users. On the other hand, YugabyteDB's multi-model, multi-API, and tunable read latency approach is similar to that of [Azure Cosmos DB](https://azure.microsoft.com/en-us/blog/a-technical-overview-of-azure-cosmos-db/).

A post on our blog titled [Practical Tradeoffs in Google Cloud Spanner, Azure Cosmos DB and YugabyteDB](https://www.yugabyte.com/blog/practical-tradeoffs-in-google-cloud-spanner-azure-cosmos-db-and-yugabyte-db/) goes through the above tradeoffs in more detail.

### Why is a group of YugabyteDB nodes called a universe instead of the more commonly used term clusters?

A YugabyteDB universe packs a lot more functionality than what people think of when referring to a cluster. In fact, in certain deployment choices, the universe subsumes the equivalent of multiple clusters and some of the operational work needed to run them. Here are just a few concrete differences, which made us feel like giving it a different name would help earmark the differences and avoid confusion:

- A YugabyteDB universe can move into new machines, availability zones (AZs), regions, and data centers in an online fashion, while these primitives are not associated with a traditional cluster.

- You can set up multiple asynchronous replicas with just a few clicks (using YugabyteDB Anywhere). This is built into the universe as a first-class operation with bootstrapping of the remote replica and all the operational aspects of running asynchronous replicas being supported natively. In the case of traditional clusters, the source and the asynchronous replicas are independent clusters. The user is responsible for maintaining these separate clusters as well as operating the replication logic.

- Failover to asynchronous replicas as the primary data and fallback once the original is up and running are both natively supported in a universe.

### Why is consistent hash sharding the default sharding strategy?

Users primarily turn to YugabyteDB for scalability reasons. Consistent hash sharding is ideal for massively scalable workloads because it distributes data evenly across all the nodes in the cluster, while retaining ease of adding nodes into the cluster. Most use cases that require scalability do not need to perform range lookups on the primary key, so consistent hash sharding is the default sharding strategy for YugabyteDB. Common applications that do not need hash sharding include user identity (user IDs do not need ordering), product catalog (product IDs are not related to one another), and stock ticker data (one stock symbol is independent of all other stock symbols). For applications that benefit from range sharding, YugabyteDB lets you select that option.

To learn more about sharding strategies and lessons learned, see [Four Data Sharding Strategies We Analyzed in Building a Distributed SQL Database](https://www.yugabyte.com/blog/four-data-sharding-strategies-we-analyzed-in-building-a-distributed-sql-database/).
