---
title: JDBC Drivers
linkTitle: JDBC Drivers
description: JDBC Drivers for YSQL
headcontent: JDBC Drivers for YSQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  latest:
    name: JDBC Drivers
    identifier: postgres-jdbc-driver
    parent: java-drivers
    weight: 500
isTocNested: true
showAsideToc: true
---
For Java Applications, JDBC driver provides database connectivity through the standard JDBC application program interface (APIs) available on the Java platform. YugabyteDB supports `YugabyteDB Smart JDBC Driver` which supports cluster-awareness and topology-awareness. We recommend using `YugabyteDB Smart JDBC Driver` when building Java applications with YugabyteDB. Along with this, YugabyteDB has full support for [PostgreSQL JDBC Driver](https://jdbc.postgresql.org/).

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/latest/drivers-orms/java/yugabyte-jdbc/" class="nav-link">
      <i class="icon-java-bold" aria-hidden="true"></i>
      YugabyteDB JDBC Driver
    </a>
  </li>

  <li >
    <a href="/latest/drivers-orms/java/postgres-jdbc/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL JDBC Driver
    </a>
  </li>

</ul>

The [PostgreSQL JDBC driver](https://jdbc.postgresql.org/) is the official JDBC driver for PostgreSQL which can used for connecting to YugabyteDB YSQL. YugabyteDB YSQL has full compatiblity with PostgreSQL JDBC Driver, allows Java programmers to connect to YugabyteDB database to execute DMLs and DDLs using the JDBC APIs.

## Step 1: Add the PostgreSQL JDBC Driver Dependency

Postgres JDBC Drivers are available as maven dependency, you can download the driver by adding the following dependency into the java project.

### Maven Depedency

If you are using [Maven](https://maven.apache.org/guides/development/guide-building-maven.html), add the following to your `pom.xml` of your project.

```xml
<!-- https://mvnrepository.com/artifact/org.postgresql/postgresql -->
<dependency>
  <groupId>org.postgresql</groupId>
  <artifactId>postgresql</artifactId>
  <version>42.2.14</version>
</dependency>
```

### Gradle Dependency

If you are using [Gradle](https://docs.gradle.org/current/samples/sample_building_java_applications.html), add the following dependencies to your `build.gradle` file:

```java
// https://mvnrepository.com/artifact/org.postgresql/postgresql
implementation 'org.postgresql:postgresql:42.2.14'
```

## Step 2: Connect to your Cluster

After setting up the dependenices, we implement the Java client application that uses the [PostgreSQL JDBC driver](https://jdbc.postgresql.org/) to connect to your YugabyteDB cluster and run query on the sample data.

Java Apps can connect to and query the YugabyteDB database using the `java.sql.DriverManager` class. All the JDBC interfaces required for working with YugabyteDB database will be part of `java.sql.*` package.

Use the `DriverManager.getConnection` method for getting connection object for the YugabyteDB Database which can be used for performing DDLs and DMLs against the database.

Example PostgreSQL JDBC URL for connecting to YugabyteDB can be seen below.

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

Example JDBC URL for connecting to YugabyteDB cluster enabled with on the wire SSL encryption.

```java
string yburl = "jdbc://postgresql://hostname:port/database?user=yugabyte&password=yugabyte&ssl=true&sslmode=verify-full&sslrootcert=~/.postgresql/root.crt"
Connection conn = DriverManager.getConnection(yburl);
```

| JDBC Params | Description | Default |
| :---------- | :---------- | :------ |
| ssl  | Enable SSL client connection   | false
| sslmode | SSL mode  | require
| sslrootcert | path to the root certificate on your computer | ~/.postgresql/

If you have created Free tier cluster on [Yugabyte Anywhere](https://www.yugabyte.com/cloud/), [Follow the steps](/latest/yugabyte-cloud/cloud-connect/connect-applications/) to download the Credentials and SSL Root certificate.

## Step 3 -  Query the YugabyteDB Cluster from Your

Next, Create a new Java class called `QuickStartApp.java` in the base package directory of your project. Copy the sample code below in order to setup a YugabyteDB Table and query the Table contents from the java client. Ensure you replace the connection string `yburl` with credentials of your cluster and SSL certs if required.

```java

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
    String yburl = "jdbc:yugabytedb://127.0.0.1:5433/yugabyte?user=yugabyte&password=yugabyte";
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

## Next Steps

- Learn how to build Java Application using [Hibernate ORM](../hibernate).
- Learn how to [develop Spring Boot Applications using YugabyteDB JDBC Driver](/latest/integrations/spring-framework/sdyb/).
