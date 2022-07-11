---
title: Build a Java application that uses YSQL
headerTitle: Build a Java application
linkTitle: Java
description: Build a simple Java application using the YugabyteDB JDBC Driver and using the YSQL API to connect to and interact with a YugabyteDB Managed cluster.
headContent: "Client driver: Yugabyte JDBC"
menu:
  preview_yugabyte-cloud:
    parent: cloud-build-apps
    name: Java
    identifier: cloud-java
    weight: 100
type: docs
---

The following tutorial shows a small [Java application](https://github.com/yugabyte/yugabyte-simple-java-app) that connects to a YugabyteDB cluster using the topology-aware [Yugabyte JDBC driver](../../../../integrations/jdbc-driver/) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Managed in Java.

## Prerequisites

This tutorial requires the following.

### YugabyteDB Managed

- You have a cluster deployed in YugabyteDB Managed. To get started, use the [Quick start](../../).
- You downloaded the cluster CA certificate and added your computer to the cluster IP allow list. Refer to [Before you begin](../cloud-add-ip/).

### Other packages

- Java Development Kit (JDK) 1.8, or later, is installed. JDK installers for Linux and macOS can be downloaded from [Oracle](http://jdk.java.net/), [Adoptium (OpenJDK)](https://adoptium.net/), or [Azul Systems (OpenJDK)](https://www.azul.com/downloads/?package=jdk). Homebrew users on macOS can install using `brew install openjdk`.
- [Apache Maven](https://maven.apache.org/index.html) 3.3 or later, is installed.

## Clone the application from GitHub

Clone the sample application to your computer:

```sh
git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-java-app.git && cd yugabyte-simple-java-app
```

## Provide connection parameters

The application needs to establish a connection to the YugabyteDB cluster. To do this:

1. Open the `app.properties` file located in the application `src/main/resources/` folder.

2. Set the following configuration parameters:

    - **host** - the host name of your YugabyteDB cluster. To obtain a YugabyteDB Managed cluster host name, sign in to YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
    - **port** - the port number that will be used by the JDBC driver (the default YugabyteDB YSQL port is 5433).
    - **dbUser** and **dbPassword** - the username and password for the YugabyteDB database. If you are using the credentials you created when deploying a cluster in YugabyteDB Managed, these can be found in the credentials file you downloaded.
    - **sslMode** - the SSL mode to use. YugabyteDB Managed [requires SSL connections](../../../cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql); use `verify-full`.
    - **sslRootCert** - the full path to the YugabyteDB Managed cluster CA certificate.

3. Save the file.

## Build and run the application

First build the application.

```sh
$ mvn clean package
```

Start the application.

```sh
$ java -cp target/yugabyte-simple-java-app-1.0-SNAPSHOT.jar SampleApp
```

If you are running the application on a Sandbox or single node cluster, the driver displays a warning that the load balance failed and will fall back to a regular connection.

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created DemoAccount table.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000

>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic Java application that works with YugabyteDB Managed.

## Explore the application logic

Open the `SampleApp.java` file in the application `/src/main/java/` folder to review the methods.

### main

The `main` method establishes a connection with your cluster via the topology-aware Yugabyte JDBC driver.

```java
YBClusterAwareDataSource ds = new YBClusterAwareDataSource();

ds.setUrl("jdbc:yugabytedb://" + settings.getProperty("host") + ":"
    + settings.getProperty("port") + "/yugabyte");
ds.setUser(settings.getProperty("dbUser"));
ds.setPassword(settings.getProperty("dbPassword"));

// Additional SSL-specific settings. See the source code for details.

Connection conn = ds.getConnection();
```

### createDatabase

The `createDatabase` method uses PostgreSQL-compliant DDL commands to create a sample database.

```java
Statement stmt = conn.createStatement();

stmt.execute("CREATE TABLE IF NOT EXISTS " + TABLE_NAME +
    "(" +
    "id int PRIMARY KEY," +
    "name varchar," +
    "age int," +
    "country varchar," +
    "balance int" +
    ")");

stmt.execute("INSERT INTO " + TABLE_NAME + " VALUES" +
    "(1, 'Jessica', 28, 'USA', 10000)," +
    "(2, 'John', 28, 'Canada', 9000)");
```

### selectAccounts

The `selectAccounts` method queries your distributed data using the SQL `SELECT` statement.

```java
Statement stmt = conn.createStatement();

ResultSet rs = stmt.executeQuery("SELECT * FROM " + TABLE_NAME);

while (rs.next()) {
    System.out.println(String.format("name = %s, age = %s, country = %s, balance = %s",
        rs.getString(2), rs.getString(3),
        rs.getString(4), rs.getString(5)));
}
```

### transferMoneyBetweenAccounts

The `transferMoneyBetweenAccounts` method updates your data consistently with distributed transactions.

```java
Statement stmt = conn.createStatement();

try {
    stmt.execute(
        "BEGIN TRANSACTION;" +
            "UPDATE " + TABLE_NAME + " SET balance = balance - " + amount + "" + " WHERE name = 'Jessica';" +
            "UPDATE " + TABLE_NAME + " SET balance = balance + " + amount + "" + " WHERE name = 'John';" +
            "COMMIT;"
    );
} catch (SQLException e) {
    if (e.getSQLState().equals("40001")) {
        System.err.println("The operation is aborted due to a concurrent transaction that is" +
            " modifying the same set of rows. Consider adding retry logic for production-grade applications.");
        e.printStackTrace();
    } else {
        throw e;
    }
}
```

## Learn more

[Yugabyte JDBC driver](../../../../integrations/jdbc-driver/)

[Explore more applications](../../../cloud-examples/)

[Sample Java application demonstrating load balancing](../../../../quick-start/build-apps/java/ysql-yb-jdbc/)

[Deploy clusters in YugabyteDB Managed](../../../cloud-basics)

[Connect to applications in YugabyteDB Managed](../../../cloud-connect/connect-applications/)
