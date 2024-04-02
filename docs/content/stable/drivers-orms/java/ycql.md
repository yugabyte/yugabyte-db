---
title: Java Driver 3.10 for YCQL
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect a Java application using YCQL 3.10 driver
menu:
  stable:
    identifier: ycql-java-driver
    parent: java-drivers
    weight: 500
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../yugabyte-jdbc/" class="nav-link">
      YSQL
    </a>
  </li>
  <li class="active">
    <a href="../ycql/" class="nav-link">
      YCQL
    </a>
  </li>
</ul>

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YugabyteDB Java Driver for YCQL (3.10)
    </a>
  </li>
   <li >
    <a href="../ycql-4.x/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YugabyteDB Java Driver for YCQL (4.15)
    </a>
  </li>
</ul>

[YugabyteDB Java Driver for YCQL (3.10)](https://github.com/yugabyte/cassandra-java-driver) is based on [DataStax Java Driver 3.10](https://docs.datastax.com/en/developer/java-driver/3.10/) for [YCQL](../../../api/ycql/) with additional [smart driver](../../smart-drivers-ycql/) features.

{{< note title="YugabyteDB Managed" >}}

To use the driver's partition-aware load balancing feature in a YugabyteDB Managed cluster, applications must be deployed in a VPC that has been peered with the cluster VPC so that they have access to all nodes in the cluster. For more information, refer to [Using YCQL drivers with YugabyteDB Managed](../../smart-drivers-ycql/#using-ycql-drivers-with-yugabytedb-managed).

{{< /note >}}

## Maven

To build a sample Java application with the [Yugabyte Java Driver for YCQL](https://github.com/yugabyte/cassandra-java-driver), add the following Maven dependency to your application:

```xml
   <dependencies>
    <dependency>
      <groupId>com.yugabyte</groupId>
      <artifactId>cassandra-driver-core</artifactId>
      <version>3.10.3-yb-2</version>
    </dependency>
  </dependencies>
```

## Create the sample Java application

### Prerequisites

This tutorial assumes that you have:

- installed YugabyteDB, created a universe, and are able to interact with it using the YCQL shell. If not, follow the steps in [Quick start](../../../quick-start/).
- installed JDK version 1.8 or later.
- installed Maven 3.3 or later.

### Create the project's POM

Create a file, named `pom.xml`, and then copy the following content into it. The Project Object Model (POM) includes configuration information required to build the project.

```xml
<?xml version="1.0"?>
<project
  xsi:schemaLocation="http://maven.apache.org/POM/4.0.0 http://maven.apache.org/xsd/maven-4.0.0.xsd"
  xmlns="http://maven.apache.org/POM/4.0.0"
  xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <modelVersion>4.0.0</modelVersion>

  <groupId>com.yugabyte.sample.apps</groupId>
  <artifactId>hello-world</artifactId>
  <version>1.0</version>
  <packaging>jar</packaging>

  <dependencies>
    <dependency>
      <groupId>com.yugabyte</groupId>
      <artifactId>cassandra-driver-core</artifactId>
      <version>3.8.0-yb-5</version>
    </dependency>
  </dependencies>

  <build>
    <plugins>
      <plugin>
        <groupId>org.apache.maven.plugins</groupId>
        <artifactId>maven-dependency-plugin</artifactId>
        <version>2.1</version>
        <executions>
          <execution>
            <id>copy-dependencies</id>
            <phase>prepare-package</phase>
            <goals>
              <goal>copy-dependencies</goal>
            </goals>
            <configuration>
              <outputDirectory>${project.build.directory}/lib</outputDirectory>
              <overWriteReleases>true</overWriteReleases>
              <overWriteSnapshots>true</overWriteSnapshots>
              <overWriteIfNewer>true</overWriteIfNewer>
            </configuration>
          </execution>
        </executions>
      </plugin>
    </plugins>
  </build>
</project>
```

### Write a sample Java application

Create the appropriate directory structure as expected by Maven.

```sh
$ mkdir -p src/main/java/com/yugabyte/sample/apps
```

Copy the following contents into the file `src/main/java/com/yugabyte/sample/apps/YBCqlHelloWorld.java`.

```java
package com.yugabyte.sample.apps;

import java.util.List;
import com.datastax.driver.core.Cluster;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.Session;

public class YBCqlHelloWorld {
  public static void main(String[] args) {
    try {
      // Create a Cassandra client.
      Cluster cluster = Cluster.builder()
                               .addContactPoint("127.0.0.1")
                               .build();
      Session session = cluster.connect();

      // Create keyspace 'ybdemo' if it does not exist.
      String createKeyspace = "CREATE KEYSPACE IF NOT EXISTS ybdemo;";
      ResultSet createKeyspaceResult = session.execute(createKeyspace);
      System.out.println("Created keyspace ybdemo");

      // Create table 'employee' if it does not exist.
      String createTable = "CREATE TABLE IF NOT EXISTS ybdemo.employee (id int PRIMARY KEY, " +
                                                                       "name varchar, " +
                                                                       "age int, " +
                                                                       "language varchar);";
      ResultSet createResult = session.execute(createTable);
      System.out.println("Created table employee");

      // Insert a row.
      String insert = "INSERT INTO ybdemo.employee (id, name, age, language)" +
                                          " VALUES (1, 'John', 35, 'Java');";
      ResultSet insertResult = session.execute(insert);
      System.out.println("Inserted data: " + insert);

      // Query the row and print out the result.
      String select = "SELECT name, age, language FROM ybdemo.employee WHERE id = 1;";
      ResultSet selectResult = session.execute(select);
      List<Row> rows = selectResult.all();
      String name = rows.get(0).getString(0);
      int age = rows.get(0).getInt(1);
      String language = rows.get(0).getString(2);
      System.out.println("Query returned " + rows.size() + " row: " +
                         "name=" + name + ", age=" + age + ", language: " + language);

      // Close the client.
      session.close();
      cluster.close();
    } catch (Exception e) {
        System.err.println("Error: " + e.getMessage());
    }
  }
}
```

### Build the project

To build the project, run the following `mvn package` command.

```sh
$ mvn package
```

You should see a `BUILD SUCCESS` message.

### Run the application

To use the application, run the following command.

```sh
$ java -cp "target/hello-world-1.0.jar:target/lib/*" com.yugabyte.sample.apps.YBCqlHelloWorld
```

You should see the following as the output.

```output
Created keyspace ybdemo
Created table employee
Inserted data: INSERT INTO ybdemo.employee (id, name, age, language) VALUES (1, 'John', 35, 'Java');
Query returned 1 row: name=John, age=35, language: Java
```