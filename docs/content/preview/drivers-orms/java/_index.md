---
title: Java drivers, ORMs, and frameworks
headerTitle: Java
linkTitle: Java
description: Java drivers and ORMs support for YugabyteDB.
aliases:
  - /preview/integrations/jdbc-drivers/
menu:
  preview:
    identifier: java-drivers
    parent: drivers-orms
    weight: 500
type: indexpage
showRightNav: true
---

## Supported projects

The following projects can be used to implement Java applications using the YugabyteDB YSQL and YCQL APIs.

| Driver | Documentation and Guides | Latest Driver Version | Supported YugabyteDB Version |
| ------- | ------------------------ | ------------------------ | ---------------------|
| YugabyteDB JDBC Driver [Recommended] | [Documentation](yugabyte-jdbc/)<br />[Reference](yugabyte-jdbc-reference/)<br />[Blog](https://dev.to/yugabyte/yugabytedb-jdbc-smart-driver-for-proxyless-halb-2k8a/) | [42.7.3-yb-1](https://mvnrepository.com/artifact/com.yugabyte/jdbc-yugabytedb/42.7.3-yb-1)<br/>(Supports Java 21) | 2.8 and above |
| YugabyteDB R2DBC Driver | [Documentation](yb-r2dbc/) | [1.1.0-yb-1-ea](https://mvnrepository.com/artifact/com.yugabyte/r2dbc-postgresql) | 2.18 and above |
| PostgreSQL JDBC Driver | [Documentation](postgres-jdbc/)<br /> [Reference](postgres-jdbc-reference/) | [42.3.4](https://mvnrepository.com/artifact/org.postgresql/postgresql/42.3.4) | 2.4 and above |
| Vert.x Pg Client | [Documentation](ysql-vertx-pg-client/) | [4.3.2](https://mvnrepository.com/artifact/io.vertx/vertx-core/4.3.2) | |
| YugabyteDB YCQL (3.10) Driver | [Documentation](ycql) | [3.10.3-yb-2](https://mvnrepository.com/artifact/com.yugabyte/cassandra-driver-core/3.10.3-yb-2) | |
| YugabyteDB YCQL (4.15) Driver | [Documentation](ycql-4.x) | [4.15.0-yb-1](https://mvnrepository.com/artifact/com.yugabyte/java-driver-core/4.15.0-yb-1) | |

| Projects | Documentation and Guides | Example Apps |
| ------- | ------------------------ | ------------ |
| Hibernate ORM | [Documentation](hibernate/)<br />[Hello World](../orms/java/ysql-hibernate/)<br />[Blog](https://www.yugabyte.com/blog/run-the-rest-version-of-spring-petclinic-with-angular-and-distributed-sql-on-gke/)<br /> | [Hibernate ORM App](https://github.com/yugabyte/orm-examples/tree/master/java/hibernate/) |
| Spring Data JPA | [Documentation](/preview/integrations/spring-framework/sd-jpa/)<br />[Hello World](../orms/java/ysql-spring-data/)<br />[Blog](https://www.yugabyte.com/blog/run-the-rest-version-of-spring-petclinic-with-angular-and-distributed-sql-on-gke/) | [Spring Data JPA App](https://github.com/yugabyte/orm-examples/tree/master/java/spring/) |
| Ebean ORM | [Documentation](ebean/)<br /> [Hello World](../orms/java/ysql-ebean/)<br /> [Blog](https://www.yugabyte.com/blog/ebean-orm-yugabytedb/)| [Ebean ORM App](https://github.com/yugabyte/orm-examples/tree/master/java/ebean/) |
| MyBatis ORM | [Documentation](mybatis/)<br /> [Hello World](../orms/java/ysql-mybatis/) | [MyBatis ORM App](https://github.com/yugabyte/orm-examples/tree/master/java/mybatis/) |
| Spring Data YugabyteDB | [Documentation](/preview/integrations/spring-framework/sdyb/)<br/>[Blog](https://www.yugabyte.com/blog/spring-data-yugabytedb-getting-started/) | [Spring Data YugabyteDB Sample App](https://github.com/yugabyte/spring-data-yugabytedb-example/) |

Learn how to establish a connection to a YugabyteDB database and begin basic CRUD operations by referring to [Connect an app](yugabyte-jdbc/) or [Use an ORM](hibernate/).

## Prerequisites

To develop Java driver applications for YugabyteDB, you need the following:

- **Java Development Kit (JDK)**

  Install JDK 8 or later. {{% jdk-setup %}}

- **Create a Java project**

  You can create Java projects using Maven or Gradle software project management tools. For ease-of-use, use an integrated development environment (IDE) such as IntelliJ IDEA or Eclipse IDE to configure Maven or Gradle to build and run your project.

  If you are not using an IDE, see [Building Maven](https://maven.apache.org/guides/development/guide-building-maven.html) or [Creating New Gradle Projects](https://docs.gradle.org/current/samples/sample_building_java_applications.html) for more information on how to set up a Java project.

    1. Create a project called "DriverDemo".

       ```sh
       $ mvn archetype:generate \
            -DgroupId=com.yugabyte \
            -DartifactId=DriverDemo \
            -DarchetypeArtifactId=maven-archetype-quickstart \
            -DinteractiveMode=false

       $ cd DriverDemo
       ```

    1. Open the pom.xml file in a text editor and add the following below the `<url>` element.

        ```xml
        <properties>
          <maven.compiler.source>1.8</maven.compiler.source>
          <maven.compiler.target>1.8</maven.compiler.target>
        </properties>
        ```

       If you're using Java 11, it should be:

       ```xml
        <properties>
          <maven.compiler.source>11</maven.compiler.source>
          <maven.compiler.target>11</maven.compiler.target>
        </properties>
        ```

- **YugabyteDB cluster**
  - Create a free cluster on YugabyteDB Aeon. Refer to [Use a cloud cluster](/preview/tutorials/quick-start-yugabytedb-managed/). Note that YugabyteDB Aeon requires SSL.
  - Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](/preview/tutorials/quick-start/).

## Next step

[Connect an app](yugabyte-jdbc/)
