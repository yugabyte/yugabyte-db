<img src="https://cloud.yugabyte.com/logo-big.png" align="center" alt="YugabyteDB" width="50%"/>
<img referrerpolicy="no-referrer-when-downgrade" src="https://static.scarf.sh/a.png?x-pxid=0969fc8d-7684-4250-9cbd-4249c3ebb47b" />

---------------------------------------

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Documentation Status](https://readthedocs.org/projects/ansicolortags/badge/?version=latest)](https://docs.yugabyte.com/)
[![Ask in forum](https://img.shields.io/badge/ask%20us-forum-orange.svg)](https://forum.yugabyte.com/)
[![Slack chat](https://img.shields.io/badge/Slack:-%23yugabyte_db-blueviolet.svg?logo=slack)](https://communityinviter.com/apps/yugabyte-db/register)
[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/home?pixel&useReferer)](https://github.com/yugabyte/ga-beacon)

# What is YugabyteDB?

YugabyteDB is a PostgreSQL-compatible, [high-performance](https://docs.yugabyte.com/preview/benchmark/), cloud-native, [distributed SQL](https://www.yugabyte.com/tech/distributed-sql/) database. It combines the benefits of traditional relational databases with the scalability of NoSQL systems, making it suitable for applications that require both transactional consistency and the ability to handle large amounts of data. It is best suited for cloud-native OLTP (that is, real-time, business-critical) applications that need absolute data correctness and require at least one of the following: scalability, high tolerance to failures, or globally-distributed deployments.

* [Core Features](#core-features)
* [Get Started](#get-started)
* [Build Apps](#build-apps)
* [Current Roadmap](#current-roadmap)
* [Recent features](#recently-released-features)
* [Architecture](#architecture)
* [Need Help?](#need-help)
* [Contribute](#contribute)
* [License](#license)
* [Read More](#read-more)

# Core Features

* **[Powerful RDBMS capabilities](https://docs.yugabyte.com/preview/explore/ysql-language-features/)** Yugabyte SQL (*YSQL* for short) reuses the PostgreSQL query layer (similar to Amazon Aurora PostgreSQL), thereby supporting most of its features (datatypes, queries, expressions, operators and functions, stored procedures, triggers, extensions, and so on).

* **[Distributed transactions](https://docs.yugabyte.com/preview/architecture/transactions/)** The transaction design is based on the Google Spanner architecture. Strong consistency of writes is achieved by using Raft consensus for replication and cluster-wide distributed ACID transactions using *hybrid logical clocks*. *Snapshot*, *serializable* and *read committed* isolation levels are supported. Reads (queries) have strong consistency by default, but can be tuned dynamically to read from followers and read replicas.

* **[Continuous availability](https://docs.yugabyte.com/preview/explore/fault-tolerance/)** YugabyteDB is extremely resilient to common outages with native failover and repair. YugabyteDB can be configured to tolerate disk, rack, node, zone, region, and cloud failures automatically. For a typical deployment where a YugabyteDB cluster is deployed in one region across multiple zones on a public cloud, the RPO is 0 (meaning no data is lost on failure) and the RTO is 3 seconds (meaning the data being served by the failed node is available in 3 seconds).

* **[Horizontal scalability](https://docs.yugabyte.com/preview/explore/linear-scalability/)** Scaling a YugabyteDB cluster to achieve more IOPS or data storage is as simple as adding nodes to the cluster.

* **[Geo-distributed, multi-cloud](https://docs.yugabyte.com/preview/develop/multi-cloud/)** YugabyteDB can be deployed in public clouds and natively inside Kubernetes. It supports deployments that span three or more fault domains, such as multi-zone, multi-rack, multi-region, and multi-cloud deployments. It also supports xCluster asynchronous replication with unidirectional master-slave and bidirectional multi-master configurations in two-region deployments. Read replicas are also a supported to serve (stale) data with low latencies.

* **[Multi API design](https://docs.yugabyte.com/preview/api)** The YugabyteDB query layer is built to be extensible. Currently, YugabyteDB supports two distributed SQL APIs: [Yugabyte SQL (YSQL)](https://docs.yugabyte.com/preview/api/ysql/), a fully relational API that re-uses the PostgreSQL query layer, and [Yugabyte Cloud QL (YCQL)](https://docs.yugabyte.com/preview/api/ycql/), a semi-relational SQL-like API with documents/indexing support with Apache Cassandra QL roots.

* **[100% open source](https://github.com/yugabyte/yugabyte-db)** YugabyteDB is fully open-source under the [Apache 2.0 license](https://github.com/yugabyte/yugabyte-db/blob/master/LICENSE.md). The open-source version has powerful enterprise features such as distributed backups, encryption of data at rest, in-flight TLS encryption, change data capture, read replicas, and more.

YugabyteDB was created with several key design goals in mind, aiming to address the challenges faced by modern, cloud-native applications while maintaining the familiarity and power of traditional relational databases. Read more about these in our [Design goals](https://docs.yugabyte.com/preview/architecture/design-goals/).

# Get Started

* [Quick Start](https://docs.yugabyte.com/preview/quick-start/)
* Try running a real-world demo application:
  * [Microservices-oriented e-commerce app](https://github.com/yugabyte/yugastore-java)
  * [Streaming IoT app with Kafka and Spark Streaming](https://docs.yugabyte.com/preview/develop/realworld-apps/iot-spark-kafka-ksql/)

Can't find what you're looking for? Have a question? Post your questions or comments on our Community [Slack](https://communityinviter.com/apps/yugabyte-db/register) or [Forum](https://forum.yugabyte.com).

# Build Applications

YugabyteDB supports many languages and client drivers, including Java, Go, NodeJS, Python, and more. For a complete list, including examples, see [Drivers and ORMs](https://docs.yugabyte.com/preview/drivers-orms/).

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

## v2.25 (Preview) - Jan, 2025

**v2.25** is the current [Preview](https://docs.yugabyte.com/preview/releases/versioning/#preview-releases) release. This includes features under active development and is recommended for development and testing only. For the full list of features and improvements in this release, see [Release notes - v2.25](https://docs.yugabyte.com/preview/releases/ybdb-releases/v2.25/). Here are some of the prominent features.

### [PostgreSQL 15 Support](https://docs.yugabyte.com/preview/develop/pg15-features/)

As part of this release, we have upgraded our PostgreSQL fork from version 11.2 to 15.0, enabling you to leverage the many key capabilities introduced in PostgreSQL between these two versions. This upgrade brings YSQL API support for numerous features, including stored generated columns, foreign keys on partitioned tables, and non-distinct NULLs in unique indexes. It also introduces query execution optimizations like incremental sort and memoization, along with various observability and security enhancements.

### [Query Diagnostics](https://docs.yugabyte.com/preview/explore/query-1-performance/query-diagnostics/)

This feature significantly simplifies tuning poorly performing SQL queries by allowing you to capture and export detailed diagnostic information, including bind variables and constants, pg_stat_statements statistics, schema details, active session history, and execution plans.

### [Active session history](https://docs.yugabyte.com/preview/explore/observability/active-session-history/)

In addition, the Active Session History, which provides real-time and historical views of system activity, is now enabled by default.

## v2024.2 (Stable) - Dec, 2024

**v2024.2** is the current [stable](https://docs.yugabyte.com/preview/releases/versioning/#stable-releases) release. Stable releases undergo rigorous testing for a longer period of time and are ready for production use. For the full list of features and improvements in this release, see [Release notes - v2024.2](https://docs.yugabyte.com/preview/releases/ybdb-releases/v2024.2/). Here are some of the prominent features.

#### [Yugabyte Kubernetes Operator](https://docs.yugabyte.com/preview/quick-start/kubernetes/#yugabytedb-kubernetes-operator)

The [Yugabyte Kubernetes Operator](https://docs.yugabyte.com/preview/quick-start/kubernetes/#yugabytedb-kubernetes-operator) is a powerful tool designed to automate deploying, scaling, and managing YugabyteDB clusters in Kubernetes environments. It streamlines database operations, reducing manual effort for developers and operators. For more information, refer to the [YugabyteDB Kubernetes Operator](https://github.com/yugabyte/yugabyte-k8s-operator) GitHub project.

#### [Active session history](https://docs.yugabyte.com/stable/explore/observability/active-session-history/)

Get real-time and historical views of system activity by sampling session activity in the database. Use this feature to analyze and troubleshoot performance issues.

#### [pg_partman extension](https://docs.yugabyte.com/stable/explore/ysql-language-features/pg-extensions/extension-pgpartman/)

Use the [pg_partman extension](https://docs.yugabyte.com/stable/explore/ysql-language-features/pg-extensions/extension-pgpartman/) to create and manage both time- and serial-based (aka range-based) table partition sets. pg_partman is often used in combination with [pg_cron](https://docs.yugabyte.com/stable/explore/ysql-language-features/pg-extensions/extension-pgcron/) for data lifecycle management, and specifically for managing data aging, retention, and expiration.

#### [Colocated tables with tablespaces](https://docs.yugabyte.com/stable/explore/colocation/#colocated-tables-with-tablespaces)

Starting this release, you can create [colocated tables with tablespaces](https://docs.yugabyte.com/stable/explore/colocation/#colocated-tables-with-tablespaces). With this enhancement, you can now take advantage of colocated tables for geo-distributed use cases, eliminating the need for trade-offs between distributing data across specific regions.

# Architecture

<img src="https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/architecture/images/yb-architecture.jpg" align="center" alt="YugabyteDB Architecture"/>

Review detailed architecture in our [Docs](https://docs.yugabyte.com/preview/architecture/).

# Need Help?

* You can ask questions, find answers, and help others on our Community [Slack](https://communityinviter.com/apps/yugabyte-db/register), [Forum](https://forum.yugabyte.com), [Stack Overflow](https://stackoverflow.com/questions/tagged/yugabyte-db), as well as Twitter [@Yugabyte](https://twitter.com/yugabyte).

* Use [GitHub issues](https://github.com/yugabyte/yugabyte-db/issues) to report issues or request new features.

* To troubleshoot YugabyteDB and cluster/node-level issues, refer to [Troubleshooting documentation](https://docs.yugabyte.com/preview/troubleshoot/).

# Contribute

As an open-source project with a strong focus on the user community, we welcome contributions as GitHub pull requests. See our [Contributor Guides](https://docs.yugabyte.com/preview/contribute/) to get going. Discussions and RFCs for features happen on the design discussions section of our [Forum](https://forum.yugabyte.com/c/design-discussions/7).

# License

Source code in this repository is variously licensed under the Apache License 2.0 and the Polyform Free Trial License 1.0.0. A copy of each license can be found in the [licenses](licenses) directory.

The build produces two sets of binaries:

* The entire database with all its features (including the enterprise ones) is licensed under the Apache License 2.0
* The binaries that contain `-managed` in the artifact and help run a managed service are licensed under the Polyform Free Trial License 1.0.0.

> By default, the build options generate only the Apache License 2.0 binaries.

# Read More

* To see our updates, go to the [Distributed SQL Blog](https://blog.yugabyte.com/).
* For in-depth design and architecture details, see our [design specs](https://github.com/yugabyte/yugabyte-db/tree/master/architecture/design).
* [Tech Talks](https://www.yugabyte.com/yftt/) and [Videos](https://www.youtube.com/c/YugaByte).
* See how YugabyteDB [compares with other databases](https://docs.yugabyte.com/preview/faq/comparisons/).
