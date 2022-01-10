<img src="https://www.yugabyte.com/wp-content/uploads/2021/05/yb_horizontal_alt_color_RGB.png" align="center" alt="YugabyteDB" width="50%"/>

---------------------------------------

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Documentation Status](https://readthedocs.org/projects/ansicolortags/badge/?version=latest)](https://docs.yugabyte.com/)
[![Ask in forum](https://img.shields.io/badge/ask%20us-forum-orange.svg)](https://forum.yugabyte.com/)
[![Slack chat](https://img.shields.io/badge/Slack:-%23yugabyte_db-blueviolet.svg?logo=slack)](https://www.yugabyte.com/slack)
[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/home?pixel&useReferer)](https://github.com/yugabyte/ga-beacon)

# What is YugabyteDB? 

**YugabyteDB** is a **high-performance, cloud-native distributed SQL database** that aims to support **all PostgreSQL features**. It is best to fit for **cloud-native OLTP (i.e. real-time, business-critical) applications** that need absolute **data correctness** and require at least one of the following: **scalability, high tolerance to failures, or globally-distributed deployments.**

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

* **Powerful RDBMS capabilities** Yugabyte SQL (*YSQL* for short) reuses the query layer of PostgreSQL (similar to Amazon Aurora PostgreSQL), thereby supporting most of its features (datatypes, queries, expressions, operators and functions, stored procedures, triggers, extensions, etc). Here is a detailed [list of features currently supported by YSQL](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/YSQL-Features-Supported.md).

* **Distributed transactions** The transaction design is based on the Google Spanner architecture. Strong consistency of writes is achieved by using Raft consensus for replication and cluster-wide distributed ACID transactions using *hybrid logical clocks*. *Snapshot*, *serializable* and *read committed* isolation levels are supported. Reads (queries) have strong consistency by default, but can be tuned dynamically to read from followers and read-replicas.

* **Continuous availability** YugabyteDB is extremely resilient to common outages with native failover and repair. YugabyteDB can be configured to tolerate disk, node, zone, region, and cloud failures automatically. For a typical deployment where a YugabyteDB cluster is deployed in one region across multiple zones on a public cloud, the RPO is 0 (meaning no data is lost on failure) and the RTO is 3 seconds (meaning the data being served by the failed node is available in 3 seconds).

* **Horizontal scalability** Scaling a YugabyteDB cluster to achieve more IOPS or data storage is as simple as adding nodes to the cluster.

* **Geo-distributed, multi-cloud** YugabyteDB can be deployed in public clouds and natively inside Kubernetes. It supports deployments that span three or more fault domains, such as multi-zone, multi-region, and multi-cloud deployments. It also supports xCluster asynchronous replication with unidirectional master-slave and bidirectional multi-master configurations that can be leveraged in two-region deployments. To serve (stale) data with low latencies, read replicas are also a supported feature.

* **Multi API design** The query layer of YugabyteDB is built to be extensible. Currently, YugabyteDB supports two distributed SQL APIs: **[Yugabyte SQL (YSQL)](https://docs.yugabyte.com/latest/api/ysql/)**, a fully relational API that re-uses query layer of PostgreSQL, and **[Yugabyte Cloud QL (YCQL)](https://docs.yugabyte.com/latest/api/ycql/)**, a semi-relational SQL-like API with documents/indexing support with Apache Cassandra QL roots.

* **100% open source** YugabyteDB is fully open-source under the [Apache 2.0 license](https://github.com/yugabyte/yugabyte-db/blob/master/LICENSE.md). The open-source version has powerful enterprise features such as distributed backups, encryption of data-at-rest, in-flight TLS encryption, change data capture, read replicas, and more.

Read more about YugabyteDB in our [Docs](https://docs.yugabyte.com/latest/introduction/).

# Get Started

* [Install YugabyteDB](https://docs.yugabyte.com/latest/quick-start/install/)
* [Create a local cluster](https://docs.yugabyte.com/latest/quick-start/create-local-cluster/)
* [Start with Yugabyte Cloud](https://www.yugabyte.com/cloud/)
* [Connect and try out SQL commands](https://docs.yugabyte.com/latest/quick-start/explore-ysql/)
* [Build an app](https://docs.yugabyte.com/latest/quick-start/build-apps/) using a PostgreSQL-compatible driver or ORM.
* Try running a real-world demo application:
  * [Microservices-oriented e-commerce app](https://github.com/yugabyte/yugastore-java)
  * [Streaming IoT app with Kafka and Spark Streaming](https://docs.yugabyte.com/latest/develop/realworld-apps/iot-spark-kafka-ksql/)

Cannot find what you are looking for? Have a question? Please post your questions or comments on our Community [Slack](https://www.yugabyte.com/slack) or [Forum](https://forum.yugabyte.com).

# Build Apps

YugabyteDB supports several languages and client drivers. Below is a brief list.

| Language  | ORM | YSQL Drivers | YCQL Drivers |
| --------- | --- | ------------ | ------------ |
| Java  | [Spring/Hibernate](https://docs.yugabyte.com/latest/quick-start/build-apps/java/ysql-spring-data/) | [PostgreSQL JDBC](https://docs.yugabyte.com/latest/quick-start/build-apps/java/ysql-jdbc/) | [cassandra-driver-core-yb](https://docs.yugabyte.com/latest/quick-start/build-apps/java/ycql/)
| Go  | [Gorm](https://github.com/yugabyte/orm-examples) | [pq](https://docs.yugabyte.com/latest/quick-start/build-apps/go/#ysql) | [gocql](https://docs.yugabyte.com/latest/quick-start/build-apps/go/#ycql)
| NodeJS  | [Sequelize](https://github.com/yugabyte/orm-examples) | [pg](https://docs.yugabyte.com/latest/quick-start/build-apps/nodejs/#ysql) | [cassandra-driver](https://docs.yugabyte.com/latest/quick-start/build-apps/nodejs/#ycql)
| Python  | [SQLAlchemy](https://github.com/yugabyte/orm-examples) | [psycopg2](https://docs.yugabyte.com/latest/quick-start/build-apps/python/#ysql) | [yb-cassandra-driver](https://docs.yugabyte.com/latest/quick-start/build-apps/python/#ycql)
| Ruby  | [ActiveRecord](https://github.com/yugabyte/orm-examples) | [pg](https://docs.yugabyte.com/latest/quick-start/build-apps/ruby/#ysql) | [yugabyte-ycql-driver](https://docs.yugabyte.com/latest/quick-start/build-apps/ruby/#ycql)
| C#  | [EntityFramework](https://github.com/yugabyte/orm-examples) | [npgsql](http://www.npgsql.org/) | [CassandraCSharpDriver](https://docs.yugabyte.com/latest/quick-start/build-apps/csharp/#ycql)
| C++ | Not tested | [libpqxx](https://docs.yugabyte.com/latest/quick-start/build-apps/cpp/#ysql) | [cassandra-cpp-driver](https://docs.yugabyte.com/latest/quick-start/build-apps/cpp/#ycql)
| C   | Not tested | [libpq](https://docs.yugabyte.com/latest/quick-start/build-apps/c/#ysql) | Not tested

# What's being worked on?

> This section was last updated in **December, 2021**.

## Current roadmap

Here is a list of some of the key features being worked on for the upcoming releases (the YugabyteDB [**v2.11 latest release**](https://blog.yugabyte.com/announcing-yugabytedb-2-11/) has been released in **November, 2021**, and the [**v2.8 stable release**](https://blog.yugabyte.com/announcing-yugabytedb-2-8/) was released in **October 2021**).

| Feature                                         | Status    | Release Target | Progress        |  Comments     |
| ----------------------------------------------- | --------- | -------------- | --------------- | ------------- |
|[Upgrade to PostgreSQL v13](https://github.com/yugabyte/yugabyte-db/issues/9797)| PROGRESS| v2.13 |[Track](https://github.com/yugabyte/yugabyte-db/issues/9797)| For latest features, new PostgreSQL extensions, performance, and community fixes
|[Change Data Capture](https://github.com/yugabyte/yugabyte-db/issues/9019)| PROGRESS| v2.13 |[Track](https://github.com/yugabyte/yugabyte-db/issues/9019)|Allows multiple downstream apps and services to consume changes from YugabyteDB|
|Support for  [in-cluster PITR](https://github.com/yugabyte/yugabyte-db/issues/7120)  | PROGRESS| v2.13 |[Track](https://github.com/yugabyte/yugabyte-db/issues/7120)|Point in time recovery of YSQL databases, to a fixed point in time, across DDL and DML changes|
|[Support for materalized views](https://github.com/yugabyte/yugabyte-db/issues/10102) | PROGRESS| v2.13 |[Track](https://github.com/yugabyte/yugabyte-db/issues/10102)|Support create, drop, refresh materialized view|
|[Geo-partitioning support](https://github.com/yugabyte/yugabyte-db/issues/9980) for the transaction status table |PROGRESS| v2.13 |[Track]()|Allows creating multiple transaction status tables|
| [Automatic tablet splitting enabled by default](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/docdb-automatic-tablet-splitting.md) | PROGRESS  | v2.13 | [Track](https://github.com/yugabyte/yugabyte-db/issues/1004) |Enables changing the number of tablets (which are splits of data) at runtime.|
| YSQL-table statistics and cost based optimizer(CBO) | PROGRESS  |  v2.13 | [Track](https://github.com/yugabyte/yugabyte-db/issues/5242) | Improve YSQL query performance |
| [YSQL-Feature support - ALTER TABLE](https://github.com/yugabyte/yugabyte-db/issues/1124) | PROGRESS | v2.13 | [Track](https://github.com/yugabyte/yugabyte-db/issues/1124) | Support for various `ALTER TABLE` variants |
| [YSQL-Online schema migration](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/online-schema-migrations.md)  | PROGRESS  | v2.13 | [Track](https://github.com/yugabyte/yugabyte-db/issues/4192) | Schema migrations(includes DDL operations) to be safely run concurrently with foreground operations |
| [Row-level geo-partitioning](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/ysql-row-level-partitioning.md) | PROGRESS  |  v2.13 | [Track](https://github.com/yugabyte/yugabyte-db/issues/1958) | Enhance YSQL language support |
| Transparently restart transactions | PROGRESS  | v2.13 | [Track](https://github.com/yugabyte/yugabyte-db/issues/5683) | Decrease the incidence of transaction restart errors seen in various scenarios |
| Pessimistic locking Design | PROGRESS  | v2.13  | [Track](https://github.com/yugabyte/yugabyte-db/issues/5680) |  |
| Make [`COLOCATED` tables](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/ysql-colocated-tables.md) default for YSQL | PLANNING  |  | [Track](https://github.com/yugabyte/yugabyte-db/issues/5239)  |  |
| Support for transactions in async [xCluster replication](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/multi-region-2DC-deployment.md) | PLANNING  |    | [Track](https://github.com/yugabyte/yugabyte-db/issues/1808) | Apply transactions atomically on consumer cluster. |
| Support for GiST indexes | PLANNING  |    | [Track](https://github.com/yugabyte/yugabyte-db/issues/1337) |Suppor for GiST (Generalized Search Tree) based index|

## Recently released features

| Feature                                         | Status    | Release Target | Docs / Enhancements |  Comments     |
| ----------------------------------------------- | --------- | -------------- | ------------------- | ------------- |
| [YSQL-Support `GIN` indexes](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/ysql-gin-indexes.md) |  ✅ *DONE*  | v2.11 | [Docs](https://docs.yugabyte.com/latest/explore/ysql-language-features/gin/) | Support for generalized inverted indexes for container data types like jsonb, tsvector, and array |
| [YSQL-Collation Support](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/ysql-collation-support.md)  | ✅ *DONE*  | v2.11           |[Docs](https://docs.yugabyte.com/latest/explore/ysql-language-features/collations/) |Allows specifying the sort order and character classification behavior of data per-column, or even per-operation according to language and country-specific rules           |
[YSQL-Savepoint Support](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/savepoints.md)  |  ✅ *DONE*  | v2.11     |[Docs](https://docs.yugabyte.com/latest/explore/ysql-language-features/savepoints/) | Useful for implementing complex error recovery in multi-statement transaction|
| [xCluster replication management through Platform](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/platform-xcluster-replication-management.md) | ✅ *DONE* | v2.11           |   [Docs](https://docs.yugabyte.com/latest/yugabyte-platform/create-deployments/async-replication-platform/)     |   
| [Spring Data YugabyteDB module](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/spring-data-yugabytedb.md) | ✅ *DONE*  | v2.9 | [Track](https://github.com/yugabyte/yugabyte-db/issues/7956) | Bridges the gap for learning the distributed SQL concepts with familiarity and ease of Spring Data APIs |
| Support Liquibase, Flyway, ORM schema migrations | ✅ *DONE* | v2.9           |           [Docs](https://blog.yugabyte.com/schema-versioning-in-yugabytedb-using-flyway/)      | 
| [Support `ALTER TABLE` add primary key](https://github.com/yugabyte/yugabyte-db/issues/1124) | ✅ *DONE* | v2.9 | [Track](https://github.com/yugabyte/yugabyte-db/issues/1124) |  |
| [YCQL-LDAP Support](https://github.com/yugabyte/yugabyte-db/issues/4421) |  ✅ *DONE*  | v2.8           |[Docs](https://docs.yugabyte.com/latest/secure/authentication/ldap-authentication-ycql/#root)  | support LDAP authentication in YCQL API |             
| [Platform Alerting and Notification](https://blog.yugabyte.com/yugabytedb-2-8-alerts-and-notifications/) | ✅ *DONE* | v2.8  |  [Docs](https://docs.yugabyte.com/latest/yugabyte-platform/alerts-monitoring/alert/) |  To get notified in real time about database alerts, user defined alert policies notify you when a performance metric rises above or falls below a threshold you set.|      
| [Platform API](https://blog.yugabyte.com/yugabytedb-2-8-api-automated-operations/) | ✅ *DONE* | v2.8           |   [Docs](https://api-docs.yugabyte.com/docs/yugabyte-platform/ZG9jOjIwMDY0MTA4-platform-api-overview)              |   Securely Deploy YugabyteDB Clusters Using Infrastructure-as-Code|            

# Architecture

<img src="https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/architecture/images/yb-architecture.jpg" align="center" alt="YugabyteDB Architecture"/>

Review detailed architecture in our [Docs](https://docs.yugabyte.com/latest/architecture/).

# Need Help?

* You can ask questions, find answers, and help others on our Community [Slack](https://www.yugabyte.com/slack), [Forum](https://forum.yugabyte.com), [Stack Overflow](https://stackoverflow.com/questions/tagged/yugabyte-db), as well as Twitter [@Yugabyte](https://twitter.com/yugabyte)

* Please use [GitHub issues](https://github.com/yugabyte/yugabyte-db/issues) to report issues or request new features. 

* To Troubleshoot YugabyteDB, cluser/node level isssues, Please refer to [Troubleshooting documentation](https://docs.yugabyte.com/latest/troubleshoot/)

# Contribute

As an an open-source project with a strong focus on the user community, we welcome contributions as GitHub pull requests. See our [Contributor Guides](https://docs.yugabyte.com/latest/contribute/) to get going. Discussions and RFCs for features happen on the design discussions section of [our Forum](https://forum.yugabyte.com).

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
* See how YugabyteDB [compares with other databases](https://docs.yugabyte.com/latest/comparisons/).
