---
title: Quarkus framework integration for YSQL
linkTitle: Quarkus
description: Quarkus framework integration for YSQL using Yugabyte JDBC Driver
aliases:
menu:
  latest:
    identifier: quarkus
    parent: integrations
    weight: 648
isTocNested: true
showAsideToc: true
---

[Quarkus](https://quarkus.io/) is a Kubernetes Native Java stack tailored for OpenJDK HotSpot and GraalVM, crafted from the best of breed Java libraries and standards.

This describes how to build a simple JPA based web application using Quarkus framework for YSQL API using [Yugabyte JDBC Driver](../jdbc-driver).

## Prerequisites

- Follow [Quick start](../../quick-start/) instructions to run a local YugabyteDB cluster. Test YugabyteDB's YSQL API, as [documented](../../quick-start/explore/ysql/) so that you can confirm that you have YSQL service running on `localhost:5433`.
- You will need JDK 11 and above. You can use [SDKMAN](https://sdkman.io/install) to install the JDK runtime.

## Get Started

You can find the complete source at [java framework with smart driver for YSQL](https://github.com/yugabyte/yb-ms-data.git). This project has directories for different java frameworks such as spring-boot, quarkus and micronaut. Clone this repository to a local workstation and open the `yb-ms-data` directory in your favorite IDE to easily navigate and explore Quarkus's project files.

```sh
git clone https://github.com/srinivasa-vasu/yb-ms-data.git
```

## Dependencies

This project depends on the following libraries. 
```gradle
implementation("io.quarkus:quarkus-hibernate-orm")
implementation("io.quarkus:quarkus-flyway")
implementation("io.quarkus:quarkus-resteasy")
implementation("io.quarkus:quarkus-resteasy-jackson")
implementation("io.quarkus:quarkus-config-yaml")
implementation("io.quarkus:quarkus-agroal")
implementation("io.quarkus:quarkus-smallrye-fault-tolerance")
implementation("com.yugabyte:jdbc-yugabytedb:42.3.0")
```
Update the driver dependency library **("com.yugabyte:jdbc-yugabytedb:42.3.0")** to the latest version. Grab the latest version from [Yugabyte JDBC driver](../jdbc-driver).

## Driver Configuration

Refer to the file `yb-ms-data/quarkus/src/main/resources/application.yaml` in the project directory:

```yml
quarkus:
  datasource:
    db-kind: pgsql
    jdbc:
      url: jdbc:yugabytedb://[hostname:port]/yugabyte
      driver: com.yugabyte.Driver
      initial-size: 5
      max-size: 20
      additional-jdbc-properties:
        load-balance: todo
```

- **db-kind** indicates the type of the db instance. The value can be *pqsql* or *postgresql* for Postgres or Postgres API compatible instances. As YugabyteDB reuses Postgres Query layer, you can have one of these values.
- **url** is the JDBC connection string.
- **driver** is the JDBC driver class name.
- **additional-jdbc-properties** is where YugabyteDB driver specific properties such as `load-balance` and `topology-keys` can be set.

Update the JDBC url with the appropriate `hostname` and `port` number details `jdbc:yugabytedb://[hostname:port]/yugabyte` in the application.yaml file. Remember to remove the square brackets. It is just a place holder to indicate the fields that need user inputs.

## Build the app

Navigate to `yb-ms-data/quarkus` folder in the project:

```sh
cd yb-ms-data/quarkus
```

To build the app:

```sh
gradle quarkusBuild
```

## Run the app

To run & test the app:

```sh
gradle quarkusDev
```