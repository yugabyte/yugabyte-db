---
title: Postgres JDBC Driver
linkTitle: Postgres JDBC Driver
description: Postgres JDBC Driver for YSQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
aliases:
  - /latest/develop/drivers-orms/java/postgres-jdbc/
menu:
  latest:
    identifier: postgres-jdbc-driver
    parent: java
    weight: 577
isTocNested: true
showAsideToc: true
---
The [PostgreSQL JDBC driver](https://jdbc.postgresql.org/) is the official JDBC driver for PostgreSQL which can used for connecting to YugabyteDB YSQL. YugabyteDB YSQL has full compatiblity with PostgreSQL JDBC Driver, allows Java programmers to connect to YugabyteDB database to execute DMLs and DDLs using the JDBC APIs.

## Quick Start

Learn how to eastablish a connection to YugabyteDB database and begin simple CRUD operations using the steps in [Build an Application](/latest/quick-start/build-apps/java/ysql-jdbc) in Quick Start section.

## Fundamentals

Learn how to perform the fundamental tasks requried for Java App development using the PostgreSQL JDBC driver

  * [Download the Driver Dependency](http://localhost:1313/latest/develop/drivers-orms/java/postgres-jdbc/#download-the-driver-dependency)
  * [Connect to YugabyteDB Database](http://localhost:1313/latest/develop/drivers-orms/java/postgres-jdbc/#connect-to-yugabytedb-database)

### Download the Driver Dependency

Postgres JDBC Drivers are available as maven dependency, you can download the driver by adding the following dependency in to your java project.

<h4>Maven Depedency</h4>

```
<!-- https://mvnrepository.com/artifact/org.postgresql/postgresql -->
<dependency>
  <groupId>org.postgresql</groupId>
  <artifactId>postgresql</artifactId>
  <version>42.2.14</version>
</dependency>
```

<h4>Gradle Dependency</h4>

```
// https://mvnrepository.com/artifact/org.postgresql/postgresql
implementation 'org.postgresql:postgresql:42.2.14'
```

### Connect to YugabyteDB Database

You can connect to and query the YugabyteDB database using the `DriverManager` class.


Use the `DriverManager.getConnection` 

