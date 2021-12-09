---
title: PostgreSQL JDBC Driver
linkTitle: PostgreSQL JDBC Driver
description: Postgres JDBC Driver for YSQL
headcontent: Postgres JDBC Driver for YSQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  latest:
    identifier: postgres-jdbc-driver
    parent: java-drivers
    weight: 577
isTocNested: true
showAsideToc: true
---

The [PostgreSQL JDBC driver](https://jdbc.postgresql.org/) is the official JDBC driver for PostgreSQL which can used for connecting to YugabyteDB YSQL. YugabyteDB YSQL has full compatiblity with PostgreSQL JDBC Driver, allows Java programmers to connect to YugabyteDB database to execute DMLs and DDLs using the JDBC APIs.

* [Quick Start](#quick-start)
* [Download the Driver Dependency](#download-driver-dependencuy)
* [Compatibility Matrix](#compatibility-matrix)
* [Fundamentals](#fundamentals)
* [Transaction and Isolation Levels](#transaction-isolation-levels)
* [Other Usage Examples](#other-usage-examples)
* [FAQ](#faq)

## Quick Start

Learn how to establish a connection to YugabyteDB database and begin simple CRUD operations using the steps in [Build an Application](/latest/quick-start/build-apps/java/ysql-jdbc) in Quick Start section.

## Download the Driver Dependency

Postgres JDBC Drivers are available as maven dependency, you can download the dirver by adding the following dependency in to your java project.

### Maven Depedency

```xml
<!-- https://mvnrepository.com/artifact/org.postgresql/postgresql -->
<dependency>
  <groupId>org.postgresql</groupId>
  <artifactId>postgresql</artifactId>
  <version>42.2.14</version>
</dependency>
```

### Gradle Dependency

```java
// https://mvnrepository.com/artifact/org.postgresql/postgresql
implementation 'org.postgresql:postgresql:42.2.14'
```

## Compatibility Matrix

| Driver Version | YugabyteDB Version | Support |
| :------------- | :----------------- | :------ |
| hostname  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| database | database name | yugabyte
| user | user for connecting to the database | yugabyte
| password | password for connecting to the database | yugabyte

## Fundamentals

Learn how to perform the fundamental tasks required for Java App development using the PostgreSQL JDBC driver

* [Connect to YugabyteDB Database](postgres-jdbc-fundamentals#connect-to-yugabytedb-database)

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="postgres-jdbc-fundamentals" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      Postgres JDBC Driver - Fundamentals
    </a>
  </li>
</ul>

## Transaction and Isolation Levels

## Other Usage Examples

## FAQ
