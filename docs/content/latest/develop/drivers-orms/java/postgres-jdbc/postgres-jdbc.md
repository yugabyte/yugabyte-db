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
    weight: 570
isTocNested: true
showAsideToc: true
---

<!-- <ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="postgres-jdbc-fundamentals" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      Postgres JDBC Driver - Fundamentals
    </a>
  </li>
</ul> -->

The [PostgreSQL JDBC driver](https://jdbc.postgresql.org/) is the official JDBC driver for PostgreSQL which can used for connecting to YugabyteDB YSQL. YugabyteDB YSQL has full compatiblity with PostgreSQL JDBC Driver, allows Java programmers to connect to YugabyteDB database to execute DMLs and DDLs using the JDBC APIs.

## Quick Start

Learn how to establish a connection to YugabyteDB database and begin simple CRUD operations using the steps in [Build an Application](/latest/quick-start/build-apps/java/ysql-jdbc) in the Quick Start section.

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
| 42.2.14  | 2.11 (latest) | full
| 42.2.14 |  2.8 (stable) | full
| 42.2.14 | 2.6 | full

## Fundamentals

Learn how to perform the fundamental tasks required for Java App development using the PostgreSQL JDBC driver

<!-- * [Connect to YugabyteDB Database](postgres-jdbc-fundamentals/#connect-to-yugabytedb-database)
* [Configure SSL/TLS](postgres-jdbc-fundamentals/#configure-ssl-tls)
* [Create Table](/postgres-jdbc-fundamentals/#create-table)
* [Read and Write Queries](/postgres-jdbc-fundamentals/#read-and-write-queries) -->

### Connect to YugabyteDB Database

You can connect to and query the YugabyteDB database using the `DriverManager` class.

Use the `DriverManager.getConnection` for getting connection object for the YugabyteDB Database which can be used for performing DDLs and DMLs against the database.

JDBC Connection String

```java
jdbc://postgresql://hostname:port/database
```

Example JDBC URL for connecting to YugabyteDB can be seen below.

```java
Connection conn = DriverManager.getConnection("jdbc:postgresql://localhost:5433/yugabyte","yugabyte", "yugabyte");
```

| JDBC Params | Description | Default |
| :---------- | :---------- | :------ |
| hostname  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| database | database name | yugabyte
| user | user for connecting to the database | yugabyte
| password | password for connecting to the database | yugabyte

### Configure SSL/TLS

To build a Java application that communicate securily over SSL with YugabyteDB database, you need the root certificate (`ca.crt`), and node certificate (`yugabytedb.crt`) and key (`yugabytedb.key`) files. If you have not generated these files, follow the instructions in [Create server certificates](../../../../secure/tls-encryption/server-certificates/).

#### Set up SSL certificates for Java applications

To build a Java application that connects to YugabyteDB over an SSL connection, you need the root certificate (`ca.crt`), and node certificate (`yugabytedb.crt`) and key (`yugabytedb.key`) files. If you have not generated these files, follow the instructions in [Create server certificates](../../../../secure/tls-encryption/server-certificates/).

1. Download the certificate (`yugabytedb.crt`, `yugabytedb.key`, and `ca.crt`) files (see [Copy configuration files to the nodes](../../../../secure/tls-encryption/server-certificates/#copy-configuration-files-to-the-nodes)).

1. If you do not have access to the system `cacerts` Java truststore you can create your own truststore.

    ```sh
    $ keytool -keystore ybtruststore -alias ybtruststore -import -file ca.crt
    ```

1. Verify the `yugabytedb.crt` client certificate with `ybtruststore`.

    ```sh
    $ openssl verify -CAfile ca.crt -purpose sslclient tlstest.crt
    ```

1. Convert the client certificate to DER format.

    ```sh
    $ openssl x509 –in yugabytedb.crt -out yugabytedb.crt.der -outform der
    ```

1. Convert the client key to pk8 format.

    ```sh
    $ openssl pkcs8 -topk8 -inform PEM -in yugabytedb.key -outform DER -nocrypt -out yugabytedb.key.pk8
    ```

#### Client Side Authentication

#### SSL Modes

| JDBC Params | Description | Default |
| :----- | :------------ | :----------- |
| hostname  | hostname of the YugabyteDB instance | localhost

### Create Table

### Read and Write Queries

## Transaction and Isolation Levels

## Other Usage Examples

## FAQ
