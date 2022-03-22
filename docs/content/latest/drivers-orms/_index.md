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

Connect your application to your YugabyteDB with one of our supported Drivers, ORMs and AppDev Frameworks.

YugabyteDB supports <b>Smart Language Drivers</b> which enables the clients applications to connect to all nodes of the YugabyteDB cluster by elminating the need for external load balancers. All of the YugabyteDB Smart Driver libraries are actively maintained, and receive bug fixes, performance enhancements, and security patches. The YugabyteDB Smart Drivers have the following features:

- YugabyteDB Drivers are <b>cluster-aware</b>. Drivers have the knowledge of all the data nodes in the YugabyteDB cluster which eliminates the need for an external load balancer.
- YugabyteDB Driver are [topology-aware](/latest/deploy/multi-dc/), which is essential for geographically-distributed applications. The driver is able to connect to YugabyteDB servers that are part of different geo-locations seamlessly without need of an external load balancer.

Along with the Smart drivers, YugabyteDB also provides support for upstream [PostgreSQL drivers](https://www.postgresql.org/download/products/2-drivers-and-interfaces/) for the respecitve programming languages. The following libraries are officially supported by YugabyteDB.

## [Java](java/)

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| [YugabyteDB JDBC Smart Driver](java/yugabyte-jdbc)  [Recommended] | Full | [CRUD Example](/latest/quick-start/build-apps/java/ysql-yb-jdbc) |
| [Postgres JDBC Driver](java/postgres-jdbc) | Full | [CRUD Example](/latest/quick-start/build-apps/java/ysql-jdbc)  |
| Hibernate | Full |  |
| MyBatis | Full |  |

| App Framework | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| Spring Data YugabyteDB | Full |  |
| Spring Data JPA | Full |  |
| Micronaut | Beta |  |
| Quarkus | Beta |  |

## [Go](go/)

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| PGX [Recommended] | Full |  |
| PQ | Full | |
| GORM | Full | |
| PG | Full | |

## [JavaScript](javascript/)

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| PostgreSQL | Full |   |
| Sequelize | Full |   |
| TypeORM | Full |   |

### App Framework Support

| Framework | Support Level | Example apps |
| :--------- | :------------ | :----------- |
| Reactjs | Full |  |
| Nextjs | Full | |

## [.net](dotnet/)

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |

## [Python](python/)

| Driver/ORM | Support Level | Example apps |
| :--------- | :------------ | :----------- |

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
