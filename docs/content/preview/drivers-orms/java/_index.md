---
title: Java drivers, ORMs, and frameworks
headerTitle: Java
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

## Supported Projects

| Project | Documentation and Guides | Latest Driver Version | Supported YugabyteDB Version |
| ------- | ------------------------ | ------------------------ | ---------------------|
| PostgreSQL JDBC Driver | [Documentation](postgres-jdbc)<br />[Hello World App](/preview/quick-start/build-apps/java/ysql-jdbc)<br /> [Reference Page](../../reference/drivers/java/postgres-jdbc-reference.md) | [42.3.4](https://mvnrepository.com/artifact/org.postgresql/postgresql/42.2.14) | 2.4 and above
| YugabyteDB Smart Driver [Recommended] | [Documentation](yugabyte-jdbc)<br />[Hello World App](/preview/quick-start/build-apps/java/ysql-yb-jdbc)<br />[Blog](https://dev.to/yugabyte/yugabytedb-jdbc-smart-driver-for-proxyless-halb-2k8a)<br />[Reference Page](../../reference/drivers/java/yugabyte-jdbc-reference.md) | [42.3.4](https://mvnrepository.com/artifact/com.yugabyte/jdbc-yugabytedb/42.3.2) | 2.8 and above

| Project | Documentation and Guides | Example Apps |
| ------- | ------------------------ | ------------ |
| Hibernate | [Documentation](hibernate)<br />[Hello World App](/preview/quick-start/build-apps/java/ysql-hibernate)<br />[Blog](https://blog.yugabyte.com/run-the-rest-version-of-spring-petclinic-with-angular-and-distributed-sql-on-gke/)<br /> | [Hibernate ORM App](https://github.com/yugabyte/orm-examples/tree/master/java/hibernate)
| Spring Data YugabyteDB | [Documentation](../../integrations/spring-framework/sdyb.md)<br />[Hello World App](../../quick-start/build-apps/java/ysql-sdyb.md)<br />[Blog](https://blog.yugabyte.com/spring-data-yugabytedb-getting-started/) | [Spring Data YugabyteDB Sample App](https://github.com/yugabyte/spring-data-yugabytedb-example)
| Spring Data Jpa | [Documentation](../../integrations/spring-framework/sd-jpa.md)<br />[Hello World App](../../quick-start/build-apps/java/ysql-spring-data.md)<br />[Blog](https://blog.yugabyte.com/run-the-rest-version-of-spring-petclinic-with-angular-and-distributed-sql-on-gke/) | [Spring Data Jpa App](https://github.com/yugabyte/orm-examples/tree/master/java/spring)

<!-- 
| Project (* Recommended) | Type | Support | Examples |
| :------ | :--- | :------ | :------- |
| [YugabyteDB JDBC Driver*](yugabyte-jdbc) | JDBC Driver | Full | [Hello World](/preview/quick-start/build-apps/java/ysql-yb-jdbc) <br />[CRUD](yugabyte-jdbc) |
| [PostgreSQL JDBC Driver](/preview/reference/drivers/java/postgres-jdbc-reference/) | JDBC Driver | Full | [Hello World](/preview/quick-start/build-apps/java/ysql-jdbc) <br />[CRUD](postgres-jdbc)|
| [Hibernate*](hibernate) | ORM |  Full | [Hello World](/preview/quick-start/build-apps/java/ysql-hibernate) <br />[CRUD](hibernate/#working-with-domain-objects) |
| [Spring Data YugabyteDB*](/preview/integrations/spring-framework/sdyb/) | Framework |  Full | [Hello World](/preview/quick-start/build-apps/java/ysql-sdyb) |
-->
Learn how to establish a connection to a YugabyteDB database and begin basic CRUD operations using the **Hello World** examples.

For fully-runnable code snippets and explanations of common operations, see the **example apps**. Before running the example apps, make sure you have installed the prerequisites.

For reference documentation, including using projects with SSL, refer to the [drivers and ORMs reference](../../reference/drivers/java/yugabyte-jdbc-reference/) pages.

## Prerequisites

To develop Java applications for YugabyteDB, you need the following:

- **Java Development Kit (JDK)**\
  Install JDK 8 or later. For more information on how to check your version of Java and install the JDK, see the [AdoptOpenJDK Installation Page](https://adoptopenjdk.net/installation.html).

- **Create a Java project**\
  You can create Java projects using Maven or Gradle software project management tools. For ease-of-use, use an integrated development environment (IDE) such as IntelliJ IDEA or Eclipse IDE to configure Maven or Gradle to build and run your project.\
  If you are not using an IDE, see [Building Maven](https://maven.apache.org/guides/development/guide-building-maven.html) or [Creating New Gradle Projects](https://docs.gradle.org/current/samples/sample_building_java_applications.html) for more information on how to set up a Java project.

- **YugabyteDB cluster**
  - Create a free cluster on [YugabyteDB Managed](https://www.yugabyte.com/cloud/). Refer to [Create a free cluster](../../yugabyte-cloud/cloud-basics/create-clusters-free/). Note that YugabyteDB Managed requires SSL.
  - Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](/preview/quick-start/install/macos).

## Next Steps

- Learn how to build Java Application using [Hibernate ORM](../hibernate).
- Learn how to [develop Spring Boot Applications](/preview/integrations/spring-framework/sdyb/) using Spring Data YugabyteDB project.
