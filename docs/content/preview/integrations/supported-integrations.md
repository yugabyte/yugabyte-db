---
title: YugabyteDB Supported Integrations
linkTitle: Supported Integrations
description: YugabyteDB supported integrations, drivers, ORMs and tools.
section: INTEGRATIONS
menu:
  preview:
    identifier: supported-integrations
    weight: 410
isTocNested: true
showAsideToc: true
---

Connect applications to YugabyteDB using Yugabyte-supported drivers, ORMs, SQL clients, and application development frameworks.

We are continuously improving these tools and adding support for more tools, however, if you encounter any bugs or issues using drivers and tools listed on this page with YugabyteDB, please create a Github issue in the official [YugabyteDB repository](https://github.com/yugabyte/yugabyte-db) with details to help us address those issues. If you have any questions regarding these tools you can reach out to us on [YugabyteDB Community Slack](https://join.slack.com/t/yugabyte-db/shared_invite/zt-xbd652e9-3tN0N7UG0eLpsace4t1d2A).

## Object-relational Mapping (ORM) Tools

| Language | Framework | Documentation and Guides | Plugins and Apps | Github Issues |
| --- | --- | --- | --- | --- |
| Java | Spring Data JPA | Documentation[Quickstart](https://docs.yugabyte.com/latest/quick-start/build-apps/java/ysql-spring-data/)[Blog](https://blog.yugabyte.com/run-the-rest-version-of-spring-petclinic-with-angular-and-distributed-sql-on-gke/) | [Spring App](https://github.com/yugabyte/orm-examples/tree/master/java/spring) |
 |
| Java | Spring Data YugabyteDB | [Documentation](https://docs.yugabyte.com/latest/integrations/spring-framework/sdyb/)[Quickstart](https://docs.yugabyte.com/preview/quick-start/build-apps/java/ysql-sdyb/)[Blog](https://blog.yugabyte.com/spring-data-yugabytedb-getting-started/) | [Spring Data YugabyteDB Sample App](https://github.com/yugabyte/spring-data-yugabytedb-example) |
 |
| Java | Hibernate | [Documentation](https://docs.yugabyte.com/preview/drivers-orms/java/hibernate/)[Quickstart](https://docs.yugabyte.com/preview/quick-start/build-apps/java/ysql-hibernate/)[Blog](https://blog.yugabyte.com/run-the-rest-version-of-spring-petclinic-with-angular-and-distributed-sql-on-gke/) | [Hibernate App](https://github.com/yugabyte/orm-examples/tree/master/java/hibernate) |
 |
| Scala | Play (Ebean ORM) | Documentation[Quickstart](https://docs.yugabyte.com/latest/quick-start/build-apps/java/ysql-ebeans/) | [ORM App](https://github.com/yugabyte/orm-examples/tree/master/java/ebeans) | [Ebean ORM Integration](https://github.com/yugabyte/yugabyte-db/issues/11186) |
| Go | GORM | [Documentation](https://docs.yugabyte.com/latest/integrations/gorm/)[Quickstart](https://docs.yugabyte.com/latest/integrations/gorm/) | [GORM App](https://github.com/yugabyte/orm-examples/tree/master/golang/gorm) | [GORM Integration](https://github.com/yugabyte/yugabyte-db/issues/9515) |
| Python | Django | [Documentation](https://docs.yugabyte.com/latest/integrations/django-rest-framework/)[Quickstart](https://docs.yugabyte.com/latest/quick-start/build-apps/python/ysql-django/) | [Django App](https://github.com/yugabyte/orm-examples/tree/master/python/django) | [Django Integration](https://github.com/yugabyte/yugabyte-db/issues/10636) |
| Javascript | Sequelize | [Documentation](https://docs.yugabyte.com/latest/integrations/sequelize/#root)[Quickstart](https://docs.yugabyte.com/latest/quick-start/build-apps/nodejs/ysql-sequelize/)[Blog](https://blog.yugabyte.com/using-sequelize-with-yugabytedb/) | [Sequelize App](https://github.com/yugabyte/orm-examples/tree/master/node/sequelize) | [Sequelize Integration](https://github.com/yugabyte/yugabyte-db/issues/9350) |
| C# (.NET) | Entity Framework | [Documentation](https://docs.yugabyte.com/latest/quick-start/build-apps/csharp/ysql-entity-framework/)[Quickstart](https://docs.yugabyte.com/latest/integrations/entity-framework/) | [Entity App](https://github.com/yugabyte/orm-examples/tree/master/csharp/entityframework) | [EntityFramework Integration](https://github.com/yugabyte/yugabyte-db/issues/10541) |

## YSQL Smart Drivers

YugabyteDB smart drivers are custom built by Yugabyte and take advantage of YugabyteDB&#39;s state of the art distributed architecture and latest features. YugayteDB smart drivers enable client applications to connect to YugabyteDB clusters without the need of external load balancers. YugabyteDB smart drivers currently have the following capabilities:

- **Cluster Awareness** - Drivers know about all the data nodes in a YugabyteDB cluster, which eliminates the need for an external load balancer.
- **[Topology Awareness]**(/preview/deploy/multi-dc/) - For geographically-distributed applications, the driver can seamlessly connect to the geographically nearest regions and availability zones for lower latency.

All YugabyteDB smart driver libraries are actively maintained, and receive bug fixes, performance enhancements, and security patches.

| Language | Driver | Documentation and Guides | Github Issues |
| --- | --- | --- | --- |
| Java | JDBC | [Documentation](https://docs.yugabyte.com/preview/integrations/jdbc-driver/) |
 |
| Go | pgx | [Documentation](https://docs.yugabyte.com/preview/quick-start/build-apps/go/ysql-yb-pgx/) | [Go YSQL Smart Driver](https://github.com/yugabyte/yugabyte-db/issues/10760) |
| Python | Psycopg2 |
 | [Python YSQL Smart Driver](https://github.com/yugabyte/yugabyte-db/issues/10917) |

## YSQL Drivers

YugabyteDB is a Postgres compatible RDBMS and supports upstream PostgreSQL drivers for the respective programming languages. YugabyteDB reuses PostgreSQL&#39;s query layer and supports all advanced features. The following libraries are officially supported by YugabyteDB.

| Language | Driver | Documentation and Guides | Github Issues |
| --- | --- | --- | --- |
| Java | PostgreSQL JDBC | [Documentation](https://docs.yugabyte.com/latest/quick-start/build-apps/java/ysql-jdbc/) |
 |
| Python | psycopg2 | [Documentation](https://docs.yugabyte.com/latest/quick-start/build-apps/python/ysql-psycopg2/) | [YSQL Python driver](https://github.com/yugabyte/yugabyte-db/issues/9833) |
| Javascript/Node.js | pg | [Documentation](https://docs.yugabyte.com/latest/quick-start/build-apps/nodejs/ysql-pg/) | [YSQL Node.js driver](https://github.com/yugabyte/yugabyte-db/issues/9834) |
| Go | pq | [Documentation](https://docs.yugabyte.com/latest/quick-start/build-apps/go/ysql-pq/) | [Go(lib/pq) YSQL driver](https://github.com/yugabyte/yugabyte-db/issues/9835) |
| Go | pgx | [Documentation](https://docs.yugabyte.com/latest/quick-start/build-apps/go/ysql-pgx/) | [Go jackc/pgx YSQL driver](https://github.com/yugabyte/yugabyte-db/issues/9836) |
| C++ | libpqxx | [Documentation](https://docs.yugabyte.com/latest/quick-start/build-apps/cpp/ysql/) | [C++ YSQL drive](https://github.com/yugabyte/yugabyte-db/issues/9838)r |
| C# | Npgsql | [Documentation](https://docs.yugabyte.com/latest/quick-start/build-apps/csharp/ysql/) | [C# YSQL driver](https://github.com/yugabyte/yugabyte-db/issues/9838) |
| Ruby | pg | [Documentation](https://docs.yugabyte.com/latest/quick-start/build-apps/ruby/ysql-pg/) | [RoR driver](https://github.com/yugabyte/yugabyte-db/issues/10833) |

## YCQL Drivers

YugabyteDB YCQL is compatible with v3.4 of Apache Cassandra QL (CQL) APIs. The following YCQL drivers are officially supported by YugabyteDB.

| Language | Driver | Documentation and Guides | Github Issues |
| --- | --- | --- | --- |
| Java | Cassandra-driver-core-yb | [Documentation](https://docs.yugabyte.com/latest/quick-start/build-apps/java/ycql/) |
 |
| Python | yb-cassandra-driver | [Documentation](https://docs.yugabyte.com/latest/quick-start/build-apps/python/ycql/) |
 |
| Go | gocql | [Documentation](https://docs.yugabyte.com/latest/quick-start/build-apps/go/ycql/) | [Go Cassandra (YCQL) driver](https://github.com/yugabyte/yugabyte-db/issues/9818) |
| C++ | cassandra-cpp-driver | [Documentation](https://docs.yugabyte.com/latest/quick-start/build-apps/cpp/ycql/) |
 |
| Ruby | yugabyte-ycql-driver | [Documentation](https://docs.yugabyte.com/latest/quick-start/build-apps/ruby/ycql/) | [Ruby YCQL driver](https://github.com/yugabyte/yugabyte-db/issues/9821) |
