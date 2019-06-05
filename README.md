<img src="https://camo.githubusercontent.com/ba1bb8c876e148273142e6c17542db24be13349f/68747470733a2f2f73332d75732d776573742d322e616d617a6f6e6177732e636f6d2f6173736574732e79756761627974652e636f6d2f79622d64622d6c6f676f2e706e67" align="center" height="56" alt="YugaByte DB"/>

---------------------------------------

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Documentation Status](https://readthedocs.org/projects/ansicolortags/badge/?version=latest)](https://docs.yugabyte.com/)
[![Ask in forum](https://img.shields.io/badge/ask%20us-forum-orange.svg)](https://forum.yugabyte.com/)
[![Gitter chat](https://badges.gitter.im/gitlabhq/gitlabhq.svg)](https://gitter.im/YugaByte/Lobby)

- [What is YugaByte DB?](#what-is-yugabyte-db)
- [Get Started](#get-started)
- [Build Apps](#build-apps)
- [Need Help?](#need-help)
- [Contribute](#contribute)
- [Read More](#read-more)

# What is YugaByte DB?

YugaByte DB is a high-performance, cloud-native distributed SQL database. Here are the salient points about it:
* Has a pluggable query layer, and supports two APIs:
    * **[YSQL](https://docs.yugabyte.com/latest/api/ysql/)** (PostgreSQL-compatible)
    * **[YCQL](https://docs.yugabyte.com/latest/api/ycql/)** (Apache Cassandra CQL roots with document and index support)
* Offers horizontal scalability, strong consistency, high availability
* Fault tolerant - can tolerate disk, node, zone and region failures automatically
* Supports geo-distributed deployments (multi-zone, multi-region, multi-cloud)
* Can be deployed in public clouds and natively inside Kubernetes
* Open source under the [Apache 2.0 license](https://github.com/YugaByte/yugabyte-db/blob/master/LICENSE.txt)

Read more about YugaByte DB in our [docs](https://docs.yugabyte.com/introduction/overview/).

# Get Started

* [Install YugaByte DB](https://docs.yugabyte.com/latest/quick-start/install/)
* [Create a local cluster](https://docs.yugabyte.com/latest/quick-start/create-local-cluster/)
* [Connect and try out SQL commands](https://docs.yugabyte.com/latest/quick-start/explore-ysql/)
* [Build apps](https://docs.yugabyte.com/latest/develop/build-apps/) using a PostgreSQL-compatible driver or ORM.
* Try a real-world app:
    * [Microservices-oriented e-commerce app](https://github.com/YugaByte/yugastore-java)
    * [Streaming IoT app with Kafka and Spark Streaming](https://docs.yugabyte.com/latest/develop/realworld-apps/iot-spark-kafka-ksql/)

Cannot find what you are looking for? Have a question? Please post your questions or comments to our [community forum](https://forum.yugabyte.com).

# Build Apps

YugaByte DB supports a number of languages and client drivers. Below is a brief list.

| Language  | ORM | YSQL Drivers | YCQL Drivers |
| --------- | --- | ------------ | ------------ |
| Java  | [Spring/Hibernate](https://docs.yugabyte.com/latest/develop/build-apps/java/ysql-spring-data/) | [PostgreSQL JDBC](https://docs.yugabyte.com/latest/develop/build-apps/java/ysql-jdbc/) | [cassandra-driver-core-yb](https://docs.yugabyte.com/latest/develop/build-apps/java/ycql/)
| Go  | [Gorm](https://github.com/YugaByte/orm-examples) | [pq](https://docs.yugabyte.com/latest/develop/build-apps/go/#ysql) | [gocql](https://docs.yugabyte.com/latest/develop/build-apps/go/#ycql)
| NodeJS  | [Sequelize](https://github.com/YugaByte/orm-examples) | [pg](https://docs.yugabyte.com/latest/develop/build-apps/nodejs/#ysql) | [cassandra-driver](https://docs.yugabyte.com/latest/develop/build-apps/nodejs/#ycql)
| Python  | [SQLAlchemy](https://github.com/YugaByte/orm-examples) | [psycopg2](https://docs.yugabyte.com/latest/develop/build-apps/python/#ysql) | [yb-cassandra-driver](https://docs.yugabyte.com/latest/develop/build-apps/python/#ycql)
| Ruby  | [ActiveRecord](https://github.com/YugaByte/orm-examples) | [pg](https://docs.yugabyte.com/latest/develop/build-apps/ruby/#ysql) | [yugabyte-ycql-driver](https://docs.yugabyte.com/latest/develop/build-apps/ruby/#ycql)
| C#  | Not tested | Not tested | [CassandraCSharpDriver](https://docs.yugabyte.com/latest/develop/build-apps/csharp/#ycql)
| C++ | Not tested | [libpqxx](https://docs.yugabyte.com/latest/develop/build-apps/cpp/#ysql) | [cassandra-cpp-driver](https://docs.yugabyte.com/latest/develop/build-apps/cpp/#ycql)
| C   | Not tested | [libpq](https://docs.yugabyte.com/latest/develop/build-apps/c/#ysql) | Not tested


# Need Help?

* You can ask questions, find answers, help others on the [YugaByte Community Forum](http://forum.yugabyte.com) and [Stack Overflow](https://stackoverflow.com/questions/tagged/yugabyte-db)

* Please use [GitHub issues](https://github.com/YugaByte/yugabyte-db/issues) to report issues.

# Contribute

As an open-source project with a strong focus on the user community, we welcome contributions as GitHub pull requests. See our [Contributor Guides](https://docs.yugabyte.com/latest/contribute/) to get going. Discussions and RFCs for features happen on the design discussions section of [our community forum](https://forum.yugabyte.com).

# Read More

* To see our updates, go to [the YugaByte DB Distributed SQL Blog](https://blog.yugabyte.com/).
* See how YugaByte [compares with other databases](https://docs.yugabyte.com/comparisons/). 
