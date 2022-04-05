---
title: Drivers and ORMs for YugabyteDB
headerTitle: Drivers and ORMs
linkTitle: Drivers and ORMs
description: Connect your applications from one of Supported Drivers and ORMs
headcontent: Drivers and ORMs for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
aliases:
  - /drivers-orms/
section: YUGABYTEDB CORE
menu:
  latest:
    identifier: drivers-orms
    weight: 570
---

Connect applications to YugabyteDB databases using Yugabyte-supported drivers, ORMs, and application development frameworks.

YugabyteDB <b>smart language drivers</b> enable client applications to connect to YugabyteDB clusters without the need for external load balancers. YugabyteDB smart drivers have the following features:

- <b>Cluster-aware</b>. Drivers know about all the data nodes in a YugabyteDB cluster, which eliminates the need for an external load balancer.
- [Topology-aware](/latest/deploy/multi-dc/). For geographically-distributed applications, the driver can seamlessly connect to the geographically nearest regions and availability zones for lower latency.
All YugabyteDB smart driver libraries are actively maintained, and receive bug fixes, performance enhancements, and security patches.

Along with the smart drivers, YugabyteDB also supports upstream [PostgreSQL drivers](https://www.postgresql.org/download/products/2-drivers-and-interfaces/) for the respective programming languages. The following libraries are officially supported by YugabyteDB.

## [Java](java/)

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [YugabyteDB JDBC Smart Driver](java/yugabyte-jdbc)  [Recommended] | Full | [CRUD Example](/latest/quick-start/build-apps/java/ysql-yb-jdbc) |
| [Postgres JDBC Driver](java/postgres-jdbc) | Full | [CRUD Example](/latest/quick-start/build-apps/java/ysql-jdbc)  |
| [Hibernate](java/hibernate) | Full | [CRUD Example](java/hibernate/#step-1-add-the-hibernate-orm-dependency) |
<!-- | MyBatis | Full |  | -->

| App Framework | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [Spring Data YugabyteDB](/latest/integrations/spring-framework/sdyb/) | Full | [CRUD Example](/latest/quick-start/build-apps/java/ysql-spring-data/) |
| Spring Data JPA | Full | [CRUD Example](/latest/integrations/spring-framework/sdyb/#examples)|
<!-- | Micronaut | Beta |  | -->
<!-- | Quarkus | Beta |  | -->

## [Go](go/)

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [PGX](/latest/reference/drivers/go/pgx-reference/) [Recommended] | Full | [CRUD Example](go/pgx) |
| [PQ](/latest/reference/drivers/go/pq-reference/) | Full | [CRUD Example](go/pq)|
| [GORM](go/gorm/) | Full | [CRUD Example](go/gorm)|
| [PG](go/pg) | Full | [CRUD Example](go/pg) |

## [JavaScript](javascript/)

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [PostgreSQL](nodejs/postgres-node-driver) | Full |  [CRUD Example](/latest/quick-start/build-apps/nodejs/ysql-pg/) |
| [Sequelize](nodejs/sequelize) | Full |  [CRUD Example](/latest/quick-start/build-apps/nodejs/ysql-sequelize/)|
<!-- | TypeORM | Full |   | -->

<!-- ### App Framework Support

| Framework | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| Reactjs | Full |  |
| Nextjs | Full | | -->

## [C#](csharp/)

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| Npgsql(csharp/postgres-npgsql) | Full | [CRUD Example](/latest/quick-start/build-apps/csharp/ysql) |
| EntityFramework(csharp/entityframework) | Full | [CRUD Example](csharp/postgres-npgsql) |

## [Python](python/)

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [Yugabyte Psycopg2](/latest/drivers-orms/python/yugabyte-psycopg2)  [Recommended] | Full | [CRUD Example](/latest/drivers-orms/python/yugabyte-psycopg2)|
| [Postgres Psycopg2](/latest/drivers-orms/python/postgres-psycopg2) | Full | [CRUD Example](/latest/quick-start/build-apps/python/ysql-psycopg2) |
| aiopg | Full | [CRUD Example](/latest/quick-start/build-apps/python/ysql-aiopg) |
| [Django](/latest/drivers-orms/python/django) | Full | [CRUD Example](/latest/quick-start/build-apps/python/ysql-django) |
| [SQLAlchemy](/latest/drivers-orms/python/sqlalchemy) | Full | [CRUD Example](/latest/quick-start/build-apps/python/ysql-sqlalchemy) |

<!--
## [Ruby](ruby/)

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |

## [C](c/)

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |

## [C++](cpp/)

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |

## [PHP](php/)

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |

## [RUST](rust/)

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
-->

<!--
<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="java/">
    <div class="head">
      <div class="icon">
        <i class="icon-java"></i>
      </div>
      <div class="title">Java</div>
    </div>
    <div class="body">
      Java Client Drivers, ORMs and Frameworks.
    </div>
  </a>
</div>

 <div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="nodejs/">
    <div class="head">
      <div class="icon">
        <i class="icon-nodejs"></i>
      </div>
      <div class="title">NodeJS</div>
    </div>
    <div class="body">
      NodeJS Client Drivers, ORMs and Frameworks.
    </div>
  </a>
</div>

<div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="golang/">
    <div class="head">
      <div class="icon">
        <i class="icon-go"></i>
      </div>
      <div class="title">Go</div>
    </div>
    <div class="body">
      Golang Client Drivers, ORMs and Frameworks.
    </div>
  </a>
</div>

<div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="python/">
    <div class="head">
      <div class="icon">
        <i class="icon-python"></i>
      </div>
      <div class="title">Python</div>
    </div>
    <div class="body">
      Python Client Drivers, ORMs and Frameworks.
    </div>
  </a>
</div>

<div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="ruby/">
    <div class="head">
      <div class="icon">
        <i class="icon-ruby"></i>
      </div>
      <div class="title">Ruby</div>
    </div>
    <div class="body">
      Ruby Client Drivers, ORMs and Frameworks.
    </div>
  </a>
</div>

<div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="csharp/">
    <div class="head">
      <div class="icon">
        <i class="icon-csharp"></i>
      </div>
      <div class="title">C#</div>
    </div>
    <div class="body">
      C# Client Drivers, ORMs and Frameworks.
    </div>
  </a>
</div>

 <div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="php/ysql/">
    <div class="head">
      <div class="icon">
        <i class="icon-php"></i>
      </div>
      <div class="title">PHP</div>
    </div>
    <div class="body">
      Build applications using PHP.
    </div>
  </a>
</div>

<div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="cpp/ysql/">
    <div class="head">
      <div class="icon">
        <i class="icon-cplusplus"></i>
      </div>
      <div class="title">C++</div>
    </div>
    <div class="body">
      Build applications using C++.
    </div>
  </a>
</div>

<div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="c/ysql/">
    <div class="head">
      <div class="icon">
        <i class="icon-c"></i>
      </div>
      <div class="title">C</div>
    </div>
    <div class="body">
      Build applications using C.
    </div>
  </a>
</div>

<div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="scala/ycql/">
    <div class="head">
      <div class="icon">
        <i class="icon-scala"></i>
      </div>
      <div class="title">Scala</div>
    </div>
    <div class="body">
      Build applications using Scala.
    </div>
  </a>
</div> -->

</div>
