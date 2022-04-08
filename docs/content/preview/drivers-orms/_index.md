---
title: Drivers and ORMs for YugabyteDB
headerTitle: Drivers and ORMs
linkTitle: Drivers and ORMs
description: Connect your applications from one of Supported Drivers and ORMs
headcontent: Connect your application to your database using your favorite language.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
aliases:
  - /drivers-orms/
section: YUGABYTEDB CORE
menu:
  preview:
    identifier: drivers-orms
    weight: 570
---

Connect applications to YugabyteDB databases using Yugabyte-supported drivers, ORMs, and application development frameworks.

YugabyteDB <b>smart language drivers</b> enable client applications to connect to YugabyteDB clusters without the need for external load balancers. YugabyteDB smart drivers have the following features:

- <b>Cluster-aware</b>. Drivers know about all the data nodes in a YugabyteDB cluster, which eliminates the need for an external load balancer.
- [Topology](/preview/deploy/multi-dc/)-aware. For geographically-distributed applications, the driver can seamlessly connect to the geographically nearest regions and availability zones for lower latency.

All YugabyteDB smart driver libraries are actively maintained, and receive bug fixes, performance enhancements, and security patches.

Along with the smart drivers, YugabyteDB also supports upstream [PostgreSQL drivers](https://www.postgresql.org/download/products/2-drivers-and-interfaces/) for the respective programming languages.

## Supported libraries

The following libraries are officially supported by YugabyteDB. Recommended libraries are indicated using an asterisk.

### [Java](java/)

| Name | Type | Support | Example |
| :--------- | :--- |:------------- | :----------- |
| [YugabyteDB JDBC Smart Driver*](java/yugabyte-jdbc) | Smart Driver | Full | [CRUD](java/yugabyte-jdbc) |
| [PostgreSQL JDBC Driver](java/postgres-jdbc) | Driver | Full | [CRUD](java/postgres-jdbc)  |
| [Hibernate](java/hibernate) | ORM | Full | [CRUD](java/hibernate/#step-1-add-the-hibernate-orm-dependency) |
| [Spring Data YugabyteDB](/preview/integrations/spring-framework/sdyb/) | Framework | Full | [CRUD](/preview/integrations/spring-framework/sdyb/#examples) |
| Spring Data JPA | Framework | Full | [CRUD](/preview/quick-start/build-apps/java/ysql-spring-data/)|
<!-- | Micronaut | Beta |  | -->
<!-- | Quarkus | Beta |  | -->
<!-- | MyBatis | Full |  | -->

### [Go](go/)

| Name | Type | Support | Example |
| :--- | :--- | :------------ | :----------- |
| [PGX*](go/pgx/) | Driver | Full | [CRUD](go/pgx) |
| [PQ](go/pq) | Driver | Full | [CRUD](go/pq)|
| [GORM](go/gorm/) | ORM | Full | [CRUD](go/gorm)|
| [PG](go/pg) | ORM| Full | [CRUD](go/pg) |

### [Node.js](nodejs/)

| Name | Type | Support | Example |
| :--- | :--- | :------------ | :----------- |
| [node-postgres](nodejs/postgres-node-driver) | Driver | Full |  [CRUD](nodejs/postgres-node-driver) |
| [Sequelize](nodejs/sequelize) | ORM | Full |  [CRUD](nodejs/sequelize)|
<!-- | TypeORM | Full |   | -->

<!-- ### App Framework Support

| Framework | Support | Example |
| :--------- | :------------ | :----------- |
| Reactjs | Full |  |
| Nextjs | Full | | -->

### [C#](csharp/)

| Name | Type | Support | Example |
| :--- | :--- | :------------ | :----------- |
| [Npgsql](csharp/postgres-npgsql) | Driver | Full | [CRUD](csharp/postgres-npgsql) |
| [EntityFramework](csharp/entityframework) | ORM | Full | [CRUD](csharp/entityframework) |

### [Python](python/)

| Name | Type | Support | Example |
| :--- | :--- | :------------ | :----------- |
| [Yugabyte Psycopg2*](python/yugabyte-psycopg2) | Smart Driver | Full | [CRUD](python/yugabyte-psycopg2)|
| [PostgreSQL Psycopg2](python/postgres-psycopg2/) | Driver | Full | [CRUD](python/postgres-psycopg2/) |
| aiopg | Driver | Full | [Quick Start](/preview/quick-start/build-apps/python/ysql-aiopg) |
| [Django](python/django) | ORM | Full | [CRUD](python/django) |
| [SQLAlchemy](python/sqlalchemy) | ORM | Full | [CRUD](python/sqlalchemy) |

<!--
## [Ruby](ruby/)

| Driver/ORM | Support | Example |
| :--------- | :------------ | :----------- |

## [C](c/)

| Driver/ORM | Support | Example |
| :--------- | :------------ | :----------- |

## [C++](cpp/)

| Driver/ORM | Support | Example |
| :--------- | :------------ | :----------- |

## [PHP](php/)

| Driver/ORM | Support | Example |
| :--------- | :------------ | :----------- |

## [RUST](rust/)

| Driver/ORM | Support | Example |
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
