<img src="https://cloud.yugabyte.com/logo-big.png" align="center" alt="YugabyteDB" width="50%"/>

---------------------------------------

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Documentation Status](https://readthedocs.org/projects/ansicolortags/badge/?version=latest)](https://docs.yugabyte.com/)
[![Ask in forum](https://img.shields.io/badge/ask%20us-forum-orange.svg)](https://forum.yugabyte.com/)
[![Slack chat](https://img.shields.io/badge/Slack:-%23yugabyte_db-blueviolet.svg?logo=slack)](https://communityinviter.com/apps/yugabyte-db/register)
[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/home?pixel&useReferer)](https://github.com/yugabyte/ga-beacon)

# What is YugabyteDB? 

**YugabyteDB** is a **high-performance, cloud-native, [distributed SQL](https://www.yugabyte.com/tech/distributed-sql/) database** that aims to support **all PostgreSQL features**. It is best suited for **cloud-native OLTP (i.e., real-time, business-critical) applications** that need absolute **data correctness** and require at least one of the following: **scalability, high tolerance to failures, or globally-distributed deployments.**

* [Core Features](#core-features)
* [Get Started](#get-started)
* [Build Apps](#build-apps)
* [What's being worked on?](#whats-being-worked-on)
* [Architecture](#architecture)
* [Need Help?](#need-help)
* [Contribute](#contribute)
* [License](#license)
* [Read More](#read-more)

# Core Features

* **Powerful RDBMS capabilities** Yugabyte SQL (*YSQL* for short) reuses the query layer of PostgreSQL (similar to Amazon Aurora PostgreSQL), thereby supporting most of its features (datatypes, queries, expressions, operators and functions, stored procedures, triggers, extensions, etc). Here is a detailed [list of features currently supported by YSQL](https://docs.yugabyte.com/preview/explore/ysql-language-features/postgresql-compatibility/).

* **Distributed transactions** The transaction design is based on the Google Spanner architecture. Strong consistency of writes is achieved by using Raft consensus for replication and cluster-wide distributed ACID transactions using *hybrid logical clocks*. *Snapshot*, *serializable* and *read committed* isolation levels are supported. Reads (queries) have strong consistency by default, but can be tuned dynamically to read from followers and read-replicas.

* **Continuous availability** YugabyteDB is extremely resilient to common outages with native failover and repair. YugabyteDB can be configured to tolerate disk, node, zone, region, and cloud failures automatically. For a typical deployment where a YugabyteDB cluster is deployed in one region across multiple zones on a public cloud, the RPO is 0 (meaning no data is lost on failure) and the RTO is 3 seconds (meaning the data being served by the failed node is available in 3 seconds).

* **Horizontal scalability** Scaling a YugabyteDB cluster to achieve more IOPS or data storage is as simple as adding nodes to the cluster.

* **Geo-distributed, multi-cloud** YugabyteDB can be deployed in public clouds and natively inside Kubernetes. It supports deployments that span three or more fault domains, such as multi-zone, multi-region, and multi-cloud deployments. It also supports xCluster asynchronous replication with unidirectional master-slave and bidirectional multi-master configurations that can be leveraged in two-region deployments. To serve (stale) data with low latencies, read replicas are also a supported feature.

* **Multi API design** The query layer of YugabyteDB is built to be extensible. Currently, YugabyteDB supports two distributed SQL APIs: **[Yugabyte SQL (YSQL)](https://docs.yugabyte.com/preview/api/ysql/)**, a fully relational API that re-uses query layer of PostgreSQL, and **[Yugabyte Cloud QL (YCQL)](https://docs.yugabyte.com/preview/api/ycql/)**, a semi-relational SQL-like API with documents/indexing support with Apache Cassandra QL roots.

* **100% open source** YugabyteDB is fully open-source under the [Apache 2.0 license](https://github.com/yugabyte/yugabyte-db/blob/master/LICENSE.md). The open-source version has powerful enterprise features such as distributed backups, encryption of data-at-rest, in-flight TLS encryption, change data capture, read replicas, and more.

Read more about YugabyteDB in our [FAQ](https://docs.yugabyte.com/preview/faq/general/).

# Get Started

* [Quick Start](https://docs.yugabyte.com/preview/quick-start/)
* Try running a real-world demo application:
  * [Microservices-oriented e-commerce app](https://github.com/yugabyte/yugastore-java)
  * [Streaming IoT app with Kafka and Spark Streaming](https://docs.yugabyte.com/preview/develop/realworld-apps/iot-spark-kafka-ksql/)

Cannot find what you are looking for? Have a question? Please post your questions or comments on our Community [Slack](https://communityinviter.com/apps/yugabyte-db/register) or [Forum](https://forum.yugabyte.com).

# Build Apps

YugabyteDB supports many languages and client drivers, including Java, Go, NodeJS, Python, and more. For a complete list, including examples, see [Drivers and ORMs](https://docs.yugabyte.com/preview/drivers-orms/).

# What's being worked on?

> This section was last updated in **July, 2024**.

## Current roadmap

Here is a list of some of the key features being worked on for the upcoming releases. The YugabyteDB [**v2024.1 release**](https://docs.yugabyte.com/preview/releases/ybdb-releases/) has been released in **June, 2024**.

| Feature                                         | Status    |  Progress        |  Comments     |
| ----------------------------------------------- | --------- |  --------------- | ------------- |
|[Upgrade to PostgreSQL v15](https://github.com/yugabyte/yugabyte-db/issues/9797)| PROGRESS| [Track](https://github.com/yugabyte/yugabyte-db/issues/9797)| For latest features, new PostgreSQL extensions, performance, and community fixes
|[Support PostgreSQL Publication/Replication slot API in CDC](https://github.com/yugabyte/yugabyte-db/issues/18724) | PROGRESS|  [Track](https://github.com/yugabyte/yugabyte-db/issues/18724)| PostgreSQL has a huge community that needs a PG-compatible API to set up and consume database changes.
|[Bitmap scan support](https://github.com/yugabyte/yugabyte-db/issues/22653)| PROGRESS| [Track](https://github.com/yugabyte/yugabyte-db/issues/22653)| Bitmap Scan support for using Index Scans, remote filter and enhance Cost Model.
| YSQL-table statistics and cost based optimizer(CBO) | PROGRESS  |   [Track](https://github.com/yugabyte/yugabyte-db/issues/5242) | Improve YSQL query performance |
|[YSQL parallel query execution](https://github.com/yugabyte/yugabyte-db/issues/17984)|PROGRESS | [Track](https://github.com/yugabyte/yugabyte-db/issues/17984)| Devise query plans that can leverage multiple CPUs in order to answer queries faster.
| [YSQL-Feature support - ALTER TABLE](https://github.com/yugabyte/yugabyte-db/issues/1124) | PROGRESS |  [Track](https://github.com/yugabyte/yugabyte-db/issues/1124) | Support for various `ALTER TABLE` variants |
| Connection Management | PROGRESS  |  [Track](https://github.com/yugabyte/yugabyte-db/issues/17599) |Server side connection management|

## Recently released features

| Feature                                         | Status    | Release Target | Docs / Enhancements |  Comments     |
| ----------------------------------------------- | --------- | -------------- | ------------------- | ------------- |
| Support for transactions in async [xCluster replication](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/multi-region-xcluster-async-replication.md) |  ✅ *DONE*  |  v2.19  | [Docs](https://docs.yugabyte.com/preview/develop/build-global-apps/active-active-single-master/#transactional-consistency) | Preserve and guarantee transactional atomicity and global ordering when propagating change data from one universe to another |
| Support wait-on-conflict concurrency control |  ✅ *DONE*  | v2.19  |  | Support wait-on-conflict concurrency control |
|[Faster Bulk-Data Loading in YugabyteDB](https://github.com/yugabyte/yugabyte-db/issues/11765)|  ✅ *DONE*| v2.15 |[Track](https://github.com/yugabyte/yugabyte-db/issues/11765)| Faster Bulk-Data Loading in YugabyteDB|
|[Change Data Capture](https://github.com/yugabyte/yugabyte-db/issues/9019)|  ✅ *DONE*| v2.13 ||Change data capture (CDC) allows multiple downstream apps and services to consume the continuous and never-ending stream(s) of changes to Yugabyte databases|
|[Support for materalized views](https://github.com/yugabyte/yugabyte-db/issues/10102) |  ✅ *DONE*| v2.13 |[Docs](https://docs.yugabyte.com/preview/explore/ysql-language-features/advanced-features/views/#materialized-views)|A materialized view is a pre-computed data set derived from a query specification and stored for later use|
|[Geo-partitioning support](https://github.com/yugabyte/yugabyte-db/issues/9980) for the transaction status table | ✅ *DONE*| v2.13 |[Docs](https://docs.yugabyte.com/preview/explore/multi-region-deployments/row-level-geo-partitioning/)|Instead of central remote transaction execution metatda, it is now optimized for access from different regions. Since the transaction metadata is also geo partitioned, it eliminates the need for round-trip to remote regions to update transaction statuses.|
| Transparently restart transactions |  ✅ *DONE*| v2.13 | |Decrease the incidence of transaction restart errors seen in various scenarios |
| [Row-level geo-partitioning](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/ysql-row-level-partitioning.md) |  ✅ *DONE*| v2.13 |[Docs](https://docs.yugabyte.com/preview/explore/multi-region-deployments/row-level-geo-partitioning/)|Row-level geo-partitioning allows fine-grained control over pinning data in a user table (at a per-row level) to geographic locations, thereby allowing the data residency to be managed at the table-row level.|
| [YSQL-Support `GIN` indexes](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/ysql-gin-indexes.md) |  ✅ *DONE*  | v2.11 | [Docs](https://docs.yugabyte.com/preview/explore/ysql-language-features/gin/) | Support for generalized inverted indexes for container data types like jsonb, tsvector, and array |
| [YSQL-Collation Support](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/ysql-collation-support.md)  | ✅ *DONE*  | v2.11           |[Docs](https://docs.yugabyte.com/preview/explore/ysql-language-features/collations/) |Allows specifying the sort order and character classification behavior of data per-column, or even per-operation according to language and country-specific rules           |
[YSQL-Savepoint Support](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/savepoints.md)  |  ✅ *DONE*  | v2.11     |[Docs](https://docs.yugabyte.com/preview/explore/ysql-language-features/savepoints/) | Useful for implementing complex error recovery in multi-statement transaction|
| [xCluster replication management through Platform](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/platform-xcluster-replication-management.md) | ✅ *DONE* | v2.11           |   [Docs](https://docs.yugabyte.com/preview/yugabyte-platform/create-deployments/async-replication-platform/)     |   
| [Spring Data YugabyteDB module](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/spring-data-yugabytedb.md) | ✅ *DONE*  | v2.9 | [Track](https://github.com/yugabyte/yugabyte-db/issues/7956) | Bridges the gap for learning the distributed SQL concepts with familiarity and ease of Spring Data APIs |
| Support Liquibase, Flyway, ORM schema migrations | ✅ *DONE* | v2.9           |           [Docs](https://blog.yugabyte.com/schema-versioning-in-yugabytedb-using-flyway/)      | 
| [Support `ALTER TABLE` add primary key](https://github.com/yugabyte/yugabyte-db/issues/1124) | ✅ *DONE* | v2.9 | [Track](https://github.com/yugabyte/yugabyte-db/issues/1124) |  |
| [YCQL-LDAP Support](https://github.com/yugabyte/yugabyte-db/issues/4421) |  ✅ *DONE*  | v2.8           |[Docs](https://docs.yugabyte.com/preview/secure/authentication/ldap-authentication-ycql/#root)  | support LDAP authentication in YCQL API |             
| [Platform Alerting and Notification](https://blog.yugabyte.com/yugabytedb-2-8-alerts-and-notifications/) | ✅ *DONE* | v2.8  |  [Docs](https://docs.yugabyte.com/preview/yugabyte-platform/alerts-monitoring/alert/) |  To get notified in real time about database alerts, user defined alert policies notify you when a performance metric rises above or falls below a threshold you set.|      
| [Platform API](https://blog.yugabyte.com/yugabytedb-2-8-api-automated-operations/) | ✅ *DONE* | v2.8           |   [Docs](https://api-docs.yugabyte.com/docs/yugabyte-platform/ZG9jOjIwMDY0MTA4-platform-api-overview)              |   Securely Deploy YugabyteDB Clusters Using Infrastructure-as-Code|            

# Architecture

<img src="https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/architecture/images/yb-architecture.jpg" align="center" alt="YugabyteDB Architecture"/>

Review detailed architecture in our [Docs](https://docs.yugabyte.com/preview/architecture/).

# Need Help?

* You can ask questions, find answers, and help others on our Community [Slack](https://communityinviter.com/apps/yugabyte-db/register), [Forum](https://forum.yugabyte.com), [Stack Overflow](https://stackoverflow.com/questions/tagged/yugabyte-db), as well as Twitter [@Yugabyte](https://twitter.com/yugabyte)

* Please use [GitHub issues](https://github.com/yugabyte/yugabyte-db/issues) to report issues or request new features.

* To Troubleshoot YugabyteDB, cluser/node level issues, Please refer to [Troubleshooting documentation](https://docs.yugabyte.com/preview/troubleshoot/)

# Contribute

As an open-source project with a strong focus on the user community, we welcome contributions as GitHub pull requests. See our [Contributor Guides](https://docs.yugabyte.com/preview/contribute/) to get going. Discussions and RFCs for features happen on the design discussions section of our [Forum](https://forum.yugabyte.com).

# License

Source code in this repository is variously licensed under the Apache License 2.0 and the Polyform Free Trial License 1.0.0. A copy of each license can be found in the [licenses](licenses) directory.

The build produces two sets of binaries:

* The entire database with all its features (including the enterprise ones) are licensed under the Apache License 2.0
* The  binaries that contain `-managed` in the artifact and help run a managed service are licensed under the Polyform Free Trial License 1.0.0.

> By default, the build options generate only the Apache License 2.0 binaries.

# Read More

* To see our updates, go to [The Distributed SQL Blog](https://blog.yugabyte.com/).
* For an in-depth design and the YugabyteDB architecture, see our [design specs](https://github.com/yugabyte/yugabyte-db/tree/master/architecture/design).
* Tech Talks and [Videos](https://www.youtube.com/c/YugaByte).
* See how YugabyteDB [compares with other databases](https://docs.yugabyte.com/preview/faq/comparisons/).
