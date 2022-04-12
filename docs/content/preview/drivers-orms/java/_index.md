---
title: Java drivers, ORMs, and frameworks
headerTitle: Java
headcontent: Prerequisites and CRUD examples for building applications in Java.
linkTitle: Java
description: Java drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    identifier: java-drivers
    parent: drivers-orms
    weight: 540
isTocNested: true
showAsideToc: true
---
The following projects can be used to implement Java applications using the YugabyteDB YSQL API.

| Project (* Recommended) | Type | Support | Examples |
| :------ | :--- | :------ | :------- |
| [YugabyteDB JDBC Driver*](yugabyte-jdbc) | JDBC Driver | Full | [Hello World](/preview/quick-start/build-apps/java/ysql-yb-jdbc) <br />[CRUD](yugabyte-jdbc) |
| [PostgreSQL JDBC Driver](/preview/reference/drivers/java/postgres-jdbc-reference/) | JDBC Driver | Full | [Hello World](/preview/quick-start/build-apps/java/ysql-jdbc) <br />[CRUD](postgres-jdbc)|
| [Hibernate*](hibernate) | ORM |  Full | [Hello World](/preview/quick-start/build-apps/java/ysql-hibernate) <br />[CRUD](hibernate/#working-with-domain-objects) |
| [Spring Data YugabyteDB*](/preview/integrations/spring-framework/sdyb/) | Framework |  Full | [Hello World](/preview/quick-start/build-apps/java/ysql-sdyb) |

Learn how to establish a connection to a YugabyteDB database and begin basic CRUD operations using the **Hello World** examples.

For fully-runnable code snippets and explanations of common operations, see the project page **CRUD** example. Before running CRUD examples, make sure you have installed the prerequisites.

For reference documentation, including using projects with SSL, refer to the [drivers and ORMs reference](../../reference/drivers/java/yugabyte-jdbc-reference/) pages.

## Prerequisites

To develop Java applications for YugabyteDB, you need the following:

- **Java Development Kit (JDK)**\
  Install JDK 8 or later. For more information on how to check your version of Java and install the JDK, see the [AdoptOpenJDK Installation Page](https://adoptopenjdk.net/installation.html).

- **Create a Java project**\
  You can create Java projects using Maven or Gradle software project management tools. For ease-of-use, use an integrated development environment (IDE) such as IntelliJ IDEA or Eclipse IDE to configure Maven or Gradle to build and run your project.\
  If you are not using an IDE, see [Building Maven](https://maven.apache.org/guides/development/guide-building-maven.html) or [Creating New Gradle Projects](https://docs.gradle.org/current/samples/sample_building_java_applications.html) for more information on how to set up a Java project.

- **YugabyteDB cluster**
  - Create a free cluster on [Yugabyte Cloud](https://www.yugabyte.com/cloud/). Refer to [Create a free cluster](../../yugabyte-cloud/cloud-basics/create-clusters-free/). Note that Yugabyte Cloud requires SSL.
  - Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](/preview/quick-start/install/macos).

<!-- ## Learn More

- Learn about configuring load balancing options present in YugabyteDB JDBC Driver in [YugabyteDB JDBC reference section](/preview/reference/drivers/java/yugabyte-jdbc-reference/#load-balancing).
- Learn how to [develop Spring Boot Applications using Spring Data YugabyteDB project](/preview/integrations/spring-framework/sdyb/). -->
