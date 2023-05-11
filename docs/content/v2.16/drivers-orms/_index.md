---
title: Drivers and ORMs for YugabyteDB
headerTitle: Drivers and ORMs
linkTitle: Drivers and ORMs
description: Connect your applications with supported drivers and ORMs
headcontent: Connect applications with your database
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  v2.16:
    identifier: drivers-orms
    parent: develop
    weight: 570
type: indexpage
showRightNav: true
---

Applications connect to and interact with YugabyteDB using API client libraries (also known as client drivers). Because the YugabyteDB [YSQL API](../api/ysql/) is PostgreSQL-compatible, and the [YCQL API](../api/ycql/) has roots in the Apache Cassandra CQL, YugabyteDB supports many third-party drivers.

In addition to the compatible upstream PostgreSQL drivers, YugabyteDB also supports [smart drivers](smart-drivers/), which extend the PostgreSQL drivers to enable client applications to connect to YugabyteDB clusters without the need for external load balancers.

### Choose your language

<ul class="nav yb-pills">

  <li>
    <a href="java/" class="orange">
      <i class="fa-brands fa-java"></i>
      Java
    </a>
  </li>

  <li>
    <a href="go/" class="orange">
      <i class="fa-brands fa-golang"></i>
      Go
    </a>
  </li>

  <li>
    <a href="python/" class="orange">
      <i class="fa-brands fa-python"></i>
      Python
    </a>
  </li>

  <li>
    <a href="nodejs/" class="orange">
      <i class="fa-brands fa-node-js"></i>
      NodeJS
    </a>
  </li>

  <li>
    <a href="c/" class="orange">
      <i class="icon-c"></i>
      C
    </a>
  </li>

  <li>
    <a href="cpp/" class="orange">
      <i class="icon-cplusplus"></i>
      C++
    </a>
  </li>

  <li>
    <a href="csharp/" class="orange">
      <i class="icon-csharp"></i>
      C#
    </a>
  </li>

  <li>
    <a href="ruby/" class="orange">
      <i class="icon-ruby"></i>
      Ruby
    </a>
  </li>

  <li>
    <a href="rust/" class="orange">
      <i class="fa-brands fa-rust"></i>
      Rust
    </a>
  </li>

  <li>
    <a href="php/" class="orange">
      <i class="fa-brands fa-php"></i>
      PHP
    </a>
  </li>

  <li>
    <a href="scala/" class="orange">
      <i class="icon-scala"></i>
      Scala
    </a>
  </li>

</ul>

## Supported libraries

The following libraries are officially supported by YugabyteDB.

{{< tabpane text=true >}}

  {{% tab header="Java" lang="java" %}}

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [YugabyteDB JDBC Smart Driver](java/yugabyte-jdbc/) [Recommended] | Full | [CRUD](java/yugabyte-jdbc/) |
| [PostgreSQL JDBC Driver](java/postgres-jdbc/) | Full | [CRUD](java/postgres-jdbc/) |
| [YugabyteDB Java Driver for YCQL (3.10)](java/ycql/) | Full | [CRUD](java/ycql) |
| [YugabyteDB Java Driver for YCQL (4.6)](java/ycql-4.6/) | Full | [CRUD](java/ycql-4.6) |
| [Ebean](java/ebean/) | Full | [CRUD](java/ebean/) |
| [Hibernate](java/hibernate/) | Full | [CRUD](java/hibernate/) |
| [Spring Data YugabyteDB](../integrations/spring-framework/sdyb/) | Full | [CRUD](../integrations/spring-framework/sdyb/#examples) |
| [Spring Data JPA](../integrations/spring-framework/sd-jpa/) | Full | [CRUD](../integrations/spring-framework/sd-jpa/#fundamentals) |
<!-- | Micronaut | Beta |  | -->
<!-- | Quarkus | Beta |  | -->
<!-- | MyBatis | Full |  | -->

  {{% /tab %}}

  {{% tab header="Go" lang="go" %}}

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [YugabyteDB PGX Smart Driver](go/yb-pgx/) [Recommended] | Full | [CRUD](go/yb-pgx/) |
| [PGX Driver](go/pgx/) | Full | [CRUD](go/pgx/) |
| [PQ Driver](go/pq/) | Full | [CRUD](go/pq/) |
| [YugabyteDB Go Driver for YCQL](go/ycql/) | Full | [CRUD](go/ycql) |
| [GORM](go/gorm/) | Full | [CRUD](go/gorm/) |
| [PG](go/pg/) | Full | [CRUD](go/pg/) |

  {{% /tab %}}

  {{% tab header="Python" lang="python" %}}

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [YugabyteDB Psycopg2 Smart Driver](python/yugabyte-psycopg2/) [Recommended] | Full | [CRUD](python/yugabyte-psycopg2/) |
| [PostgreSQL Psycopg2 Driver](python/postgres-psycopg2/) | Full | [CRUD](python/postgres-psycopg2/) |
| aiopg | Full | [Hello World](../develop/build-apps/python/ysql-aiopg/) |
| [Django](python/django/) | Full | [CRUD](python/django/) |
| [SQLAlchemy](python/sqlalchemy/) | Full | [CRUD](python/sqlalchemy/) |

  {{% /tab %}}

  {{% tab header="NodeJS" lang="nodejs" %}}

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [YugabyteDB node-postgres Smart Driver](nodejs/yugabyte-node-driver/) [Recommended] | Full | [CRUD](nodejs/yugabyte-node-driver/) |
| [PostgreSQL node-postgres Driver](nodejs/postgres-node-driver/) | Full | [CRUD](nodejs/postgres-node-driver/) |
| [Sequelize](nodejs/sequelize/) | Full | [CRUD](nodejs/sequelize/) |
| [Prisma](nodejs/prisma/) | Full | [CRUD](nodejs/prisma/)

  {{% /tab %}}

  {{% tab header="C" lang="c" %}}

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [libpq C Driver](c/ysql/) | Full | [CRUD](c/ysql/) |

  {{% /tab %}}

  {{% tab header="C++" lang="cpp" %}}

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [libpqxx C++ Driver](cpp/ysql/) | Full | [CRUD](cpp/ysql/) |
| [YugabyteDB C++ Driver for YCQL](cpp/ycql/) | Full | [CRUD](cpp/ycql/) |

  {{% /tab %}}

  {{% tab header="C#" lang="csharp" %}}

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [PostgreSQL Npgsql Driver](csharp/postgres-npgsql/) | Full | [CRUD](csharp/postgres-npgsql/) |
| [YugabyteDB C# Driver for YCQL](csharp/ycql/) | Full | [CRUD](csharp/ycql/) |
| [Entity Framework](csharp/entityframework/) | Full | [CRUD](csharp/entityframework/) |

  {{% /tab %}}

  {{% tab header="Ruby" lang="ruby" %}}

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [Pg Gem Driver](ruby/ysql-pg/) | Full | [CRUD](ruby/ysql-pg/) |
| [YugabyteDB Ruby Driver for YCQL](ruby/ycql/) | Full | [CRUD](ruby/ycql/) |
| [YugabyteDB Ruby Driver for YCQL](ruby/ycql/) | Full | [CRUD](ruby/ycql/) |
| [ActiveRecord ORM](ruby/activerecord/) | Full | [CRUD](ruby/activerecord/) |

  {{% /tab %}}

  {{% tab header="Rust" lang="rust" %}}

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [Diesel](rust/diesel/) | Full | [CRUD](rust/diesel/) |

  {{% /tab %}}

  {{% tab header="PHP" lang="php" %}}

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [php-pgsql Driver](php/ysql/) | Full | [CRUD](php/ysql/) |

  {{% /tab %}}

  {{% tab header="Scala" lang="scala" %}}

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [YugabyteDB Java Driver for YCQL](scala/ycql/) | Full | [CRUD](scala/ycql/) |

  {{% /tab %}}

{{< /tabpane >}}

## Read more

- [PostgreSQL drivers](https://www.postgresql.org/download/products/2-drivers-and-interfaces/)
- [Cassandra Query Language (CQL)](https://cassandra.apache.org/doc/latest/cassandra/cql/index.html)