---
title: Drivers and ORMs for YugabyteDB
headerTitle: Drivers and ORMs
linkTitle: Drivers and ORMs
description: Connect your applications with supported drivers and ORMs
headcontent: Drivers and ORMs for YugabyteDB
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
aliases:
  - /drivers-orms/
menu:
  preview:
    identifier: drivers-orms
    parent: develop
    weight: 570
type: indexpage
showRightNav: true
---

The [Yugabyte Structured Query Language (YSQL) API](../api/ysql/) builds upon and extends a fork of the query layer from PostgreSQL, with the intent of supporting most PostgreSQL functionality. Client applications can use the [PostgreSQL drivers](https://www.postgresql.org/download/products/2-drivers-and-interfaces/) to read and write data into YugabyteDB databases. YSQL-compatible PostgreSQL drivers are listed in the compatibility matrix below.

## Smart drivers

In addition to the compatible upstream PostgreSQL drivers, YugabyteDB also supports [smart drivers](smart-drivers/), which extend the PostgreSQL drivers to enable client applications to connect to YugabyteDB clusters without the need for external load balancers.

{{< note title="Note" >}}

To take advantage of smart driver load balancing features when connecting to clusters in YugabyteDB Managed, applications using smart drivers must be deployed in a VPC that has been peered with the cluster VPC. For information on VPC networking in YugabyteDB Managed, refer to [VPC network](../yugabyte-cloud/cloud-basics/cloud-vpcs/).

For applications that access the cluster from a non-peered network, use the upstream PostgreSQL driver instead; in this case, the cluster performs the load balancing. Applications that use smart drivers from non-peered networks fall back to the upstream driver behaviour automatically.

{{< /note >}}

## Supported libraries

The following libraries are officially supported by YugabyteDB.

### [Java](java/)

| Driver/ORM | Support Level | Example apps |
| :-------------------------- | :------------ | :----------- |
| [YugabyteDB JDBC Smart Driver](java/yugabyte-jdbc/) [Recommended] | Full | [CRUD Example](java/yugabyte-jdbc/) |
| [PostgreSQL JDBC Driver](java/postgres-jdbc/) | Full | [CRUD Example](java/postgres-jdbc/) |
| [Ebean](java/ebean/) | Full | [CRUD Example](java/ebean/) |
| [Hibernate](java/hibernate/) | Full | [CRUD Example](java/hibernate/) |
| [Spring Data YugabyteDB](../integrations/spring-framework/sdyb/) | Full | [CRUD Example](../integrations/spring-framework/sdyb/#examples) |
| [Spring Data JPA](../integrations/spring-framework/sd-jpa/) | Full | [Hello World](../develop/build-apps/java/ysql-spring-data/) |
<!-- | Micronaut | Beta |  | -->
<!-- | Quarkus | Beta |  | -->
<!-- | MyBatis | Full |  | -->

### [Go](go/)

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [YugabyteDB PGX Smart Driver](go/yb-pgx/) [Recommended] | Full | [CRUD Example](go/yb-pgx/) |
| [PGX Driver](go/pgx/) | Full | [CRUD Example](go/pgx/) |
| [PQ Driver](go/pq/) | Full | [CRUD Example](go/pq/) |
| [GORM](go/gorm/) | Full | [CRUD Example](go/gorm/) |
| [PG](go/pg/) | Full | [CRUD Example](go/pg/) |

### [Node.js](nodejs/)

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [YugabyteDB node-postgres Smart Driver](nodejs/yugabyte-node-driver/) [Recommended] | Full | [CRUD Example](nodejs/yugabyte-node-driver/) |
| [PostgreSQL node-postgres Driver](nodejs/postgres-node-driver/) | Full | [CRUD Example](nodejs/postgres-node-driver/) |
| [Sequelize](nodejs/sequelize/) | Full | [CRUD Example](nodejs/sequelize/) |
| [Prisma](nodejs/prisma/) | Full | [CRUD Example](nodejs/prisma/)

### [C#](csharp/)

| Name | Type | Support | Example |
| :--- | :--- | :-------| :------ |
| [Npgsql Driver](csharp/postgres-npgsql/) | Driver | Full | [CRUD Example](csharp/postgres-npgsql/) |
| [EntityFramework](csharp/entityframework/) | ORM | Full | [CRUD Example](csharp/entityframework/) |

### [Python](python/)

| Driver/ORM | Support Level | Example apps |
| :------------------------- | :------------ | :----------- |
| [YugabyteDB Psycopg2 Smart Driver](python/yugabyte-psycopg2/) [Recommended] | Full | [CRUD Example](python/yugabyte-psycopg2/) |
| [PostgreSQL Psycopg2 Driver](python/postgres-psycopg2/) | Full | [CRUD Example](python/postgres-psycopg2/) |
| aiopg | Full | [Hello World](../develop/build-apps/python/ysql-aiopg/) |
| [Django](python/django/) | Full | [CRUD Example](python/django/) |
| [SQLAlchemy](python/sqlalchemy/) | Full | [CRUD Example](python/sqlalchemy/) |

### [Rust](rust/)

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [Diesel](rust/diesel/) | Full | [Diesel](rust/diesel/) |
