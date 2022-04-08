---
title: Java
headerTitle: Java
linkTitle: Java
description: Java Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    identifier: java-drivers
    parent: drivers-orms
    weight: 540
isTocNested: true
showAsideToc: true
---
The following projects are recommended for implementing Java applications using the YugabyteDB YSQL API.

| Project | Type | Support Level |
| :------ | :--- | :------------ |
| [YugabyteDB JDBC Driver](yugabyte-jdbc) | JDBC Driver | Full |
| [Hibernate](hibernate) | ORM |  Full |
| [Spring Data YugabyteDB](/preview/integrations/spring-framework/sdyb/) | Framework |  Full |

## Build a Hello World application

Learn how to establish a connection to YugabyteDB database and begin simple CRUD operations using the steps in [Build an Application](/preview/quick-start/build-apps/java/ysql-yb-jdbc) in the Quick Start section.

## Pre-requisites for building a Java application

### Install the Java Development Kit (JDK)

Make sure that your system has JDK 8 or later installed. For more information on how to check your version of Java and install the JDK, see the [AdoptOpenJDK Installation Page](https://adoptopenjdk.net/installation.html).

### Create a Java project

Java projects can be created using Maven or Gradle Software project management tools. We recommend using an integrated development environment (IDE) such as Intellij IDEA or Eclipse IDE for conveniently configuring Maven or Gradle to build and run your project.

If you are not using an IDE, see [Building Maven](https://maven.apache.org/guides/development/guide-building-maven.html) or [Creating New Gradle Project](https://docs.gradle.org/current/samples/sample_building_java_applications.html) for more information on how to setup a Java project.

### Create a YugabyteDB cluster

Create a free cluster on Yugabyte Cloud. Refer to [Create a free cluster](../../yugabyte-cloud/cloud-basics/create-clusters-free/).

You can also set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](/preview/quick-start/install/macos).

## Examples

For fully runnable code snippets and explanations for common operations, see the specific Java driver and ORM section. The following table provides links to driver-specific documentation and examples.

| Project | Type | Usage Examples |
| :------ | :--- | :------------- |
| [YugabyteDB JDBC Driver](/preview/reference/drivers/java/yugabyte-jdbc-reference/) | JDBC Driver | [Hello World](/preview/quick-start/build-apps/java/ysql-yb-jdbc) <br />[CRUD App](yugabyte-jdbc)
| [PostgreSQL JDBC Driver](/preview/reference/drivers/java/postgres-jdbc-reference/) | JDBC Driver | [Hello World](/preview/quick-start/build-apps/java/ysql-jdbc) <br />[CRUD App](postgres-jdbc)
| [Hibernate](hibernate) | ORM |  [Hello World](/preview/quick-start/build-apps/java/ysql-jdbc) <br />[CRUD App](hibernate/#working-with-domain-objects) |

<!-- ## Learn More

- Learn about configuring load balancing options present in YugabyteDB JDBC Driver in [YugabyteDB JDBC reference section](/preview/reference/drivers/java/yugabyte-jdbc-reference/#load-balancing).
- Learn how to [develop Spring Boot Applications using Spring Data YugabyteDB project](/preview/integrations/spring-framework/sdyb/). -->
