---
title: JDBC Drivers
linkTitle: JDBC Drivers
description: JDBC Drivers for YSQL
headcontent: JDBC Drivers for YSQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    name: JDBC Drivers
    identifier: yugabyte-jdbc-driver
    parent: java-drivers
    weight: 400
isTocNested: true
showAsideToc: true
---

For Java Applications, JDBC driver provides database connectivity through the standard JDBC application program interface (APIs) available on the Java platform. YugabyteDB supports `YugabyteDB Smart JDBC Driver` which supports cluster-awareness and topology-awareness. We recommend using `YugabyteDB Smart JDBC Driver` when building Java applications with YugabyteDB. Along with this, YugabyteDB has full support for [PostgreSQL JDBC Driver](https://jdbc.postgresql.org/).

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/preview/drivers-orms/java/yugabyte-jdbc/" class="nav-link active">
      <i class="icon-java-bold" aria-hidden="true"></i>
      YugabyteDB JDBC Driver
    </a>
  </li>

  <li >
    <a href="/preview/drivers-orms/java/postgres-jdbc/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL JDBC Driver
    </a>
  </li>

</ul>

This page provides details for getting started with `YugabyteDB JDBC Driver` for connecting to YugabyteDB YSQL API.

[Yugabyte JDBC driver](https://github.com/yugabyte/pgjdbc) is a distributed JDBC driver for [YSQL](/preview/api/ysql/) built on the [PostgreSQL JDBC driver](https://github.com/pgjdbc/pgjdbc).
Although the upstream PostgreSQL JDBC driver works with YugabyteDB, the Yugabyte driver enhances YugabyteDB by eliminating the need for external load balancers.

## Step 1: Add the YugabyteDB JDBC Driver Dependency

### Maven Dependency

If you are using [Maven](https://maven.apache.org/guides/development/guide-building-maven.html), add the following to your `pom.xml` of your project.

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
  <version>4.0.3</version>
</dependency>
```

### Gradle Dependency

If you are using [Gradle](https://docs.gradle.org/current/samples/sample_building_java_applications.html), add the following dependencies to your `build.gradle` file:

```java
implementation 'com.yugabyte:jdbc-yugabytedb:42.3.0'
implementation 'com.zaxxer:HikariCP:4.0.3'
```

## Step 2: Connect to your Cluster

After setting up the dependenices, we implement the Java client application that uses the YugabyteDB JDBC driver to connect to your YugabyteDB cluster and run query on the sample data.

We will setup the driver properties to configure the credentials and SSL Certificates for connecting to your cluster. Java Apps can connect to and query the YugabyteDB database using the `java.sql.DriverManager` class. All the JDBC interfaces required for working with YugabyteDB database will be part of `java.sql.*` package.

Use the `DriverManager.getConnection` method for getting connection object for the YugabyteDB Database which can be used for performing DDLs and DMLs against the database.

Example JDBC URL for connecting to YugabyteDB can be seen below.

```java
string yburl = "jdbc://yugabytedb://hostname:port/database?user=yugabyte&password=yugabyte&load-balance=true"
DriverManager.getConnection(yburl);
```

| JDBC Params | Description | Default |
| :---------- | :---------- | :------ |
| hostname  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| database | database name | yugabyte
| user | user for connecting to the database | yugabyte
| password | password for connecting to the database | yugabyte
| load-balance | enables uniform load balancing | true

Example JDBC URL for connecting to YugabyteDB cluster enabled with on the wire SSL encryption.

```java
string yburl = "jdbc://yugabytedb://hostname:port/database?user=yugabyte&password=yugabyte&load-balance=true&ssl=true&sslmode=verify-full&sslrootcert=~/.postgresql/root.crt"
Connection conn = DriverManager.getConnection(yburl);
```

| JDBC Params | Description | Default |
| :---------- | :---------- | :------ |
| ssl  | Enable SSL client connection   | false
| sslmode | SSL mode  | require
| sslrootcert | path to the root certificate on your computer | ~/.postgresql/

If you have created Free tier cluster on [Yugabyte Anywhere](https://www.yugabyte.com/cloud/), [Follow the steps](/preview/yugabyte-cloud/cloud-connect/connect-applications/) to download the Credentials and SSL Root certificate.

## Step 3: Query the YugabyteDB Cluster from Your Application

Next, Create a new Java class called `QuickStartApp.java` in the base package directory of your project. Copy the sample code below in order to setup a YugabyteDB Table and query the Table contents from the java client. Ensure you replace the connection string `yburl` with credentials of your cluster and SSL certs if required.

```java
import com.zaxxer.hikari.HikariConfig;
import com.zaxxer.hikari.HikariDataSource;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.List;
import java.util.Properties;
import java.util.Scanner;

public class QuickStartApp {
  public static void main(String[] args) throws ClassNotFoundException, SQLException {
    Class.forName("com.yugabyte.Driver");
    String yburl = "jdbc:yugabytedb://127.0.0.1:5433/yugabyte?user=yugabyte&password=yugabyte&load-balance=true";
    Connection conn = DriverManager.getConnection(yburl);
    Statement stmt = conn.createStatement();
    try {
        System.out.println("Connected to the YugabyteDB Cluster successfully.");
        stmt.execute("DROP TABLE IF EXISTS employee");
        stmt.execute("CREATE TABLE IF NOT EXISTS employee" +
                    "  (id int primary key, name varchar, age int, language text)");
        System.out.println("Created table employee");

        String insertStr = "INSERT INTO employee VALUES (1, 'John', 35, 'Java')";
        stmt.execute(insertStr);
        System.out.println("EXEC: " + insertStr);

        ResultSet rs = stmt.executeQuery("select * from employee");
        while (rs.next()) {
          System.out.println(String.format("Query returned: name = %s, age = %s, language = %s",
                                          rs.getString(2), rs.getString(3), rs.getString(4)));
        }
    } catch (SQLException e) {
      System.err.println(e.getMessage());
    }
  }
}
```

When you run the Project, `QuickStartApp.java` should output something like below:

```text
Connected to the YugabyteDB Cluster successfully.
Created table employee
Inserted data: INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'Java');
Query returned: name=John, age=35, language: Java
```

if you receive no output or error, check whether you included the proper connection string in your java class with the right credentials.

After completing this steps, you should have a working Java app that uses YugabyteDB JDBC driver for connecting to your cluster, setup tables, run query and print out results.

## Further Reading

To learn more about the driver, you can read the [architecture documentation of Smart Drivers](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/smart-driver.md).

## Next Steps

- Learn how to build Java Application using [Hibernate ORM](../hibernate).
- Learn more about configuring load balancing options present in YugabyteDB JDBC Driver in [YugabyteDB JDBC reference section](/preview/reference/drivers/java/yugabyte-jdbc-reference/#load-balancing).
