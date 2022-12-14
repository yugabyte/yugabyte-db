---
title: Drivers and ORMs for YugabyteDB
headerTitle: Drivers and ORMs
linkTitle: Drivers and ORMs
description: Connect your applications with supported drivers and ORMs
headcontent: Drivers and ORMs for YugabyteDB
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  v2.14:
    identifier: drivers-orms
    parent: develop
    weight: 570
type: indexpage
---

The [Yugabyte Structured Query Language (YSQL) API](../api/ysql/) builds upon and extends a fork of the query layer from PostgreSQL, with the intent of supporting most PostgreSQL functionality. Client applications can use the [PostgreSQL drivers](https://www.postgresql.org/download/products/2-drivers-and-interfaces/) to read and write data into YugabyteDB databases. YSQL-compatible PostgreSQL drivers are listed in the compatibility matrix below.

In addition to the compatible upstream PostgreSQL drivers, YugabyteDB also supports **smart drivers**, which extend the PostgreSQL drivers to enable client applications to connect to YugabyteDB clusters without the need for external load balancers. YugabyteDB smart drivers have the following features:

- **Cluster-aware**. Drivers know about all the data nodes in a YugabyteDB cluster, eliminating the need for an external load balancer.
- [Topology-aware](../deploy/multi-dc/). For geographically-distributed applications, the driver can seamlessly connect to the geographically nearest regions and availability zones for lower latency.

All YugabyteDB smart driver libraries are actively maintained, and receive bug fixes, performance enhancements, and security patches.

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
| [Spring Data YugabyteDB](/preview/integrations/spring-framework/sdyb/) | Full | [CRUD Example](../quick-start/build-apps/java/ysql-sdyb/) |
| [Spring Data JPA](/preview/integrations/spring-framework/sd-jpa/) | Full | [CRUD Example](../quick-start/build-apps/java/ysql-spring-data/) |
<!-- | Micronaut | Beta |  | -->
<!-- | Quarkus | Beta |  | -->
<!-- | MyBatis | Full |  | -->

### [Go](go/)

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [YugabyteDB PGX](go/yb-pgx/) [Recommended] | Full | [CRUD Example](go/yb-pgx/) |
| [PGX](go/pgx/) | Full | [CRUD Example](go/pgx/) |
| [PQ](go/pq/) | Full | [CRUD Example](go/pq/) |
| [GORM](go/gorm/) | Full | [CRUD Example](go/gorm/) |
| [PG](go/pg/) | Full | [CRUD Example](go/pg/) |

### [Node.js](nodejs/)

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [PostgreSQL](nodejs/postgres-node-driver/) | Full | [CRUD Example](nodejs/postgres-node-driver/) |
| [Sequelize](nodejs/sequelize/) | Full | [CRUD Example](nodejs/sequelize/) |
| [Prisma](nodejs/prisma/) | Full | [CRUD Example](nodejs/prisma/)
<!-- | TypeORM | Full |   | -->

<!-- ### App Framework Support

| Framework | Support | Example |
| :--------- | :------------ | :----------- |
| Reactjs | Full |  |
| Nextjs | Full | | -->

### [C#](csharp/)

| Name | Type | Support | Example |
| :--- | :--- | :-------| :------ |
| [Npgsql](csharp/postgres-npgsql/) | Driver | Full | [CRUD Example](csharp/postgres-npgsql/) |
| [EntityFramework](csharp/entityframework/) | ORM | Full | [CRUD Example](csharp/entityframework/) |

### [Python](python/)

| Driver/ORM | Support Level | Example apps |
| :------------------------- | :------------ | :----------- |
| [Yugabyte Psycopg2](python/yugabyte-psycopg2/) [Recommended] | Full | [CRUD Example](python/yugabyte-psycopg2/) |
| [PostgreSQL Psycopg2](python/postgres-psycopg2/) | Full | [CRUD Example](python/postgres-psycopg2/) |
| aiopg | Full | [Quick start](../quick-start/build-apps/python/ysql-aiopg/) |
| [Django](python/django/) | Full | [CRUD Example](python/django/) |
| [SQLAlchemy](python/sqlalchemy/) | Full | [CRUD Example](python/sqlalchemy/) |

### [Rust](rust/)

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [Diesel](rust/diesel/) | Full | [Diesel](rust/diesel/) |

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

-->

<!--
<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
  <a class="section-link icon-offset" href="java/">
    <div class="head">
      <div class="icon">
        <i class="fa-brands fa-java"></i>
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
        <i class="fa-brands fa-node-js"></i>
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
        <i class="fa-brands fa-golang"></i>
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
        <i class="fa-brands fa-python"></i>
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
        <i class="fa-brands fa-php"></i>
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
</div>

</div>
-->
