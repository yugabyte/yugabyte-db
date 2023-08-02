---
title: Build a Java application that uses YSQL
headerTitle: Build a Java application
linkTitle: Java
description: Build a sample Java application with the YugabyteDB JDBC Driver and use the YSQL API to connect to and interact with YugabyteDB.
menu:
  v2.14:
    parent: build-apps
    name: Java
    identifier: java-1
    weight: 550
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../ysql-yb-jdbc/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - YB - JDBC
    </a>
  </li>
  <li >
    <a href="../ysql-jdbc/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - JDBC
    </a>
  </li>
  <li >
    <a href="../ysql-jdbc-ssl/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - JDBC SSL/TLS
    </a>
  </li>
  <li >
    <a href="../ysql-hibernate/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - Hibernate
    </a>
  </li>
  <li >
    <a href="../ysql-sdyb/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - Spring Data YugabyteDB
    </a>
  </li>
  <li >
    <a href="../ysql-spring-data/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - Spring Data JPA
    </a>
  </li>
  <li>
    <a href="../ysql-ebean/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - Ebean
    </a>
  </li>
  <li>
    <a href="../ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
  <li>
    <a href="../ycql-4.6/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL (4.6)
    </a>
  </li>
</ul>

## Prerequisites

This tutorial assumes that:

- YugabyteDB is up and running. Using the [yb-ctl](/preview/admin/yb-ctl/#root) utility, create a universe with a 3-node RF-3 cluster with some fictitious geo-locations assigned.

  ```sh
  $ cd <path-to-yugabytedb-installation>

  ./bin/yb-ctl create --rf 3 --placement_info "aws.us-west.us-west-2a,aws.us-west.us-west-2a,aws.us-west.us-west-2b"
  ```

- Java Development Kit (JDK) 1.8, or later, is installed. JDK installers can be downloaded from [OpenJDK](http://jdk.java.net/).
- [Apache Maven](https://maven.apache.org/index.html) 3.3 or later, is installed.

## Create and configure the Java project

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

1. Add the following dependencies for the driver HikariPool within the `<dependencies>` element in `pom.xml`.

    ```xml
    <dependency>
      <groupId>com.yugabyte</groupId>
      <artifactId>jdbc-yugabytedb</artifactId>
      <version>42.3.0</version>
    </dependency>

    <!-- https://mvnrepository.com/artifact/com.zaxxer/HikariCP -->
    <dependency>
      <groupId>com.zaxxer</groupId>
      <artifactId>HikariCP</artifactId>
      <version>5.0.0</version>
    </dependency>
    ```

1. Save and close `pom.xml`.

1. Install the added dependency.

    ```sh
    $ mvn install
    ```

## Create the sample Java application

You'll create two java applications, `UniformLoadBalance` and `TopologyAwareLoadBalance`. In each, you can create connections in two ways: using the `DriverManager.getConnection()` API, or using `YBClusterAwareDataSource` and `HikariPool`. This example shows both approaches.

### Uniform load balancing

1. Create a file called `./src/main/java/com/yugabyte/UniformLoadBalanceApp.java`.

    ```sh
    $ touch ./src/main/java/com/yugabyte/UniformLoadBalanceApp.java
    ```

1. Paste the following into `UniformLoadBalanceApp.java`:

    ```java
    package com.yugabyte;

    import com.zaxxer.hikari.HikariConfig;
    import com.zaxxer.hikari.HikariDataSource;

    import java.sql.Connection;
    import java.sql.DriverManager;
    import java.sql.SQLException;
    import java.util.ArrayList;
    import java.util.List;
    import java.util.Properties;
    import java.util.Scanner;

    public class UniformLoadBalanceApp {

      public static void main(String[] args) {
        makeConnectionUsingDriverManager();
        makeConnectionUsingYbClusterAwareDataSource();

        System.out.println("Execution of Uniform Load Balance Java App complete!!");
      }

      public static void makeConnectionUsingDriverManager() {
        //List to store the connections so that they can be closed at the end
        List<Connection> connectionList = new ArrayList<>();

        System.out.println("Lets create 6 connections using DriverManager");

        String yburl = "jdbc:yugabytedb://127.0.0.1:5433/yugabyte?user=yugabyte&password=yugabyte&load-balance=true";

        try {
          for(int i=0; i<6; i++) {
            Connection connection = DriverManager.getConnection(yburl);
            connectionList.add(connection);
          }

          System.out.println("You can verify the load balancing by visiting http://<host>:13000/rpcz as discussed before");
          System.out.println("Enter a integer to continue once verified:");
          int x = new Scanner(System.in).nextInt();

          System.out.println("Closing the connections!!");
          for(Connection connection : connectionList) {
             connection.close();
          }
        }
        catch (SQLException exception) {
          exception.printStackTrace();
        }
      }

      public static void makeConnectionUsingYbClusterAwareDataSource() {
        System.out.println("Now, Lets create 10 connections using YbClusterAwareDataSource and Hikari Pool");

        Properties poolProperties = new Properties();
        poolProperties.setProperty("dataSourceClassName", "com.yugabyte.ysql.YBClusterAwareDataSource");
        //the pool will create  10 connections to the servers
        poolProperties.setProperty("maximumPoolSize", String.valueOf(10));
        poolProperties.setProperty("dataSource.serverName", "127.0.0.1");
        poolProperties.setProperty("dataSource.portNumber", "5433");
        poolProperties.setProperty("dataSource.databaseName", "yugabyte");
        poolProperties.setProperty("dataSource.user", "yugabyte");
        poolProperties.setProperty("dataSource.password", "yugabyte");
        // If you want to provide additional end points
        String additionalEndpoints = "127.0.0.2:5433,127.0.0.3:5433";
        poolProperties.setProperty("dataSource.additionalEndpoints", additionalEndpoints);

        HikariConfig config = new HikariConfig(poolProperties);
        config.validate();
        HikariDataSource hikariDataSource = new HikariDataSource(config);

        System.out.println("Wait for some time for Hikari Pool to set up and create the connections...");
        System.out.println("You can verify the load balancing by visiting http://<host>:13000/rpcz as discussed before.");
        System.out.println("Enter a integer to continue once verified:");
        int x = new Scanner(System.in).nextInt();

        System.out.println("Closing the Hikari Connection Pool!!");
        hikariDataSource.close();

      }

    }
    ```

    {{< note title="Note">}}
When using `DriverManager.getConnection()`, you need to include the `load-balance=true` property in the connection URL. In the case of `YBClusterAwareDataSource`, load balancing is enabled by default.
    {{< /note >}}

1. Run the application.

    ```sh
    mvn -q package exec:java -DskipTests -Dexec.mainClass=com.yugabyte.UniformLoadBalanceApp
    ```

### Topology-aware load balancing

1. Create a file called `./src/main/java/com/yugabyte/TopologyAwareLoadBalanceApp.java`.

    ```sh
    $ touch ./src/main/java/com/yugabyte/TopologyAwareLoadBalanceApp.java
    ```

1. Paste the following into `TopologyAwareLoadBalanceApp.java`:

    ```java
    package com.yugabyte;

    import com.zaxxer.hikari.HikariConfig;
    import com.zaxxer.hikari.HikariDataSource;

    import java.sql.Connection;
    import java.sql.DriverManager;
    import java.sql.SQLException;
    import java.util.ArrayList;
    import java.util.List;
    import java.util.Properties;
    import java.util.Scanner;

    public class TopologyAwareLoadBalanceApp {

      public static void main(String[] args) {

        makeConnectionUsingDriverManager();
        makeConnectionUsingYbClusterAwareDataSource();

        System.out.println("Execution of Uniform Load Balance Java App complete!!");
      }

      public static void makeConnectionUsingDriverManager() {
        //List to store the connections so that they can be closed at the end
        List<Connection> connectionList = new ArrayList<>();

        System.out.println("Lets create 6 connections using DriverManager");
        String yburl = "jdbc:yugabytedb://127.0.0.1:5433/yugabyte?user=yugabyte&password=yugabyte&load-balance=true"
          + "&topology-keys=aws.us-west.us-west-2a";

        try {
          for(int i=0; i<6; i++) {
            Connection connection = DriverManager.getConnection(yburl);
            connectionList.add(connection);
          }

          System.out.println("You can verify the load balancing by visiting http://<host>:13000/rpcz as discussed before");
          System.out.println("Enter a integer to continue once verified:");
          int x = new Scanner(System.in).nextInt();

          System.out.println("Closing the connections!!");
          for(Connection connection : connectionList) {
            connection.close();
          }

        }
        catch (SQLException exception) {
          exception.printStackTrace();
        }

      }

      public static void makeConnectionUsingYbClusterAwareDataSource() {
        System.out.println("Now, Lets create 10 connections using YbClusterAwareDataSource and Hikari Pool");

        Properties poolProperties = new Properties();
        poolProperties.setProperty("dataSourceClassName", "com.yugabyte.ysql.YBClusterAwareDataSource");
        //the pool will create  10 connections to the servers
        poolProperties.setProperty("maximumPoolSize", String.valueOf(10));
        poolProperties.setProperty("dataSource.serverName", "127.0.0.1");
        poolProperties.setProperty("dataSource.portNumber", "5433");
        poolProperties.setProperty("dataSource.databaseName", "yugabyte");
        poolProperties.setProperty("dataSource.user", "yugabyte");
        poolProperties.setProperty("dataSource.password", "yugabyte");
        // If you want to provide additional end points
        String additionalEndpoints = "127.0.0.2:5433,127.0.0.3:5433";
        poolProperties.setProperty("dataSource.additionalEndpoints", additionalEndpoints);

        // If you want to load balance between specific geo locations using topology keys
        String geoLocations = "aws.us-west.us-west-2a";
        poolProperties.setProperty("dataSource.topologyKeys", geoLocations);


        HikariConfig config = new HikariConfig(poolProperties);
        config.validate();
        HikariDataSource hikariDataSource = new HikariDataSource(config);

        System.out.println("Wait for some time for Hikari Pool to set up and create the connections...");
        System.out.println("You can verify the load balancing by visiting http://<host>:13000/rpcz as discussed before.");
        System.out.println("Enter a integer to continue once verified:");
        int x = new Scanner(System.in).nextInt();

        System.out.println("Closing the Hikari Connection Pool!!");
        hikariDataSource.close();

      }

    }
    ```

    {{< note title="Note" >}}
When using `DriverManager.getConnection()`, you need to include the `load-balance=true` property in the connection URL. In the case of `YBClusterAwareDataSource`, load balancing is enabled by default, but you must set property `dataSource.topologyKeys`.
    {{< /note >}}

1. Run the application.

    ```sh
     mvn -q package exec:java -DskipTests -Dexec.mainClass=com.yugabyte.TopologyAwareLoadBalanceApp
    ```

## Explore the driver

Learn more about the [YugabyteDB JDBC driver](../../../../reference/drivers/java/yugabyte-jdbc-reference/) and explore the [demo apps](https://github.com/yugabyte/pgjdbc/tree/master/examples) to understand the driver's features in depth.
