---
title: PostgreSQL JDBC driver for YSQL
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect a Java application using PostgreSQL JDBC driver
menu:
  v2.20:
    identifier: postgres-jdbc-driver
    parent: java-drivers
    weight: 500
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li class="active">
    <a href="../yugabyte-jdbc/" class="nav-link">
      YSQL
    </a>
  </li>
  <li>
    <a href="../ycql/" class="nav-link">
      YCQL
    </a>
  </li>
</ul>

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../yugabyte-jdbc/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YugabyteDB JDBC Smart Driver
    </a>
  </li>

  <li >
    <a href="../postgres-jdbc/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL JDBC Driver
    </a>
  </li>

   <li >
    <a href="../ysql-vertx-pg-client/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      Vert.x Pg Client
    </a>
  </li>

</ul>

The [PostgreSQL JDBC driver](https://jdbc.postgresql.org/) is the official JDBC driver for PostgreSQL, and can be used for connecting to YugabyteDB YSQL. YSQL has full compatibility with the PostgreSQL JDBC Driver, and allows Java programmers to connect to YugabyteDB databases to execute DMLs and DDLs using the standard JDBC APIs.

For Java applications, the JDBC driver provides database connectivity through the standard JDBC application program interface (APIs) available on the Java platform.

## CRUD operations

The following sections demonstrate how to perform common tasks required for Java application development.

To start building your application, make sure you have met the [prerequisites](../#prerequisites).

If you're building the application with SSL, do the following additional steps:

- Set up SSL/TLS depending on the platform you choose to create your local cluster. To set up a cluster in Minikube with SSL/TLS, see [SSL certificates for a cluster in Kubernetes](../../../reference/drivers/java/postgres-jdbc-reference/#ssl-certificates-for-a-cluster-in-kubernetes-optional). To set up SSL certificates for a local cluster, see [Set up SSL certificates for Java applications](../../../reference/drivers/java/postgres-jdbc-reference/#set-up-ssl-certificates-for-java-applications).
- Install [OpenSSL](https://www.openssl.org/) 1.1.1 or later.

### Step 1: Set up the client dependency

PostgreSQL JDBC Drivers are available as a maven dependency, and you can download the driver by adding the following dependency to the Java project.

#### Maven dependency

If you are using [Maven](https://maven.apache.org/guides/development/guide-building-maven.html), add the following to your `pom.xml` of your project.

```xml
<!-- https://mvnrepository.com/artifact/org.postgresql/postgresql -->
<dependency>
  <groupId>org.postgresql</groupId>
  <artifactId>postgresql</artifactId>
  <version>42.2.14</version>
</dependency>
```

#### Gradle dependency

If you are using [Gradle](https://docs.gradle.org/current/samples/sample_building_java_applications.html), add the following dependencies to your `build.gradle` file:

```java
// https://mvnrepository.com/artifact/org.postgresql/postgresql
implementation 'org.postgresql:postgresql:42.2.14'
```

Install the added dependency using `mvn install`.

### Step 2: Set up the database connection

After setting up the dependencies, implement a Java client application that uses the PostgreSQL JDBC driver to connect to your YugabyteDB cluster and run a query on the sample data.

Java applications can connect to and query the YugabyteDB database using the `java.sql.DriverManager` class. The `java.sql.*` package includes all the JDBC interfaces required for working with YugabyteDB.

Use the `DriverManager.getConnection` method to create a connection object for the YugabyteDB Database. This can be used to perform DDLs and DMLs against the database.


| JDBC parameter | Description | Default |
| :------------- | :---------- | :------ |
| hostname | Hostname of the YugabyteDB instance | localhost |
| port | Listen port for YSQL | 5433 |
| database | Database name | yugabyte |
| user | User connecting to the database | yugabyte |
| password | User password | yugabyte |

Following is the PostgreSQL JDBC URL format for connecting to YugabyteDB:

```sh
jdbc:postgresql://hostname:port/database
```

Following is an example JDBC URL for connecting to YugabyteDB:

```sh
String yburl = "jdbc:postgresql://localhost:5433/yugabyte";
Connection conn = DriverManager.getConnection(yburl, "yugabyte", "yugabyte");
```

#### Use SSL

The following table describes the connection parameters required to connect using SSL.

| JDBC parameter | Description | Default |
| :------------- | :---------- | :------ |
| ssl | Enable SSL client connection | false |
| sslmode | SSL mode | require |
| sslrootcert | Path to the root certificate on your computer | ~/.postgresql/ |

The following is an example JDBC URL for connecting to a YugabyteDB cluster with SSL encryption enabled.

```java
String yburl = "jdbc:postgresql://hostname:port/database?user=yugabyte&password=yugabyte&ssl=true&sslmode=verify-full&sslrootcert=~/.postgresql/root.crt";
Connection conn = DriverManager.getConnection(yburl);
```

If you created a cluster on [YugabyteDB Managed](https://www.yugabyte.com/cloud/), use the cluster credentials and [download the SSL Root certificate](../../../yugabyte-cloud/cloud-connect/connect-applications/).

### Step 3: Write your application

Create a new Java class called `QuickStartApp.java` in the base package directory of your project as follows:

```sh
touch ./src/main/java/com/yugabyte/QuickStartApp.java
```

Copy the following code to set up a YugabyteDB table and query the table contents from the Java client. Be sure to replace the connection string `yburl` with credentials of your cluster and SSL certificate if required.

```java
package com.yugabyte;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.SQLException;
import java.sql.Statement;
import java.sql.ResultSet;

public class QuickStartApp {
  public static void main(String[] args) throws ClassNotFoundException, SQLException {
    Class.forName("org.postgresql.Driver");
    String yburl = "jdbc:postgresql://localhost:5433/yugabyte";
    Connection conn = DriverManager.getConnection(yburl, "yugabyte", "yugabyte");
    Statement stmt = conn.createStatement();
    try {
        System.out.println("Connected to the PostgreSQL server successfully.");
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

If you're using SSL, replace the connection string `yburl` with the following code:

```java
String yburl = "jdbc:postgresql://localhost:5433/yugabyte?ssl=true&sslmode=require&sslcert=src/main/resources/ssl/yugabytedb.crt.der&sslkey=src/main/resources/ssl/yugabytedb.key.pk8";
Connection conn = DriverManager.getConnection(yburl, "yugabyte", "yugabyte");
```

Run the project `QuickStartApp.java` using the following command:

```sh
mvn -q package exec:java -DskipTests -Dexec.mainClass=com.yugabyte.QuickStartApp
```

You should see output similar to the following:

```output
Connected to the YugabyteDB Cluster successfully.
Created table employee
Inserted data: INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'Java');
Query returned: name=John, age=35, language: Java
```

If there is no output or you get an error, verify that the connection string in your Java class has the correct parameters.

## Learn more

- [PostgreSQL JDBC driver reference](../../../reference/drivers/java/postgres-jdbc-reference/)
- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)
- [Develop Spring Boot applications using the YugabyteDB JDBC Driver](../../../integrations/spring-framework/sdyb/)
- Build Java applications using [Hibernate ORM](../hibernate/)
- Build Java applications using [Ebean ORM](../ebean/)
