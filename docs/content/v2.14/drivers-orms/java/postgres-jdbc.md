---
title: Connect an app
linkTitle: Connect an app
description: JDBC drivers for YSQL
menu:
  v2.14:
    identifier: postgres-jdbc-driver
    parent: java-drivers
    weight: 500
type: docs
---

For Java applications, the JDBC driver provides database connectivity through the standard JDBC application program interface (APIs) available on the Java platform. YugabyteDB supports the cluster- and topology-aware YugabyteDB Smart JDBC Driver, which is recommended for building Java applications with YugabyteDB. Yugabyte also provides full support for the [PostgreSQL JDBC Driver](https://jdbc.postgresql.org/).

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../yugabyte-jdbc/" class="nav-link">
      <i class="fa-brands fa-java" aria-hidden="true"></i>
      YugabyteDB JDBC Driver
    </a>
  </li>

  <li >
    <a href="../postgres-jdbc/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL JDBC Driver
    </a>
  </li>

</ul>

The [PostgreSQL JDBC driver](https://jdbc.postgresql.org/) is the official JDBC driver for PostgreSQL, and can be used for connecting to YugabyteDB YSQL. YugabyteDB YSQL has full compatibility with the PostgreSQL JDBC Driver, and allows Java programmers to connect to YugabyteDB databases to execute DMLs and DDLs using the JDBC APIs.

## Step 1: Add the PostgreSQL JDBC driver dependency

PostgreSQL JDBC Drivers are available as a maven dependency, and you can download the driver by adding the following dependency to the Java project.

### Maven dependency

If you are using [Maven](https://maven.apache.org/guides/development/guide-building-maven.html/), add the following to your `pom.xml` of your project.

```xml
<!-- https://mvnrepository.com/artifact/org.postgresql/postgresql -->
<dependency>
  <groupId>org.postgresql</groupId>
  <artifactId>postgresql</artifactId>
  <version>42.2.14</version>
</dependency>
```

### Gradle Dependency

If you are using [Gradle](https://docs.gradle.org/current/samples/sample_building_java_applications.html/), add the following dependencies to your `build.gradle` file:

```java
// https://mvnrepository.com/artifact/org.postgresql/postgresql
implementation 'org.postgresql:postgresql:42.2.14'
```

## Step 2: Connect to your cluster

After setting up the dependencies, implement a Java client application that uses the PostgreSQL JDBC driver to connect to your YugabyteDB cluster and run a query on the sample data.

Java applications can connect to and query the YugabyteDB database using the `java.sql.DriverManager` class. The `java.sql.*` package includes all the JDBC interfaces required for working with YugabyteDB.

Use the `DriverManager.getConnection` method to create a connection object for the YugabyteDB Database. This can be used to perform DDLs and DMLs against the database.

Example PostgreSQL JDBC URL for connecting to YugabyteDB can be seen below.

```java
jdbc:postgresql://hostname:port/database
```

Example JDBC URL for connecting to YugabyteDB can be seen below.

```java
Connection conn = DriverManager.getConnection("jdbc:postgresql://localhost:5433/yugabyte","yugabyte", "yugabyte");
```

| JDBC parameter | Description | Default |
| :------------- | :---------- | :------ |
| hostname | Hostname of the YugabyteDB instance | localhost |
| port | Listen port for YSQL | 5433 |
| database | Database name | yugabyte |
| user | Username for connecting to the database | yugabyte |
| password | Password for connecting to the database | yugabyte |

Example JDBC URL for connecting to YugabyteDB cluster enabled with on the wire SSL encryption.

```java
String yburl = "jdbc:postgresql://hostname:port/database?user=yugabyte&password=yugabyte&ssl=true&sslmode=verify-full&sslrootcert=~/.postgresql/root.crt";
Connection conn = DriverManager.getConnection(yburl);
```

| JDBC parameter | Description | Default |
| :------------- | :---------- | :------ |
| ssl | Enable SSL client connection | false |
| sslmode | SSL mode | require |
| sslrootcert | Path to the root certificate on your computer | ~/.postgresql/ |

If you created a cluster on YugabyteDB Managed, [follow the steps](../../../yugabyte-cloud/cloud-connect/connect-applications/) to download the database credentials and SSL Root certificate.

## Step 3 -  Query the YugabyteDB cluster from your application

Create a new Java class called `QuickStartApp.java` in the base package directory of your project. Copy the following sample code to set up a YugabyteDB table and query the table contents from the Java client. Replace the connection string `yburl` with your cluster credentials and SSL certificate if required.

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

When you run the Project, `QuickStartApp.java` should output something like the following:

```output
Connected to the YugabyteDB Cluster successfully.
Created table employee
Inserted data: INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'Java');
Query returned: name=John, age=35, language: Java
```

If there is no output or you get an error, verify that the connection string in your Java class has the correct parameters.

After completing these steps, you should have a working Java application that uses the PostgreSQL JDBC driver to connect to your cluster, set up tables, run queries, and print out results.

## Next Steps

- Learn how to build Java applications using [Hibernate ORM](../hibernate/).
- Learn how to [develop Spring Boot applications using the YugabyteDB JDBC Driver](/preview/integrations/spring-framework/sdyb/).
