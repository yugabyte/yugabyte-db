---
title: Connect an app
linkTitle: Connect an app
description: JDBC drivers for YSQL
menu:
  v2.14:
    identifier: yugabyte-jdbc-driver
    parent: java-drivers
    weight: 400
type: docs
---

For Java applications, the JDBC driver provides database connectivity through the standard JDBC application program interface (APIs) available on the Java platform. YugabyteDB supports the cluster- and topology-aware YugabyteDB Smart JDBC Driver, which is recommended for building Java applications with YugabyteDB. Yugabyte also provides full support for the [PostgreSQL JDBC Driver](https://jdbc.postgresql.org/).

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../yugabyte-jdbc/" class="nav-link active">
      <i class="fa-brands fa-java" aria-hidden="true"></i>
      YugabyteDB JDBC Driver
    </a>
  </li>

  <li >
    <a href="../postgres-jdbc/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL JDBC Driver
    </a>
  </li>

</ul>

[YugabyteDB JDBC driver](https://github.com/yugabyte/pgjdbc) is a distributed JDBC driver for [YSQL](../../../api/ysql/) built on the [PostgreSQL JDBC driver](https://github.com/pgjdbc/pgjdbc). Although the upstream PostgreSQL JDBC driver works with YugabyteDB, the Yugabyte driver enhances YugabyteDB by eliminating the need for external load balancers. This page provides details for getting started with `YugabyteDB JDBC Driver` for connecting to YugabyteDB YSQL API.

## Step 1: Set up the client dependencies

### Maven dependency

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

### Gradle dependency

If you are using [Gradle](https://docs.gradle.org/current/samples/sample_building_java_applications.html), add the following dependencies to your `build.gradle` file:

```java
implementation 'com.yugabyte:jdbc-yugabytedb:42.3.0'
implementation 'com.zaxxer:HikariCP:4.0.3'
```

## Step 2: Set up the database connection

After setting up the dependencies, implement the Java client application that uses the YugabyteDB JDBC driver to connect to your YugabyteDB cluster and run query on the sample data.

Set up the driver properties to configure the credentials and SSL Certificates for connecting to your cluster. Java Apps can connect to and query the YugabyteDB database using the `java.sql.DriverManager` class. All the JDBC interfaces required for working with YugabyteDB database are part of `java.sql.*` package.

Use the `DriverManager.getConnection` method for getting connection object for the YugabyteDB database, which can be used for performing DDLs and DMLs against the database.

The following is an example JDBC URL for connecting to YugabyteDB.

```java
String yburl = "jdbc:yugabytedb://hostname:port/database?user=yugabyte&password=yugabyte&load-balance=true";
DriverManager.getConnection(yburl);
```

| JDBC Parameter | Description | Default |
| :---------- | :---------- | :------ |
| hostname  | Hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| database | Database name | yugabyte
| user | User connecting to the database | yugabyte
| password | User password | yugabyte
| load-balance | Enables uniform load balancing | true

The following is an example JDBC URL for connecting to a YugabyteDB cluster with SSL encryption enabled.

```java
String yburl = "jdbc:yugabytedb://hostname:port/database?user=yugabyte&password=yugabyte&load-balance=true&ssl=true&sslmode=verify-full&sslrootcert=~/.postgresql/root.crt";
Connection conn = DriverManager.getConnection(yburl);
```

| JDBC Parameter | Description | Default |
| :---------- | :---------- | :------ |
| ssl  | Enable SSL client connection | false
| sslmode | SSL mode | require
| sslrootcert | Path to the root certificate on your computer | ~/.postgresql/

If you created a cluster on [YugabyteDB Managed](https://www.yugabyte.com/managed/), use the cluster credentials and [download the SSL Root certificate](/preview/yugabyte-cloud/cloud-connect/connect-applications/).

## Step 3: Write your application

Create a new Java class called `QuickStartApp.java` in the base package directory of your project. Copy the sample code below to set up a YugabyteDB table and query the table contents from the Java client. Be sure to replace the connection string `yburl` with credentials of your cluster and SSL certificate if required.

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

When you run the project, `QuickStartApp.java` should output something like the following:

```text
Connected to the YugabyteDB Cluster successfully.
Created table employee
Inserted data: INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'Java');
Query returned: name=John, age=35, language: Java
```

If you receive no output or an error, check the parameters in the connection string.

## Further reading

To learn more about the driver, refer to the [architecture documentation of Smart drivers](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/smart-driver.md).

## Next steps

- Learn how to build Java applications using [Hibernate ORM](../hibernate/).
- Learn more about configuring YugabyteDB JDBC Driver load balancing options in [YugabyteDB JDBC reference section](../../../reference/drivers/java/yugabyte-jdbc-reference/#load-balancing).
