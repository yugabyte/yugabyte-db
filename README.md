<img src="https://www.yugabyte.com/wp-content/themes/yugabyte/assets/images/yugabyteDB-site-logo-new-blue.svg" align="center" alt="YugabyteDB" width="50%"/>

---------------------------------------

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Documentation Status](https://readthedocs.org/projects/ansicolortags/badge/?version=latest)](https://docs.yugabyte.com/)
[![Ask in forum](https://img.shields.io/badge/ask%20us-forum-orange.svg)](https://forum.yugabyte.com/)
[![Slack chat](https://img.shields.io/badge/Slack:-%23yugabyte_db-blueviolet.svg?logo=slack)](https://communityinviter.com/apps/yugabyte-db/register)
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

See here: [YugabyteDB Roadmap](ROADMAP.md)

# Architecture

<img src="https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/architecture/images/yb-architecture.jpg" align="center" alt="YugabyteDB Architecture"/>

Review detailed architecture in our [Docs](https://docs.yugabyte.com/preview/architecture/).

# Need Help?

* You can ask questions, find answers, and help others on our Community [Slack](https://communityinviter.com/apps/yugabyte-db/register), [Forum](https://forum.yugabyte.com), [Stack Overflow](https://stackoverflow.com/questions/tagged/yugabyte-db), as well as Twitter [@Yugabyte](https://twitter.com/yugabyte)

* Please use [GitHub issues](https://github.com/yugabyte/yugabyte-db/issues) to report issues or request new features.

* To Troubleshoot YugabyteDB, cluser/node level isssues, Please refer to [Troubleshooting documentation](https://docs.yugabyte.com/preview/troubleshoot/)

# Contribute

As an an open-source project with a strong focus on the user community, we welcome contributions as GitHub pull requests. See our [Contributor Guides](https://docs.yugabyte.com/preview/contribute/) to get going. Discussions and RFCs for features happen on the design discussions section of our [Forum](https://forum.yugabyte.com).

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
