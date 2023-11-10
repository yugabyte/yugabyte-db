---
title: Build a Java application that uses YSQL
headerTitle: Build a Java application
linkTitle: Java
description: Build a simple Java application using the YugabyteDB JDBC Driver and using the YSQL API to connect to and interact with a YugabyteDB Managed cluster.
headContent: "Client driver: Yugabyte JDBC"
menu:
  stable:
    parent: build-apps
    name: Java
    identifier: cloud-java
    weight: 100
type: docs
---

The following tutorial shows a small [Java application](https://github.com/yugabyte/yugabyte-simple-java-app) that connects to a YugabyteDB cluster using the topology-aware [YugabyteDB JDBC driver](../../../../reference/drivers/java/yugabyte-jdbc-reference/) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Managed in Java.

## Prerequisites

- Java Development Kit (JDK) 1.8, or later, is installed. {{% jdk-setup %}}
- [Apache Maven](https://maven.apache.org/index.html) 3.3 or later, is installed.

### Clone the application from GitHub

Clone the sample application to your computer:

```sh
git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-java-app.git && cd yugabyte-simple-java-app
```

## Provide connection parameters

If your cluster is running on YugabyteDB Managed, you need to modify the connection parameters so that the application can establish a connection to the YugabyteDB cluster. (You can skip this step if your cluster is running locally and listening on 127.0.0.1:5433.)

To do this:

1. Open the `app.properties` file located in the application `src/main/resources/` folder.

2. Set the following configuration parameters:

    - **host** - the host name of your YugabyteDB cluster. For local clusters, use the default (127.0.0.1). For YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
    - **port** - the port number for the driver to use (the default YugabyteDB YSQL port is 5433).
    - **database** - the name of the database you are connecting to (the default is `yugabyte`).
    - **dbUser** and **dbPassword** - the username and password for the YugabyteDB database. For local clusters, use the defaults (`yugabyte` and `yugabyte`). For YugabyteDB Managed, use the credentials in the credentials file you downloaded.
    - **sslMode** - the SSL mode to use. YugabyteDB Managed [requires SSL connections](../../../../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/); use `verify-full`.
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

The `main` method establishes a connection with your cluster via the topology-aware YugabyteDB JDBC driver.

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

[YugabyteDB JDBC driver](../../../../reference/drivers/java/yugabyte-jdbc-reference/)
