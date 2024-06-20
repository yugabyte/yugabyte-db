---
title: JDBC smart driver for YSQL
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect a Java application using YugabyteDB JDBC Smart Driver
menu:
  stable:
    identifier: yugabyte-jdbc-driver
    parent: java-drivers
    weight: 400
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
    <a href="../yugabyte-jdbc/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YugabyteDB JDBC Smart Driver
    </a>
  </li>

  <li >
    <a href="../yb-r2dbc/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YugabyteDB R2DBC Smart Driver
    </a>
  </li>

  <li >
    <a href="../postgres-jdbc/" class="nav-link">
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

[YugabyteDB JDBC Smart Driver](https://github.com/yugabyte/pgjdbc) is a JDBC driver for [YSQL](../../../api/ysql/) built on the [PostgreSQL JDBC driver](https://github.com/pgjdbc/pgjdbc), with additional [connection load balancing](../../smart-drivers/) features.

For Java applications, the JDBC driver provides database connectivity through the standard JDBC application program interface (APIs) available on the Java platform.

{{< note title="YugabyteDB Managed" >}}

To use smart driver load balancing features when connecting to clusters in YugabyteDB Managed, applications must be deployed in a VPC that has been peered with the cluster VPC. For applications that access the cluster from outside the VPC network, use the upstream PostgreSQL driver instead; in this case, the cluster performs the load balancing. Applications that use smart drivers from outside the VPC network fall back to the upstream driver behaviour automatically. For more information, refer to [Using smart drivers with YugabyteDB Managed](../../smart-drivers/#using-smart-drivers-with-yugabytedb-managed).

{{< /note >}}

## CRUD operations

The following sections demonstrate how to perform common tasks required for Java application development.

To start building your application, make sure you have met the [prerequisites](../#prerequisites).

### Step 1: Set up the client dependencies

#### Maven dependency

If you are using [Maven](https://maven.apache.org/guides/development/guide-building-maven.html), add the following to your `pom.xml` of your project.

```xml
<dependency>
  <groupId>com.yugabyte</groupId>
  <artifactId>jdbc-yugabytedb</artifactId>
  <version>42.3.5-yb-5</version>
</dependency>

<!-- https://mvnrepository.com/artifact/com.zaxxer/HikariCP -->
<dependency>
  <groupId>com.zaxxer</groupId>
  <artifactId>HikariCP</artifactId>
  <version>4.0.3</version>
</dependency>
```

Install the added dependency using `mvn install`.

#### Gradle dependency

If you are using [Gradle](https://docs.gradle.org/current/samples/sample_building_java_applications.html), add the following dependencies to your `build.gradle` file:

```java
implementation 'com.yugabyte:jdbc-yugabytedb:42.3.5-yb-5'
implementation 'com.zaxxer:HikariCP:4.0.3'
```

### Step 2: Set up the database connection

After setting up the dependencies, implement the Java client application using the YugabyteDB JDBC driver to connect to your YugabyteDB cluster and run queries on the sample data.

Set up the driver properties to configure the credentials and SSL certificates for connecting to your cluster. Java applications can connect to and query the YugabyteDB database using the `java.sql.DriverManager` class. All the JDBC interfaces required for working with YugabyteDB database are part of the `java.sql.*` package.

Use the `DriverManager.getConnection` method to obtain the connection object for the YugabyteDB database, which can then be used to perform DDL and DML operations against the database.

The following table describes the connection parameters required to connect, including [smart driver parameters](../../smart-drivers/) for uniform and topology load balancing.

| JDBC Parameter | Description | Default |
| :------------- | :---------- | :------ |
| hostname  | Host name of the YugabyteDB instance. You can also enter [multiple addresses](#use-multiple-addresses). | localhost
| port |  Listen port for YSQL | 5433
| database | Database name | yugabyte
| user | User connecting to the database | yugabyte
| password | User password | yugabyte
| `load-balance` | [Uniform load balancing](../../smart-drivers/#cluster-aware-connection-load-balancing) | Defaults to upstream driver behavior unless set to 'true'
| `yb-servers-refresh-interval` | If `load_balance` is true, the interval in seconds to refresh the servers list | 300
| `topology-keys` | [Topology-aware load balancing](../../smart-drivers/#topology-aware-connection-load-balancing) | If `load-balance` is true, uses uniform load balancing unless set to comma-separated geo-locations in the form `cloud.region.zone`.

The following is an example JDBC URL for connecting to YugabyteDB:

```sh
jdbc:yugabytedb://hostname:port/database?user=yugabyte&password=yugabyte&load-balance=true& \
    yb-servers-refresh-interval=240& \
    topology-keys=cloud.region.zone1,cloud.region.zone2
```

After the driver establishes the initial connection, it fetches the list of available servers from the cluster, and load-balances subsequent connection requests across these servers.

#### Use multiple addresses

You can specify multiple hosts in the connection string to provide alternative options during the initial connection in case the primary address fails.

{{< tip title="Tip">}}
To obtain a list of available hosts, you can connect to any cluster node and use the `yb_servers()` YSQL function.
{{< /tip >}}

Delimit the addresses using commas, as follows:

```sh
jdbc:yugabytedb://hostname1:port,hostname2:port,hostname3:port/database?user=yugabyte&password=yugabyte&load-balance=true& \
    topology-keys=cloud.region.zone1,cloud.region.zone2
```

The hosts are only used during the initial connection attempt. If the first host is down when the driver is connecting, the driver attempts to connect to the next host in the string, and so on.

#### Use SSL

The following table describes the connection parameters required to connect using SSL.

| JDBC Parameter | Description | Default |
| :---------- | :---------- | :------ |
| ssl  | Enable SSL client connection | false
| sslmode | SSL mode | require
| sslrootcert | Path to the root certificate on your computer | ~/.postgresql/
| sslhostnameverifier | Address of host name verifier; only used for YugabyteDB Managed clusters where sslmode is verify-full. Driver v42.3.5-yb-2 and later only. | com.yugabyte.ysql.YBManagedHostnameVerifier

The following is an example JDBC URL for connecting to a YugabyteDB cluster with SSL encryption enabled.

```sh
jdbc:yugabytedb://hostname:port/database?user=yugabyte&password=yugabyte&load-balance=true& \
    ssl=true&sslmode=verify-full&sslrootcert=~/.postgresql/root.crt
```

If you created a cluster on [YugabyteDB Managed](https://www.yugabyte.com/managed/), use the cluster credentials and [download the SSL Root certificate](../../../yugabyte-cloud/cloud-connect/connect-applications/).

To use load balancing and SSL mode verify-full with a cluster in YugabyteDB Managed, you need to provide the additional `sslhostnameverifier` parameter, set to `com.yugabyte.ysql.YBManagedHostnameVerifier`. (Available in driver version 42.3.5-yb-2 or later. For previous versions of the driver, use `verify-ca`.)

### Step 3: Write your application

Create a new Java class called `QuickStartApp.java` in the base package directory of your project as follows:

```sh
touch ./src/main/java/com/yugabyte/QuickStartApp.java
```

Copy the following code to set up a YugabyteDB table and query the table contents from the Java client. Be sure to replace the connection string `yburl` with credentials of your cluster and SSL certificate if required.

```java
package com.yugabyte;

import com.zaxxer.hikari.HikariConfig;
import com.zaxxer.hikari.HikariDataSource;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.SQLException;
import java.sql.Statement;
import java.sql.ResultSet;

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

## Run the application

Run the project `QuickStartApp.java` using the following command:

```sh
mvn -q package exec:java -DskipTests -Dexec.mainClass=com.yugabyte.QuickStartApp
```

You should see output similar to the following:

```text
Connected to the YugabyteDB Cluster successfully.
Created table employee
Inserted data: INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'Java');
Query returned: name=John, age=35, language: Java
```

If you receive no output or an error, check the parameters in the connection string.

## Learn more

- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)
- Refer to [YugabyteDB JDBC driver reference](../../../reference/drivers/java/yugabyte-jdbc-reference/) and [Try it out](../../../reference/drivers/java/yugabyte-jdbc-reference/#try-it-out) for detailed smart driver examples.
- [Smart Driver architecture](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/smart-driver.md)
- [Develop Spring Boot applications using the YugabyteDB JDBC Driver](../../../integrations/spring-framework/sdyb/)
- Build Java applications using [Hibernate ORM](../hibernate/)
- Build Java applications using [Ebean ORM](../ebean/)
