---
title: Drivers and ORMs for YugabyteDB
headerTitle: Drivers and ORMs
linkTitle: Drivers and ORMs
description: Connect your applications with supported drivers and ORMs
headcontent: Connect applications with your database
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

Applications connect to and interact with YugabyteDB using API client libraries (also known as client drivers). Because the YugabyteDB [YSQL API](../api/ysql/) is PostgreSQL-compatible, and the [YCQL API](../api/ycql/) has roots in the Apache Cassandra CQL, YugabyteDB supports many third-party drivers.

In addition to the compatible upstream PostgreSQL drivers, YugabyteDB also supports [smart drivers](smart-drivers/), which extend the PostgreSQL drivers to enable client applications to connect to YugabyteDB clusters without the need for external load balancers.

### Choose your language

<div class="row">

  <div class="col-6 col-lg-4 col-xl-3">
    <a class="section-link icon-offset" href="java/">
      <div class="head">
        <div class="icon">
          <i class="fa-brands fa-java"></i>
        </div>
        <div class="title">Java</div>
      </div>
    </a>
  </div>

  <div class="col-6 col-lg-4 col-xl-3">
    <a class="section-link icon-offset" href="go/">
      <div class="head">
        <div class="icon">
          <i class="fa-brands fa-golang"></i>
        </div>
        <div class="title">Go</div>
      </div>
    </a>
  </div>

  <div class="col-6 col-lg-4 col-xl-3">
    <a class="section-link icon-offset" href="python/">
      <div class="head">
        <div class="icon">
          <i class="fa-brands fa-python"></i>
        </div>
        <div class="title">Python</div>
      </div>
    </a>
  </div>

  <div class="col-6 col-lg-4 col-xl-3">
    <a class="section-link icon-offset" href="nodejs/">
      <div class="head">
        <div class="icon">
          <i class="fa-brands fa-node-js"></i>
        </div>
        <div class="title">NodeJS</div>
      </div>
    </a>
  </div>

  <div class="col-6 col-lg-4 col-xl-3">
    <a class="section-link icon-offset" href="c/">
      <div class="head">
        <div class="icon">
          <i class="icon-c"></i>
        </div>
        <div class="title">C</div>
      </div>
    </a>
  </div>

  <div class="col-6 col-lg-4 col-xl-3">
    <a class="section-link icon-offset" href="cpp/">
      <div class="head">
        <div class="icon">
          <i class="icon-cplusplus"></i>
        </div>
        <div class="title">C++</div>
      </div>
    </a>
  </div>

  <div class="col-6 col-lg-4 col-xl-3">
    <a class="section-link icon-offset" href="csharp/">
      <div class="head">
        <div class="icon">
          <i class="icon-csharp"></i>
        </div>
        <div class="title">C#</div>
      </div>
    </a>
  </div>

  <div class="col-6 col-lg-4 col-xl-3">
    <a class="section-link icon-offset" href="ruby/">
      <div class="head">
        <div class="icon">
          <i class="icon-ruby"></i>
        </div>
        <div class="title">Ruby</div>
      </div>
    </a>
  </div>

  <div class="col-6 col-lg-4 col-xl-3">
    <a class="section-link icon-offset" href="rust/">
      <div class="head">
        <div class="icon">
          <i class="fa-brands fa-rust"></i>
        </div>
        <div class="title">Rust</div>
      </div>
    </a>
  </div>

  <div class="col-6 col-lg-4 col-xl-3">
    <a class="section-link icon-offset" href="php/">
      <div class="head">
        <div class="icon">
          <i class="fa-brands fa-php"></i>
        </div>
        <div class="title">PHP</div>
      </div>
    </a>
  </div>

  <div class="col-6 col-lg-4 col-xl-3">
    <a class="section-link icon-offset" href="scala/">
      <div class="head">
        <div class="icon">
          <i class="icon-scala"></i>
        </div>
        <div class="title">Scala</div>
      </div>
    </a>
  </div>

</div>

## Supported libraries

The following libraries are officially supported by YugabyteDB.

### Java

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [YugabyteDB JDBC Smart Driver](java/yugabyte-jdbc/) [Recommended] | Full | [CRUD Example](java/yugabyte-jdbc/) |
| [PostgreSQL JDBC Driver](java/postgres-jdbc/) | Full | [CRUD Example](java/postgres-jdbc/) |
| [YugabyteDB Java Driver for YCQL (3.10)](java/ycql/) | Full | [CRUD Example](java/ycql) |
| [YugabyteDB Java Driver for YCQL (4.6)](java/ycql-4.6/) | Full | [CRUD Example](java/ycql-4.6) |
| [Ebean](java/ebean/) | Full | [CRUD Example](java/ebean/) |
| [Hibernate](java/hibernate/) | Full | [CRUD Example](java/hibernate/) |
| [Spring Data YugabyteDB](../integrations/spring-framework/sdyb/) | Full | [CRUD Example](../integrations/spring-framework/sdyb/#examples) |
| [Spring Data JPA](../integrations/spring-framework/sd-jpa/) | Full | [Hello World](../develop/build-apps/java/ysql-spring-data/) |
<!-- | Micronaut | Beta |  | -->
<!-- | Quarkus | Beta |  | -->
<!-- | MyBatis | Full |  | -->

### Go

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [YugabyteDB PGX Smart Driver](go/yb-pgx/) [Recommended] | Full | [CRUD Example](go/yb-pgx/) |
| [PGX Driver](go/pgx/) | Full | [CRUD Example](go/pgx/) |
| [PQ Driver](go/pq/) | Full | [CRUD Example](go/pq/) |
| [YugabyteDB Go Driver for YCQL](go/ycql/) | Full | [CRUD Example](go/ycql) |
| [GORM](go/gorm/) | Full | [CRUD Example](go/gorm/) |
| [PG](go/pg/) | Full | [CRUD Example](go/pg/) |

### Python

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [YugabyteDB Psycopg2 Smart Driver](python/yugabyte-psycopg2/) [Recommended] | Full | [CRUD Example](python/yugabyte-psycopg2/) |
| [PostgreSQL Psycopg2 Driver](python/postgres-psycopg2/) | Full | [CRUD Example](python/postgres-psycopg2/) |
| aiopg | Full | [Hello World](../develop/build-apps/python/ysql-aiopg/) |
| [Django](python/django/) | Full | [CRUD Example](python/django/) |
| [SQLAlchemy](python/sqlalchemy/) | Full | [CRUD Example](python/sqlalchemy/) |

### Node.js

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [YugabyteDB node-postgres Smart Driver](nodejs/yugabyte-node-driver/) [Recommended] | Full | [CRUD Example](nodejs/yugabyte-node-driver/) |
| [PostgreSQL node-postgres Driver](nodejs/postgres-node-driver/) | Full | [CRUD Example](nodejs/postgres-node-driver/) |
| [Sequelize](nodejs/sequelize/) | Full | [CRUD Example](nodejs/sequelize/) |
| [Prisma](nodejs/prisma/) | Full | [CRUD Example](nodejs/prisma/)

### C

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [libpq C Driver](c/ysql/) | Full | [CRUD Example](c/ysql/) |

### C++

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [libpqxx C++ Driver](cpp/ysql/) | Full | [CRUD Example](cpp/ysql/) |
| [YugabyteDB C++ Driver for YCQL](cpp/ycql/) | Full | [CRUD Example](cpp/ycql/) |

### C#

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [PostgreSQL Npgsql Driver](csharp/postgres-npgsql/) | Full | [CRUD Example](csharp/postgres-npgsql/) |
| [YugabyteDB C# Driver for YCQL](csharp/ycql/) | Full | [CRUD Example](csharp/ycql/) |
| [Entity Framework](csharp/entityframework/) | Full | [CRUD Example](csharp/entityframework/) |

### Ruby

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [Pg Gem Driver](ruby/ysql-pg/) | Full | [CRUD example](ruby/ysql-pg/) |
| [YugabyteDB Ruby Driver for YCQL](ruby/ycql/) | Full | [CRUD example](ruby/ycql/) |
| [YugabyteDB Ruby Driver for YCQL](ruby/ycql/) | Full | [CRUD example](ruby/ycql/) |
| [ActiveRecord ORM](ruby/activerecord/) | Full | [CRUD example](ruby/activerecord/) |

### Rust

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [Diesel](rust/diesel/) | Full | [CRUD example](rust/diesel/) |

### PHP

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [php-pgsql Driver](php/ysql/) | Full | [CRUD example](php/ysql/) |

### Scala

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [YugabyteDB Java Driver for YCQL](scala/ycql/) | Full | [CRUD example](scala/ycql/) |

## Read more

- [PostgreSQL drivers](https://www.postgresql.org/download/products/2-drivers-and-interfaces/)
- [Cassandra Query Language (CQL)](https://cassandra.apache.org/doc/latest/cassandra/cql/index.html)