<img src="https://github.com/yugabyte/yugabyte-db/raw/master/architecture/images/ybDB_horizontal.jpg" align="center" alt="YugabyteDB"/>

---------------------------------------

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Documentation Status](https://readthedocs.org/projects/ansicolortags/badge/?version=latest)](https://docs.yugabyte.com/)
[![Ask in forum](https://img.shields.io/badge/ask%20us-forum-orange.svg)](https://forum.yugabyte.com/)
[![Slack chat](https://img.shields.io/badge/Slack:-%23yugabyte_db-blueviolet.svg?logo=slack)](https://www.yugabyte.com/slack)
[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/home?pixel&useReferer)](https://github.com/yugabyte/ga-beacon)

- [What is YugabyteDB?](#what-is-yugabytedb)
- [Get Started](#get-started)
- [Build Apps](#build-apps)
- [What's being worked on?](#whats-being-worked-on)
- [Architecture](#architecture)
- [Need Help?](#need-help)
- [Contribute](#contribute)
- [License](#license)
- [Read More](#read-more)

# What is YugabyteDB?

YugabyteDB is a high-performance, cloud-native distributed SQL database that aims to support all PostgreSQL features. It is best fit for cloud-native OLTP (i.e. real-time, business critical) applications that need absolute data correctness and require at least one of the following: scalability, high tolerance to failures, globally-distributed deployments.


The core features of YugabyteDB include:

* **Powerful RDBMS capabilities** Yugabyte SQL (*YSQL* for short) reuses the query layer of PostgreSQL (similar to Amazon Aurora PostgreSQL), thereby supporting most of its features (datatypes, queries, expressions, operators and functions, stored procedures, triggers, extensions, etc). Here is a detailed [list of features currently supported by YSQL](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/YSQL-Features-Supported.md).

* **Distributed transactions** The transactions design is based on the Google Spanner architecture. Strongly consistency of writes is achieved by using Raft consensus for replication and cluster-wide distributed ACID transactions using *hybrid logical clocks*. *Snapshot * and *serializable* isolation levels are supported.  Reads (queries) have strong consistency by default, but can be tuned dynamically to read from followers and read-replicas.

* **Continuous availability** YugabyteDB is extremely resilient to common outages with native failover and repair. YugabyteDB can be configured to tolerate disk, node, zone, region and cloud failures automatically. For a typical deployment where a YugabyteDB cluster is deployed in one region across multiple zones on a public cloud, the RPO is 0 (meaning no data is lost on a failure) and the RTO is 3 seconds (meaning the data being served by the failed node is available in 3 seconds).

* **Horizontal scalability** Scaling a YugabyteDB cluster in order to achieve more IOPS or data storage is as simple as adding nodes to the cluster.

* **Geo-distributed, multi-cloud** YugabyteDB can be deployed in public clouds and natively inside Kubernetes. It supports deployments that span three or more fault domains, such as multi-zone, multi-region and multi-cloud deployments. It also supports xCluster asynchronous replication with unidirectional master-slave and bidirectional multi-master configurations that can be leveraged in two-region deployments. To serve (stale) data with low latencies, read replicas are also a supported feature.

* **Multi API design** The query layer of YugabyteDB is built to be extensible. Currently, YugabyteDB supports two distributed SQL APIs **[Yugabyte SQL (YSQL)](https://docs.yugabyte.com/latest/api/ysql/)**, a fully relational API that re-uses query layer of PostgreSQL, and **[Yugabyte Cloud QL (YCQL)](https://docs.yugabyte.com/latest/api/ycql/)**, a semi-relational SQL-like API with documents/indexing support with Apache Cassandra QL roots.

* **100% open source** YugabyteDB is fully open-source under the [Apache 2.0 license](https://github.com/yugabyte/yugabyte-db/blob/master/LICENSE.md). The open-source version has powerful enterprise features distributed backups, encryption of data at-rest, in-flight TLS encryption, change data capture, read replicas and others.

Read more about YugabyteDB in our [Docs](https://docs.yugabyte.com/latest/introduction/).

# Get Started

* [Install YugabyteDB](https://docs.yugabyte.com/latest/quick-start/install/)
* [Create a local cluster](https://docs.yugabyte.com/latest/quick-start/create-local-cluster/)
* [Connect and try out SQL commands](https://docs.yugabyte.com/latest/quick-start/explore-ysql/)
* [Build an app](https://docs.yugabyte.com/latest/quick-start/build-apps/) using a PostgreSQL-compatible driver or ORM.
* Try running a real-world demo application:
    * [Microservices-oriented e-commerce app](https://github.com/yugabyte/yugastore-java)
    * [Streaming IoT app with Kafka and Spark Streaming](https://docs.yugabyte.com/latest/develop/realworld-apps/iot-spark-kafka-ksql/)

Cannot find what you are looking for? Have a question? Please post your questions or comments on our Community [Slack](https://www.yugabyte.com/slack) or [Forum](https://forum.yugabyte.com).

# Build Apps

YugabyteDB supports a number of languages and client drivers. Below is a brief list.

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

> This section has been updated on May 07, 2020.

Here is a list of some of the key features being worked on for the upcoming v2.2 release around July 2020, along with the recently completed features.

| Feature                                         | Status    | Release Target | Progress        |  Comments     |
| ----------------------------------------------- | --------- | -------------- | --------------- | ------------- |
| [Online rebuild of indexes](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/online-index-backfill.md)  | PROGRESS  | v2.2 | [Track](https://github.com/yugabyte/yugabyte-db/issues/448) | Available in beta for YCQL (docs coming soon), in progress for YSQL |
| Online schema migrations  | PROGRESS  | v2.2 | [Track](https://github.com/yugabyte/yugabyte-db/issues/4192) | YCQL already supports online schema changes |
| [Distributed backups for transactional tables](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/distributed-backup-and-restore.md)    | PROGRESS  | v2.2  | [Track](https://github.com/yugabyte/yugabyte-db/issues/2620) | Distributed backups for non-transactional tables in YCQL is already GA |
| [Automatic tablet splitting](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/docdb-automatic-tablet-splitting.md) | PROGRESS  | v2.2 | [Track](https://github.com/yugabyte/yugabyte-db/issues/1004) |  |
| Improve YSQL performance and support for benchmarks | PROGRESS  | v2.2  | [Track](https://github.com/yugabyte/yugabyte-db/issues/3226) |  |
| YSQL cluster administration features | PROGRESS  | v2.2  | [Track](https://github.com/yugabyte/yugabyte-db/issues/4194) |  |
| [`COLOCATED` tables](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/ysql-colocated-tables.md) | PROGRESS  | v2.2  |  |  |
| Audit logging support | PLANNING  | v2.3  | [Track](https://github.com/yugabyte/yugabyte-db/issues/1331) |  |
| Improving day 2 operations of Yugabyte Platform | PROGRESS  |  v2.3 | [Track](https://github.com/yugabyte/yugabyte-db/issues/4420) |  |
| [Row-level geo-partitioning](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/ysql-row-level-partitioning.md) | PROGRESS  |   | [Track](https://github.com/yugabyte/yugabyte-db/issues/1958) | This feature will be done in two phases: supporting PostgreSQL partitions followed by geo-placement support for partitions |
| [Support for most `ALTER TABLE` variants](https://github.com/yugabyte/yugabyte-db/issues/1124) | PROGRESS |  | [Track](https://github.com/yugabyte/yugabyte-db/issues/1124) |  |
| LDAP authentication support | PLANNING  |   | [Track](https://github.com/yugabyte/yugabyte-db/issues/2393) |  |
| Point in time restores | PLANNING  |   | [Track](https://github.com/yugabyte/yugabyte-db/issues/1820) |  |
| [Change data capture](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/docdb-change-data-capture.md) | ✅ *BETA* |   |  | This feature is currently available but in beta. |
| [Two datacenter (2DC) deployments](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/multi-region-2DC-deployment.md) | ✅ *DONE* | v2.1 | [Docs](https://docs.yugabyte.com/latest/deploy/multi-dc/2dc-deployment/) |  |
| [Encryption of data at rest](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/docdb-encryption-at-rest.md) | ✅ *DONE* | v2.1 | [Docs](https://docs.yugabyte.com/latest/secure/encryption-at-rest/) |  |


# Architecture

<img src="https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/architecture/images/yb-architecture.jpg" align="center" alt="YugabyteDB Architecture"/>

Review detailed architecture in our [Docs](https://docs.yugabyte.com/latest/architecture/).

# Need Help?

* You can ask questions, find answers, help others on our Community [Slack](https://www.yugabyte.com/slack) and [Forum](https://forum.yugabyte.com) as well as [Stack Overflow](https://stackoverflow.com/questions/tagged/yugabyte-db)

* Please use [GitHub issues](https://github.com/yugabyte/yugabyte-db/issues) to report issues.

# Contribute

As an open source project with a strong focus on the user community, we welcome contributions as GitHub pull requests. See our [Contributor Guides](https://docs.yugabyte.com/latest/contribute/) to get going. Discussions and RFCs for features happen on the design discussions section of [our Forum](https://forum.yugabyte.com).

# License

Source code in this repository is variously licensed under the Apache License 2.0 and the Polyform Free Trial License 1.0.0. A copy of each license can be found in the [licenses](licenses) directory.

The build produces two sets of binaries:
* The entire database with all its features (including the enterprise ones) are licensed under the Apache License 2.0
* The  binaries that contain `-managed` in the artifact and help run a managed service are licensed under the Polyform Free Trial License 1.0.0.

> By default, the build options generate only the Apache License 2.0 binaries.


# Read More

* To see our updates, go to [The Distributed SQL Blog](https://blog.yugabyte.com/).
* See how YugabyteDB [compares with other databases](https://docs.yugabyte.com/latest/comparisons/). 
