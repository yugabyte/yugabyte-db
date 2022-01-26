---
title: Build a Java application that uses YSQL
headerTitle: Build a Java application
linkTitle: Java
description: Build a simple Java application using the YugabyteDB JDBC Driver and using the YSQL API to connect to and interact with a Yugabyte Cloud cluster.
menu:
  latest:
    parent: cloud-build-apps
    name: Java
    identifier: cloud-java-1
    weight: 550
type: page
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../ysql-yb-jdbc/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - YB - JDBC
    </a>
  </li>
</ul>

The application connects to your Yugabyte Cloud instance through the topology-aware [Yugabyte JDBC driver](../../../../../integrations/jdbc-driver/) and performs basic SQL operations. Use the application as a template to get started with Yugabyte Cloud in Java.

## Prerequisites

This tutorial assumes the following:

- You have a cluster deployed in Yugabte Cloud. To get started, use the [Quick start](../../../).
- You downloaded the cluster CA certificate. Refer to [Download your cluster certificate](../../../../cloud-secure-clusters/cloud-authentication/#download-your-cluster-certificate).
- You have added your computer to the cluster IP allow list. Refer to [Assign IP Allow Lists](../../../../cloud-secure-clusters/add-connections/).
- Java Development Kit (JDK) 1.8, or later, is installed. JDK installers for Linux and macOS can be downloaded from [OpenJDK](http://jdk.java.net/), [AdoptOpenJDK](https://adoptopenjdk.net/), or [Azul Systems](https://www.azul.com/downloads/zulu-community/).
- [Apache Maven](https://maven.apache.org/index.html) 3.3 or later, is installed.

## Clone the application from GitHub

Clone the sample application to your computer:

```sh
git clone https://github.com/yugabyte/simple-java-app-yugabyte-cloud.git && cd simple-java-app-yugabyte-cloud
```

## Provide Yugabyte Cloud settings

The application needs to establish a secure connection to your Yugabyte Cloud instance. To do this:

1. Open the `app.properties` file located in the application `src/main/resources/` folder.

2. Set the following configuration settings:

    - **host** - the host name of your Yugabyte Cloud instance. To obtain the cluster host name, sign in to Yugabyte Cloud, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Network Access**.
    - **port** - the port number that will be used by the JDBC driver (the default YugabyteDB YSQL port is 5433).
    - **dbUser** and **dbPassword** - the username and password for the YugabyteDB database. If you are using the default database you created when deploying the cluster, these can be found in the credentials file you downloaded.
    - **sslRootCert** - the full path to the cluster CA certificate

3. Save the file.

## Build and run the application

To build the application, run the following command.

```sh
$ mvn clean package
```

To start the application, run the following command.

```sh
java -cp target/simple-java-app-yugabyte-cloud-1.0-SNAPSHOT.jar SampleApp
```

If you are running the application on a free or single node cluster, the driver will display a warning that the load balance failed and will fall back to a regular connection.

You should then see output similar to the following:

```output
>>>> Successfully connected to Yugabyte Cloud.
>>>> Successfully created DemoAccount table.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000

>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a simple Java application that works with Yugabyte Cloud.

## Explore the application logic

Open the `SampleApp.java` file in the application `/src/main/java/` folder. The application has the following methods:

- **main** - establishes a connection with your cluster via the topology-aware Yugabyte JDBC driver.
- **createDatabase** - uses PostGres-compliant DDL commands to create a sample database.
- **selectAccounts** - queries your distributed data using the SQL `SELECT` statement.
- **transferMoneyBetweenAccounts** - updates your data consistently with distributed transactions.

## Learn more

[Yugabyte JDBC driver](../../../../../integrations/jdbc-driver/)

[Explore additional applications](../../../../cloud-develop)

[Sample Java application demonstrating load balancing](../../../../../quick-start/build-apps/java/ysql-yb-jdbc/)

[Deploy clusters in Yugabyte Cloud](../../../../cloud-basics)
