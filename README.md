<img src="https://cloud.yugabyte.com/logo-big.png" align="center" alt="YugabyteDB" width="50%"/>
<img referrerpolicy="no-referrer-when-downgrade" src="https://static.scarf.sh/a.png?x-pxid=0969fc8d-7684-4250-9cbd-4249c3ebb47b" />

---------------------------------------

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Documentation Status](https://readthedocs.org/projects/ansicolortags/badge/?version=latest)](https://docs.yugabyte.com/)
[![Ask in forum](https://img.shields.io/badge/ask%20us-forum-orange.svg)](https://forum.yugabyte.com/)
[![Slack chat](https://img.shields.io/badge/Slack:-%23yugabyte_db-blueviolet.svg?logo=slack)](https://communityinviter.com/apps/yugabyte-db/register)
[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/home?pixel&useReferer)](https://github.com/yugabyte/ga-beacon)

# What is YugabyteDB?

YugabyteDB is a PostgreSQL-compatible, [high-performance](https://docs.yugabyte.com/stable/benchmark/), cloud-native, [distributed SQL](https://www.yugabyte.com/tech/distributed-sql/) database. It combines the benefits of traditional relational databases with the scalability of NoSQL systems, making it suitable for applications that require both transactional consistency and the ability to handle large amounts of data. It is best suited for cloud-native OLTP (that is, real-time, business-critical) applications that need absolute data correctness and require at least one of the following: scalability, high tolerance to failures, or globally-distributed deployments.

* [Core Features](#core-features)
* [Get Started](#get-started)
* [Build Applications](#build-applications)
* [Current Roadmap](#current-roadmap)
* [Recent features](#recently-released-features)
* [Architecture](#architecture)
* [Need Help?](#need-help)
* [Contribute](#contribute)
* [License](#license)
* [Read More](#read-more)

# Core Features

* **[Powerful RDBMS capabilities](https://docs.yugabyte.com/stable/explore/ysql-language-features/)** Yugabyte SQL (*YSQL* for short) reuses the PostgreSQL query layer (similar to Amazon Aurora PostgreSQL), thereby supporting most of its features (datatypes, queries, expressions, operators and functions, stored procedures, triggers, extensions, and so on).

* **[Distributed transactions](https://docs.yugabyte.com/stable/architecture/transactions/)** The transaction design is based on the Google Spanner architecture. Strong consistency of writes is achieved by using Raft consensus for replication and cluster-wide distributed ACID transactions using *hybrid logical clocks*. *Snapshot*, *serializable* and *read committed* isolation levels are supported. Reads (queries) have strong consistency by default, but can be tuned dynamically to read from followers and read replicas.

* **[Continuous availability](https://docs.yugabyte.com/stable/explore/fault-tolerance/)** YugabyteDB is extremely resilient to common outages with native failover and repair. YugabyteDB can be configured to tolerate disk, rack, node, zone, region, and cloud failures automatically. For a typical deployment where a YugabyteDB cluster is deployed in one region across multiple zones on a public cloud, the RPO is 0 (meaning no data is lost on failure) and the RTO is 3 seconds (meaning the data being served by the failed node is available in 3 seconds).

* **[Horizontal scalability](https://docs.yugabyte.com/stable/explore/linear-scalability/)** Scaling a YugabyteDB cluster to achieve more IOPS or data storage is as simple as adding nodes to the cluster.

* **[Geo-distributed, multi-cloud](https://docs.yugabyte.com/stable/develop/multi-cloud/)** YugabyteDB can be deployed in public clouds and natively inside Kubernetes. It supports deployments that span three or more fault domains, such as multi-zone, multi-rack, multi-region, and multi-cloud deployments. It also supports xCluster asynchronous replication with unidirectional master-slave and bidirectional multi-master configurations in two-region deployments. Read replicas are also a supported to serve (stale) data with low latencies.

* **[Multi API design](https://docs.yugabyte.com/stable/api)** The YugabyteDB query layer is built to be extensible. Currently, YugabyteDB supports two distributed SQL APIs: [Yugabyte SQL (YSQL)](https://docs.yugabyte.com/stable/api/ysql/), a fully relational API that re-uses the PostgreSQL query layer, and [Yugabyte Cloud QL (YCQL)](https://docs.yugabyte.com/stable/api/ycql/), a semi-relational SQL-like API with documents/indexing support with Apache Cassandra QL roots.

* **[100% open source](https://github.com/yugabyte/yugabyte-db)** YugabyteDB is fully open-source under the [Apache 2.0 license](https://github.com/yugabyte/yugabyte-db/blob/master/LICENSE.md). The open-source version has powerful enterprise features such as distributed backups, encryption of data at rest, in-flight TLS encryption, change data capture, read replicas, and more.

YugabyteDB was created with several key design goals in mind, aiming to address the challenges faced by modern, cloud-native applications while maintaining the familiarity and power of traditional relational databases. Read more about these in our [Design goals](https://docs.yugabyte.com/stable/architecture/design-goals/).

# Get Started

* [Quick Start](https://docs.yugabyte.com/stable/quick-start/)
* Try running a real-world demo application:
  * [Microservices-oriented e-commerce app](https://github.com/yugabyte/yugastore-java)
  * [Lodging Recommendation Service With OpenAI and pgvector](https://github.com/YugabyteDB-Samples/openai-pgvector-lodging-service/)

Can't find what you're looking for? Have a question? Post your questions or comments on our Community [Slack](https://communityinviter.com/apps/yugabyte-db/register) or [Forum](https://forum.yugabyte.com).

# Build Applications

YugabyteDB supports many languages and client drivers, including Java, Go, NodeJS, Python, and more. For a complete list, including examples, see [Drivers and ORMs](https://docs.yugabyte.com/stable/develop/drivers-orms/).

# Current Roadmap

The following is a list of some of the key features being worked on for upcoming releases.

|                                                  Feature                                                   |                                                           Details                                                           |
| ---------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| [PostgreSQL 15 Compatibility](https://github.com/yugabyte/yugabyte-db/issues/9797)                         | For latest features, new PostgreSQL extensions, performance, and community fixes.                                            |
| [PostgreSQL Publication/Replication slot API in CDC](https://github.com/yugabyte/yugabyte-db/issues/18724) | PostgreSQL has a huge community that needs a PG-compatible API to set up and consume database changes.                      |
| [Bitmap scan](https://github.com/yugabyte/yugabyte-db/issues/22653)                                        | Bitmap Scan support for using Index Scans, remote filter and enhanced Cost Model.                                            |
| [Cost based optimizer(CBO)](https://github.com/yugabyte/yugabyte-db/issues/10177)                          | Efficient query plans based on statistics (such as table size, number of rows) and data distribution.                       |
| [Parallel query execution](https://github.com/yugabyte/yugabyte-db/issues/17984)                           | Higher query performance by splitting a single query for execution across different CPU cores.          |
| [pgvector extension](https://github.com/yugabyte/yugabyte-db/issues/16166)                                 | Support for vector data types, enabling efficient storage and querying of high-dimensional vectors.                         |
| [Connection Management](https://github.com/yugabyte/yugabyte-db/issues/17599)                              | Server side connection management enabling upto 30K connections per node                                                    |

Refer to [roadmap tracker](https://github.com/yugabyte/yugabyte-db/issues?q=is:issue+is:open+label:current-roadmap) for the list of all items in the current roadmap.

# Recently released features

## v2025.2 (Stable) - December 2025

**v2025.2** is the current [stable](https://docs.yugabyte.com/stable/releases/versioning/#stable-releases) release. Stable releases undergo rigorous testing for a longer period of time and are ready for production use. For the full list of features and improvements in this release, see [Release notes - v2025.2](https://docs.yugabyte.com/stable/releases/ybdb-releases/v2025.2/). Here are some of the prominent features.

### PostgreSQL features enabled by default on new universes**

For new universes running v2025.2 or later, the following YSQL features are now enabled by default when you deploy using [yugabyted](https://docs.yugabyte.com/stable/deploy/manual-deployment/start-yugabyted/), [YugabyteDB Anywhere](https://docs.yugabyte.com/stable/yugabyte-platform/create-deployments/create-universe-multi-zone/), or [YugabyteDB Aeon](https://docs.yugabyte.com/stable/yugabyte-cloud/cloud-basics/create-clusters/) (coming soon to the Early Access track):

* [Read committed](https://docs.yugabyte.com/stable/architecture/transactions/read-committed/)
* [Cost-based optimizer](https://docs.yugabyte.com/stable/best-practices-operations/ysql-yb-enable-cbo/)
* [Auto Analyze](https://docs.yugabyte.com/stable/additional-features/auto-analyze/)
* [YugabyteDB bitmap scan](https://docs.yugabyte.com/stable/reference/configuration/postgresql-compatibility/#yugabytedb-bitmap-scan)
* [Parallel append](https://docs.yugabyte.com/stable/additional-features/parallel-query/)

In addition, if you upgrade to v2025.2 and the universe already has cost-based optimizer enabled, the following features are enabled by default:

* Auto Analyze
* YugabyteDB bitmap scan
* Parallel append

Note that, apart from the exceptions noted, upgrading existing universes does not change the defaults for any of these features.

For more information on PostgreSQL features developed in YugabyteDB for enhanced compatibility, refer to [Enhanced PostgreSQL Compatibility Mode](https://docs.yugabyte.com/stable/reference/configuration/postgresql-compatibility/).

### [Table-level locking for concurrent DDL and DML](https://docs.yugabyte.com/stable/stable/explore/transactions/explicit-locking/#table-level-locks)

Added Table-level locks to support concurrent DDL and DML operations across sessions, improving workload concurrency and reducing conflicts during schema changes.

### [Improved time synchronization across nodes](https://docs.yugabyte.com/stable/deploy/manual-deployment/system-config/#configure-clockbound)

Time synchronization across nodes has been enhanced through the use of the [ClockBound](https://github.com/aws/clock-bound) library, which is an open source daemon that allows you to compare timestamps to determine order for events and transactions, independent of an instance's geographic location; it improves clock accuracy by several orders of magnitude.

## v2025.1 (Stable) - July 2025

For the full list of features and improvements in this release, see [Release notes - v2025.1](https://docs.yugabyte.com/stable/releases/ybdb-releases/v2025.1/). Here are some of the prominent features.

### [PostgreSQL 15 compatible YugabyteDB clusters](https://docs.yugabyte.com/stable/api/ysql/pg15-features/)

This is the first stable release featuring a PostgreSQL fork rebase from version 11.2 to 15.0, enabling you to leverage the many key capabilities introduced in PostgreSQL between the two versions. This upgrade brings YSQL API support for numerous features, including stored generated columns, foreign keys on partitioned tables, and non-distinct NULLs in unique indexes. It also introduces query execution optimizations like incremental sort and memoization, along with various observability and security enhancements.

We're also pleased to announce that YugabyteDB 2025.1.0.0 supports in-place online upgrades and downgrade—even with the PostgreSQL fork rebased to 15.0.

**Note** that the source cluster must be running version 2024.2.3.0 or later to upgrade to version 2025.1.0.

### [HNSW indexing support for pgvector](https://docs.yugabyte.com/stable/additional-features/pg-extensions/extension-pgvector/#vector-indexing)

Brings AI-native capability by enabling efficient similarity search in vector workloads. Enhanced vector search capabilities via Hierarchical Navigable Small World (HNSW) indexing provide faster and more efficient high-dimensional vector lookups.

### [Automatic transactional xCluster DDL replication](https://docs.yugabyte.com/stable/deploy/multi-dc/async-replication/async-transactional-setup-automatic/#set-up-automatic-mode-replication)

YugabyteDB now supports seamless replication of YSQL DDL changes across xCluster setups, eliminating the need to manually apply DDLs on both source and target clusters.

### [Parallel queries: Enabling PG parallelism for colocated tables](https://docs.yugabyte.com/stable/additional-features/parallel-query/)

Improves query performance for colocated tables by allowing PostgreSQL to leverage multiple CPUs, leading to faster query execution times.

### [Optimization of INSERT ON CONFLICT batching](https://docs.yugabyte.com/stable/reference/configuration/yb-tserver/#yb-insert-on-conflict-read-batch-size)

Queries using the `INSERT ... ON CONFLICT` clause are optimized for efficient execution, with automatic batching applied when multiple statements are executed to improve performance.

### [Cost-Based Optimizer (CBO)](https://docs.yugabyte.com/stable/best-practices-operations/ysql-yb-enable-cbo/)

The CBO leverages YugabyteDB's distributed storage architecture and advanced query execution optimizations, including query pushdowns, LSM indexes, and batched nested loop joins, to deliver PostgreSQL-like performance.

### [Bitmap scan support](https://docs.yugabyte.com/stable/reference/configuration/yb-tserver/#enable-bitmapscan)

Combine multiple indexes for more efficient scans.

## v2.25 (Preview) - January 2025

**v2.25** is the most recent [Preview](https://docs.yugabyte.com/stable/releases/versioning/#preview-releases) release. This includes features under active development and is recommended for development and testing only. For the full list of features and improvements in this release, see [Release notes - v2.25](https://docs.yugabyte.com/stable/releases/ybdb-releases/v2.25/). Here are some of the prominent features.

### [PostgreSQL 15 Support](https://docs.yugabyte.com/stable/develop/pg15-features/)

As part of this release, we have upgraded our PostgreSQL fork from version 11.2 to 15.0, enabling you to leverage the many key capabilities introduced in PostgreSQL between these two versions. This upgrade brings YSQL API support for numerous features, including stored generated columns, foreign keys on partitioned tables, and non-distinct NULLs in unique indexes. It also introduces query execution optimizations like incremental sort and memoization, along with various observability and security enhancements.

### [Query Diagnostics](https://docs.yugabyte.com/stable/explore/query-1-performance/query-diagnostics/)

This feature significantly simplifies tuning poorly performing SQL queries by allowing you to capture and export detailed diagnostic information, including bind variables and constants, pg_stat_statements statistics, schema details, active session history, and execution plans.

### [Active session history](https://docs.yugabyte.com/stable/explore/observability/active-session-history/)

In addition, the Active Session History, which provides real-time and historical views of system activity, is now enabled by default.

# Architecture

<img src="https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/architecture/images/yb-architecture.jpg" align="center" alt="YugabyteDB Architecture"/>

Review detailed architecture in our [Docs](https://docs.yugabyte.com/stable/architecture/).

# Need Help?

* You can ask questions, find answers, and help others on our Community [Slack](https://communityinviter.com/apps/yugabyte-db/register), [Forum](https://forum.yugabyte.com), [Stack Overflow](https://stackoverflow.com/questions/tagged/yugabyte-db), as well as Twitter [@Yugabyte](https://twitter.com/yugabyte).

* Use [GitHub issues](https://github.com/yugabyte/yugabyte-db/issues) to report issues or request new features.

* To troubleshoot YugabyteDB and cluster/node-level issues, refer to [Troubleshooting documentation](https://docs.yugabyte.com/stable/troubleshoot/).

# Contribute

As an open-source project with a strong focus on the user community, we welcome contributions as GitHub pull requests. See our [Contributor Guides](https://docs.yugabyte.com/stable/contribute/) to get going. Discussions and RFCs for features happen on the design discussions section of our [Forum](https://forum.yugabyte.com/c/design-discussions/7).

For AI agents, refer to [AGENTS.md](AGENTS.md) for guidance on working with this codebase.

# License

This repository contains two differently licensed components. See [LICENSE.md](LICENSE.md) for detailed directory mappings.

* **YugabyteDB** (core database in `src/`, `java/`, etc.) - [Apache License 2.0](licenses/APACHE-LICENSE-2.0.txt)
* **YugabyteDB Anywhere** (management platform in `managed/`) - [Polyform Free Trial License 1.0.0](licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt)

> By default, the build generates only the Apache 2.0 licensed database binaries.

# Read More

* To see our updates, go to the [Distributed SQL Blog](https://blog.yugabyte.com/).
* For in-depth design and architecture details, see our [design specs](https://github.com/yugabyte/yugabyte-db/tree/master/architecture/design).
* [Tech Talks](https://www.yugabyte.com/yftt/) and [Videos](https://www.youtube.com/c/YugaByte).
* See how YugabyteDB [compares with other databases](https://docs.yugabyte.com/stable/faq/comparisons/).
