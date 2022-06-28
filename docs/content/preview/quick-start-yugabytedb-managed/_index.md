---
title: YugabyteDB Managed quick start
headerTitle: Quick start
linkTitle: Quick start
description: Get started using YugabyteDB Managed in less than five minutes.
layout: single
type: docs
menu:
  preview_yugabyte-cloud:
    parent: yugabytedb-managed
    weight: 2
    params:
      hideLink: true
---

<div class="custom-tabs tabs-style-2">
  <ul class="tabs-name">
    <li class="active">
      <a href="../quick-start-yugabytedb-managed/" class="nav-link">
        Use a cloud cluster
      </a>
    </li>
    <li>
      <a href="../quick-start/" class="nav-link">
        Use a local cluster
      </a>
    </li>
  </ul>
</div>

The quickest way to get started with YugabyteDB is to [sign up for YugabyteDB Managed](http://cloud.yugabyte.com) and create a free cluster. Although not suitable for production workloads, the free cluster includes enough resources to learn and develop applications with YugabyteDB.

## Create a free cluster

The free cluster provides a fully functioning single node YugabyteDB cluster deployed to the region of your choice. The cluster is free forever and includes enough resources to explore the core features available for developing applications with YugabyteDB. No credit card information is required. You can only have one free cluster.

{{< youtube id="KZSrl12x1ew" title="Create your first YugabyteDB Managed cluster" >}}

After setting up your YugabyteDB Managed account, [log in](https://cloud.yugabyte.com/login) to access YugabyteDB Managed. YugabyteDB Managed has the following main sections, accessed via the left menu:

- **Getting Started** - Create your free cluster and access documentation.

- **Clusters** - Add, monitor, and manage clusters.

- **Alerts** - Configure cluster and billing alerts, and view notifications.

- **Network Access** - Set up VPC networks and authorize access to your clusters using IP allow lists.

- **Admin** - Manage billing and payment methods, add users, and review account activity.

### Create your free cluster

To create your free cluster:

1. On the **Getting Started** page, click **Create a free cluster** to open the **Create Cluster** wizard.

1. Select **YugabyteDB Managed Free** and click **Next**.

1. Choose the cloud provider (AWS or GCP), enter a name for the cluster, and choose the region in which to deploy the cluster, then click **Next**.

1. Click **Download credentials**. The default credentials are for a database user named "admin". You'll use these credentials when connecting to your YugabyteDB database.

1. Click **Create Cluster**.

After you complete the wizard, YugabyteDB Managed bootstraps and provisions the cluster, and configures YugabyteDB. The process takes up to 15 minutes.

When the cluster is ready, the cluster [Overview](../yugabyte-cloud/cloud-monitor/overview/) is displayed. You now have a fully configured YugabyteDB cluster provisioned in YugabyteDB Managed.

{{< note title="Note" >}}

YugabyteDB is a distributed database optimized for deployment across a cluster of servers. The free cluster has a single node and limited resources, suitable for running tutorials, [Yugabyte University](https://university.yugabyte.com), and [building sample applications](../yugabyte-cloud/cloud-quickstart/cloud-build-apps/). To evaluate YugabyteDB Managed for production use or conduct a proof-of-concept (POC), contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431) for trial credits.

{{< /note >}}

#### Learn more

[Differences between free and standard clusters](../yugabyte-cloud/cloud-faq/#what-are-the-differences-between-free-and-standard-clusters)

[Deploy production clusters](../yugabyte-cloud/cloud-basics/)

[Authorize access to your cluster](../yugabyte-cloud/cloud-secure-clusters/add-connections/)

## Connect to the cluster

After [creating a free cluster](#create-a-free-cluster), the easiest way to connect to it is to use Cloud Shell.

Using Cloud Shell, you can connect to your YugabyteDB Managed cluster from your browser, and interact with it using distributed SQL.

The shell has a one hour connection limit. If your session is idle for more than 5 minutes, it may disconnect. If your session expires, close your browser tab and connect again.

### Connect to your cluster using Cloud Shell

To connect to your cluster, do the following:

![Connect using cloud shell](/images/yb-cloud/cloud-connect-shell.gif)

1. On the **Clusters** page, ensure your cluster is selected.

1. Click **Connect** to display the **Connect to Cluster** dialog.

1. Under **Cloud Shell**, click **Launch Cloud Shell**.

1. Enter the database name (`yugabyte`), the user name (`admin`), select the YSQL API type, and click **Confirm**.

    Cloud Shell opens in a separate browser window. Cloud Shell can take up to 30 seconds to be ready.

    ```output
    Enter your DB password:
    ```

1. Enter the password for the admin user credentials that you saved when you created the cluster.\
\
    The shell prompt appears and is ready to use.

    ```output
    ysqlsh (11.2-YB-2.2.0.0-b0)
    SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
    Type "help" for help.

    yugabyte=>
    ```

The command line interface (CLI) being used is called ysqlsh. ysqlsh is the CLI for interacting with YugabyteDB using the PostgreSQL-compatible YSQL API.

Cloud Shell also supports ycqlsh, a CLI for the YCQL API.

#### Learn more

For more information on the ysqlsh and ycqlsh CLIs, refer to [ysqlsh](../admin/ysqlsh/) and [ycqlsh](../admin/ycqlsh/).

For more information on the YSQL and YCQL APIs, refer to [YSQL API](../api/ysql/) and [YCQL API](../api/ycql/).

For information on other ways to connect to your cluster, refer to [Connect to clusters](../yugabyte-cloud/cloud-connect).

## Build a Java application

The following tutorial shows a small [Java application](https://github.com/yugabyte/yugabyte-simple-java-app) that connects to a YugabyteDB cluster using the topology-aware [Yugabyte JDBC driver](../integrations/jdbc-driver/) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Managed in Java.

### Prerequisites

This tutorial requires the following.

#### YugabyteDB Managed

- You have a cluster deployed in YugabyteDB Managed. To get started, use the Quick start.
- You downloaded the cluster CA certificate and added your computer to the cluster IP allow list. Refer to [Before you begin](../yugabyte-cloud/cloud-quickstart/cloud-build-apps/cloud-add-ip/).

#### Other packages

- Java Development Kit (JDK) 1.8, or later, is installed. JDK installers for Linux and macOS can be downloaded from [Oracle](http://jdk.java.net/), [Adoptium (OpenJDK)](https://adoptium.net/), or [Azul Systems (OpenJDK)](https://www.azul.com/downloads/?package=jdk). Homebrew users on macOS can install using `brew install openjdk`.
- [Apache Maven](https://maven.apache.org/index.html) 3.3 or later, is installed.

### Clone the application from GitHub

Clone the sample application to your computer:

```sh
git clone https://github.com/yugabyte/yugabyte-simple-java-app.git && cd yugabyte-simple-java-app
```

### Provide connection parameters

The application needs to establish a connection to the YugabyteDB cluster. To do this:

1. Open the `app.properties` file located in the application `src/main/resources/` folder.

2. Set the following configuration parameters:

    - **host** - the host name of your YugabyteDB cluster. To obtain a YugabyteDB Managed cluster host name, sign in to YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Network Access**.
    - **port** - the port number that will be used by the JDBC driver (the default YugabyteDB YSQL port is 5433).
    - **dbUser** and **dbPassword** - the username and password for the YugabyteDB database. If you are using the credentials you created when deploying a cluster in YugabyteDB Managed, these can be found in the credentials file you downloaded.
    - **sslMode** - the SSL mode to use. YugabyteDB Managed [requires SSL connections](../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql); use `verify-full`.
    - **sslRootCert** - the full path to the YugabyteDB Managed cluster CA certificate.

3. Save the file.

### Build and run the application

First build the application.

```sh
$ mvn clean package
```

Start the application.

```sh
$ java -cp target/yugabyte-simple-java-app-1.0-SNAPSHOT.jar SampleApp
```

If you are running the application on a free or single node cluster, the driver displays a warning that the load balance failed and will fall back to a regular connection.

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

### Explore the application logic

Open the `SampleApp.java` file in the application `/src/main/java/` folder to review the methods.

#### main

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

#### createDatabase

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

#### selectAccounts

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

#### transferMoneyBetweenAccounts

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

[Yugabyte JDBC driver](../integrations/jdbc-driver/)

[Explore more applications](../yugabyte-cloud/cloud-examples/)

[Sample Java application demonstrating load balancing](../quick-start/build-apps/java/ysql-yb-jdbc/)

[Deploy clusters in YugabyteDB Managed](../yugabyte-cloud/cloud-basics/)

[Connect to applications in YugabyteDB Managed](../yugabyte-cloud/cloud-connect/connect-applications/)
