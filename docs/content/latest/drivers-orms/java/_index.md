---
title: Java
headerTitle: Java
linkTitle: Java
description: Java Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  latest:
    identifier: java-drivers
    parent: drivers-orms
    weight: 540
isTocNested: true
showAsideToc: true
---
Following are the recommended projects for implementing Java Applications for  YugabyteDB YSQL API.

| Project | Type | Support Level |
| :------ | :--- | :------------ |
| [YugabyteDB JDBC Driver](yugabyte-jdbc) | JDBC Driver | Full |
| [Hibernate](hibernate) | ORM |  Full |
| [Spring Data YugabyteDB](/latest/integrations/spring-framework/sdyb/) | Framework |  Full |

## Build a Hello World App

Learn how to establish a connection to YugabyteDB database and begin simple CRUD operations using the steps in [Build an Application](/latest/quick-start/build-apps/java/ysql-yb-jdbc) in the Quick Start section.

## Pre-requisites for Building a Java Application

### Install the Java Development Kit (JDK)

Make sure that your system has JDK 8 or later installed. For more information on how to check your version of Java and install the JDK, see the [AdoptOpenJDK Installation Page](https://adoptopenjdk.net/installation.html).

### Create a Java Project

Java Projects can be created using Maven or Gradle Software project management tools. We recommend using an integrate develpoment environemnt (IDE) such as Intellij IDEA or Eclipse IDE for conveniently configuring Maven or Gradle to build and run your project.

If you are not using an IDE, see [Building Maven](https://maven.apache.org/guides/development/guide-building-maven.html) or [Creating New Gradle Project](https://docs.gradle.org/current/samples/sample_building_java_applications.html) for more information on how to setup a Java project.

### Create a YugabyteDB Cluster

Set up a Free tier Cluster on [Yugabyte Anywhere](https://www.yugabyte.com/cloud/). The free cluster provides a fully functioning YugabyteDB cluster deployed to the cloud region of your choice. The cluster is free forever and includes enough resources to explore the core features available for developing the Java Applications with YugabyteDB database. Complete the steps for [creating a free tier cluster](latest/yugabyte-cloud/cloud-quickstart/qs-add/).

Alternatively, You can also setup a standalone YugabyteDB cluster by following the [install YugabyteDB Steps](/latest/quick-start/install/macos).

## Usage Examples

For fully runnable code snippets and exaplantion for common operations, see the specific Java driver and ORM section. Below table provides quick links for navigating to driver specific documentation and also the usage examples.

| Project | Type | Usage Examples |
| :------ | :--- | :------------- |
| [YugabyteDB JDBC Driver](/latest/reference/drivers/java/yugabyte-jdbc-reference/) | JDBC Driver | [Hello World](/latest/quick-start/build-apps/java/ysql-yb-jdbc) <br />[CRUD App](yugabyte-jdbc)
| [Postgres JDBC Driver](/latest/reference/drivers/java/postgres-jdbc-reference/) | JDBC Driver | [Hello World](/latest/quick-start/build-apps/java/ysql-jdbc) <br />[CRUD App](postgres-jdbc)
| [Hibernate](hibernate) | ORM |  [Hello World](/latest/quick-start/build-apps/java/ysql-jdbc) <br />[CRUD App](yugabyte-jdbc) |
| [Spring Data YugabyteDB](/latest/integrations/spring-framework/sdyb/) | Framework |  [Hello World](/latest/quick-start/build-apps/java/ysql-jdbc) <br />[CRUD App](yugabyte-jdbc) |

## Next Steps

- Learn how to read and modify data using the YugabyteDB JDBC driver in our [CRUD Opertions guide](yugabyte-jdbc).
- Learn how to [develop Spring Boot Applications using YugabyteDB JDBC Driver](/latest/integrations/spring-framework/sdyb/).
