---
title: YugabyteDB Managed quick start
headerTitle: Quick start
linkTitle: Quick start
description: Get started using YugabyteDB Managed in less than five minutes.
layout: single
type: docs
aliases:
  - /preview/yugabyte-cloud/cloud-quickstart/
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

The quickest way to get started with YugabyteDB is to [sign up for YugabyteDB Managed](http://cloud.yugabyte.com) and create a free Sandbox cluster.

After setting up your YugabyteDB Managed account, [log in](https://cloud.yugabyte.com/login) to access YugabyteDB Managed.

The first time you sign in, YugabyteDB Managed provides a welcome experience with a 15 minute **Get Started** tutorial. Follow the steps to learn how to do the following:

- Create your Sandbox cluster
- Use YugabyteDB to create a database, load sample data, and run queries
- Explore a sample application that matches your use case

If you aren't using the **Get Started** tutorial, use the following instructions to create your first cluster and connect to your database.

## Create your Sandbox cluster

The Sandbox cluster provides a fully functioning single node YugabyteDB cluster deployed to the region of your choice. The cluster is free forever and includes enough resources to explore the core features available for developing applications with YugabyteDB. No credit card information is required.

>**Sandbox cluster**
>
>YugabyteDB is a distributed database optimized for deployment across a cluster of servers. The Sandbox cluster has a single node and limited resources, suitable for running tutorials, [Yugabyte University](https://university.yugabyte.com), and [building sample applications](../develop/build-apps/). See [Differences between Sandbox and Dedicated clusters](../faq/yugabytedb-managed-faq/#what-are-the-differences-between-sandbox-and-dedicated-clusters) for more information.
>
>To evaluate YugabyteDB Managed for production use or conduct a proof-of-concept (POC), contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431) for trial credits.

To create your Sandbox cluster:

![Create a Sandbox cluster](/images/yb-cloud/cloud-add-free-cluster.gif)

1. Click **Create a Free cluster** on the welcome screen, or click **Add Cluster** on the **Clusters** page to open the **Create Cluster** wizard.

1. Select Sandbox and click **Choose**.

1. Enter a name for the cluster, choose the cloud provider (AWS or GCP), and choose the region in which to deploy the cluster, then click **Next**.

    <!-- 1. Click **Add Current IP Address**. The IP address of your machine is added to the IP allow list. This allows you to connect to your sandbox cluster from applications and your desktop after it is created.

    1. Click **Next**. -->

1. Click **Download credentials**. The default credentials are for a database user named "admin". You'll use these credentials when connecting to your YugabyteDB database.

1. Click **Create Cluster**.

YugabyteDB Managed bootstraps and provisions the cluster, and configures YugabyteDB. The process takes around 5 minutes. While you wait, you can optionally fill out a survey to customize your getting started experience.

When the cluster is ready, the cluster [Overview](../yugabyte-cloud/cloud-monitor/overview/) is displayed. You now have a fully configured YugabyteDB cluster provisioned in YugabyteDB Managed.

## Connect to the cluster

Use Cloud Shell to connect to your YugabyteDB Managed cluster from your browser, and interact with it using distributed SQL.

>The shell has a one hour connection limit. If your session is idle for more than 5 minutes, it may disconnect. If your session expires, close your browser tab and connect again.

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

> The command line interface (CLI) being used is called [ysqlsh](../admin/ysqlsh/). ysqlsh is the CLI for interacting with YugabyteDB using the PostgreSQL-compatible [YSQL API](../api/ysql/). Cloud Shell also supports [ycqlsh](../admin/ycqlsh/), a CLI for the [YCQL API](../api/ycql/).
>
> For information on other ways to connect to your cluster, refer to [Connect to clusters](../yugabyte-cloud/cloud-connect).

## Explore distributed SQL

When you connect to your cluster using Cloud Shell with the [YSQL API](../api/ysql/) (the default), the shell window incorporates a **Quick Start Guide**, with a series of pre-built queries for you to run. Follow the prompts to explore YugabyteDB in 5 minutes.

![Run the quick start tutorial](/images/yb-cloud/cloud-shell-tutorial.gif)

## Build an application

Applications connect to and interact with YugabyteDB using API client libraries (also known as client drivers). The tutorials in this section show how to connect applications to YugabyteDB Managed clusters using your favorite programming language.

Before you begin, you need the following:

- a cluster deployed in YugabyteDB Managed.
- the cluster CA certificate; YugabyteDB Managed uses TLS to secure connections to the database.
- your computer added to the cluster IP allow list.

Refer to [Before you begin](../develop/build-apps/cloud-add-ip/).

### Choose your language

{{< tabpane code=false >}}

  {{% tab header="Java" lang="java" %}}

The following tutorial shows a small [Java application](https://github.com/yugabyte/yugabyte-simple-java-app) that connects to a YugabyteDB cluster using the topology-aware [Yugabyte JDBC driver](../drivers-orms/java/) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Managed in Java.

This tutorial requires the following:

- Java Development Kit (JDK) 1.8, or later, is installed. JDK installers for Linux and macOS can be downloaded from [Oracle](http://jdk.java.net/), [Adoptium (OpenJDK)](https://adoptium.net/), or [Azul Systems (OpenJDK)](https://www.azul.com/downloads/?package=jdk). Homebrew users on macOS can install using `brew install openjdk`.
- [Apache Maven](https://maven.apache.org/index.html) 3.3 or later, is installed.

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-java-app.git && cd yugabyte-simple-java-app
    ```

1. Provide connection parameters.

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    - Open the `app.properties` file located in the application `src/main/resources/` folder.

    - Set the following configuration parameters:

        - **host** - the host name of your YugabyteDB cluster. To obtain a YugabyteDB Managed cluster host name, sign in to YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **port** - the port number that will be used by the JDBC driver (the default YugabyteDB YSQL port is 5433).
        - **dbUser** and **dbPassword** - the username and password for the YugabyteDB database. If you are using the credentials you created when deploying a cluster in YugabyteDB Managed, these can be found in the credentials file you downloaded.
        - **sslMode** - the SSL mode to use. YugabyteDB Managed [requires SSL connections](../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql); use `verify-full`.
        - **sslRootCert** - the full path to the YugabyteDB Managed [cluster CA certificate](../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/).

    - Save the file.

1. Build the application.

    ```sh
    $ mvn clean package
    ```

1. Start the application.

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

[Explore the application logic](../develop/build-apps/java/cloud-ysql-yb-jdbc/#explore-the-application-logic)

  {{% /tab %}}

  {{% tab header="Go" lang="go" %}}

The following tutorial shows a small [Go application](https://github.com/yugabyte/yugabyte-simple-go-app) that connects to a YugabyteDB cluster using the [Go PostgreSQL driver](../drivers-orms/go/) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Managed in Go.

This tutorial requires the following:

- [Go](https://go.dev/dl/) (tested with version 1.17.6).

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-go-app.git && cd yugabyte-simple-go-app
    ```

1. Provide connection parameters.

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    1. Open the `sample-app.go` file.

    2. Set the following configuration parameter constants:

        - **host** - the host name of your YugabyteDB cluster. To obtain a YugabyteDB Managed cluster host name, sign in to YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **port** - the port number that will be used by the driver (the default YugabyteDB YSQL port is 5433).
        - **dbName** - the name of the database you are connecting to (the default database is named `yugabyte`).
        - **dbUser** and **dbPassword** - the username and password for the YugabyteDB database. If you are using the credentials you created when deploying a cluster in YugabyteDB Managed, these can be found in the credentials file you downloaded.
        - **sslMode** - the SSL mode to use. YugabyteDB Managed [requires SSL connections](../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql); use `verify-full`.
        - **sslRootCert** - the full path to the YugabyteDB Managed [cluster CA certificate](../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/).

    3. Save the file.

1. Initialize the `GO111MODULE` variable.

    ```sh
    $ export GO111MODULE=auto
    ```

1. Import the Go PostgreSQL driver.

    ```sh
    $ go get github.com/lib/pq
    ```

1. Start the application.

    ```sh
    $ go run sample-app.go
    ```

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic Go application that works with YugabyteDB Managed.

[Explore the application logic](../develop/build-apps/go/cloud-ysql-go/#explore-the-application-logic)

  {{% /tab %}}

  {{% tab header="Python" lang="python" %}}

The following tutorial shows a small [Python application](https://github.com/yugabyte/yugabyte-simple-python-app) that connects to a YugabyteDB cluster using the [Python psycopg2 PostgreSQL database adapter](../drivers-orms/python/) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Managed in Python.

This tutorial requires the following:

- Python 3.6 or later (Python 3.9.7 or later if running macOS on Apple silicon).

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-python-app.git && cd yugabyte-simple-python-app
    ```

1. Provide connection parameters.

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    1. Open the `sample-app.py` file.

    2. Set the following configuration parameter constants:

        - **host** - the host name of your YugabyteDB cluster. To obtain a YugabyteDB Managed cluster host name, sign in to YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **port** - the port number that will be used by the driver (the default YugabyteDB YSQL port is 5433).
        - **dbName** - the name of the database you are connecting to (the default database is named `yugabyte`).
        - **dbUser** and **dbPassword** - the username and password for the YugabyteDB database. If you are using the credentials you created when deploying a cluster in YugabyteDB Managed, these can be found in the credentials file you downloaded.
        - **sslMode** - the SSL mode to use. YugabyteDB Managed [requires SSL connections](../../../cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql); use `verify-full`.
        - **sslRootCert** - the full path to the YugabyteDB Managed cluster CA certificate.

    3. Save the file.

1. Install psycopg2 PostgreSQL database adapter.

    ```sh
    $ pip3 install psycopg2-binary
    ```

1. Start the application.

    ```sh
    $ python3 sample-app.py
    ```

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic Python application that works with YugabyteDB Managed.

[Explore the application logic](../develop/build-apps/python/cloud-ysql-python/#explore-the-application-logic)

  {{% /tab %}}

  {{% tab header="NodeJS" lang="nodejs" %}}

The following tutorial shows a small [Node.js application](https://github.com/yugabyte/yugabyte-simple-node-app) that connects to a YugabyteDB cluster using the [node-postgres driver](../drivers-orms/nodejs/) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Managed in Node.js.

This tutorial requires the following:

- The latest version of [Node.js](https://nodejs.org/en/download/).

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-node-app.git && cd yugabyte-simple-node-app
    ```

1. Provide connection parameters.

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    1. Open the `sample-app.js` file.

    2. Set the following configuration parameter constants:

        - **host** - the host name of your YugabyteDB cluster. To obtain a YugabyteDB Managed cluster host name, sign in to YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **port** - the port number that will be used by the driver (the default YugabyteDB YSQL port is 5433).
        - **database** - the name of the database you are connecting to (the default database is named `yugabyte`).
        - **user** and **password** - the username and password for the YugabyteDB database. If you are using the credentials you created when deploying a cluster in YugabyteDB Managed, these can be found in the credentials file you downloaded.
        - **ssl** - YugabyteDB Managed [requires SSL connections](../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql). To enable `verify-ca` SSL mode, the `rejectUnauthorized` property is set to `true` to require root certificate chain validation; replace `path_to_your_root_certificate` with the full path to the YugabyteDB Managed [cluster CA certificate](../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/).

    3. Save the file.

1. Install the node-postgres module.

    ```sh
    npm install pg
    ```

1. Install the [async](https://github.com/caolan/async) utility:

    ```sh
    npm install --save async
    ```

1. Start the application.

    ```sh
    $ node sample-app.js
    ```

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic Node.js application that works with YugabyteDB Managed.

[Explore the application logic](../develop/build-apps/nodejs/cloud-ysql-node/#explore-the-application-logic)

  {{% /tab %}}

  {{% tab header="C" lang="c" %}}

The following tutorial shows a small [C application](https://github.com/yugabyte/yugabyte-simple-c-app) that connects to a YugabyteDB cluster using the [libpq driver](../reference/drivers/ysql-client-drivers/#libpq) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Managed in C.

This tutorial requires the following:

- 32-bit (x86) or 64-bit (x64) architecture machine. (Use [Rosetta](https://support.apple.com/en-us/HT211861) to build and run on Apple silicon.)
- gcc 4.1.2 or later, or clang 3.4 or later installed.
- OpenSSL 1.1.1 or later (used by libpq to establish secure SSL connections).
- [libpq](../reference/drivers/ysql-client-drivers/#libpq). Homebrew users on macOS can install using `brew install libpq`. You can download the PostgreSQL binaries and source from [PostgreSQL Downloads](https://www.postgresql.org/download/).

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-c-app.git && cd yugabyte-simple-c-app
    ```

1. Provide connection parameters.

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    1. Open the `sample-app.c` file.

    2. Set the following configuration-related macros:

        - **HOST** - the host name of your YugabyteDB cluster. To obtain a YugabyteDB Managed cluster host name, sign in to YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **PORT** - the port number that will be used by the driver (the default YugabyteDB YSQL port is 5433).
        - **DB_NAME** - the name of the database you are connecting to (the default database is named `yugabyte`).
        - **USER** and **PASSWORD** - the username and password for the YugabyteDB database. If you are using the credentials you created when deploying a cluster in YugabyteDB Managed, these can be found in the credentials file you downloaded.
        - **SSL_MODE** - the SSL mode to use. YugabyteDB Managed [requires SSL connections](../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql); use `verify-full`.
        - **SSL_ROOT_CERT** - the full path to the YugabyteDB Managed [cluster CA certificate](../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/).

    3. Save the file.

1. Build the application with gcc or clang.

    ```sh
    gcc sample-app.c -o sample-app -I<path-to-libpq>/libpq/include -L<path-to-libpq>/libpq/lib -lpq
    ```

1. Replace `<path-to-libpq>` with the path to the libpq installation; for example, `/usr/local/opt`.

1. Start the application.

    ```sh
    $ ./sample-app
    ```

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic C application that works with YugabyteDB Managed.

[Explore the application logic](../develop/build-apps/c/cloud-ysql-c/#explore-the-application-logic)

  {{% /tab %}}

  {{% tab header="C++" lang="cpp" %}}

The following tutorial shows a small [C++ application](https://github.com/yugabyte/yugabyte-simple-cpp-app) that connects to a YugabyteDB cluster using the [libpqxx driver](../reference/drivers/ysql-client-drivers/#libpqxx) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Managed in C++.

This tutorial requires the following:

- 32-bit (x86) or 64-bit (x64) architecture machine. (Use [Rosetta](https://support.apple.com/en-us/HT211861) to build and run on Apple silicon.)
- gcc 4.1.2 or later, or clang 3.4 or later installed.
- OpenSSL 1.1.1 or later (used by libpq and libpqxx to establish secure SSL connections).
- [libpq](../reference/drivers/ysql-client-drivers/#libpq). Homebrew users on macOS can install using `brew install libpq`. You can download the PostgreSQL binaries and source from [PostgreSQL Downloads](https://www.postgresql.org/download/).
- [libpqxx](../reference/drivers/ysql-client-drivers/#libpqxx). Homebrew users on macOS can install using `brew install libpqxx`. To build the driver yourself, refer to [Building libpqxx](https://github.com/jtv/libpqxx#building-libpqxx).

To build and run the application, do the following:

1. Clone the sample application to your computer:

```sh
git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-cpp-app.git && cd yugabyte-simple-cpp-app
```

1. Provide connection parameters.

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    1. Open the `sample-app.cpp` file.

    2. Set the following configuration-related constants:

        - **HOST** - the host name of your YugabyteDB cluster. To obtain a YugabyteDB Managed cluster host name, sign in to YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **PORT** - the port number that will be used by the driver (the default YugabyteDB YSQL port is 5433).
        - **DB_NAME** - the name of the database you are connecting to (the default database is named `yugabyte`).
        - **USER** and **PASSWORD** - the username and password for the YugabyteDB database. If you are using the credentials you created when deploying a cluster in YugabyteDB Managed, these can be found in the credentials file you downloaded.
        - **SSL_MODE** - the SSL mode to use. YugabyteDB Managed [requires SSL connections](../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql); use `verify-full`.
        - **SSL_ROOT_CERT** - the full path to the YugabyteDB Managed [cluster CA certificate](../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/).

    3. Save the file.

1. Build the application with gcc or clang.

    ```sh
    g++ -std=c++17 sample-app.cpp -o sample-app -lpqxx -lpq \
    -I<path-to-libpq>/libpq/include -I<path-to-libpqxx>/libpqxx/include \
    -L<path-to-libpq>/libpq/lib -L<path-to-libpqxx>/libpqxx/lib
    ```

1. Replace `<path-to-libpq>` with the path to the libpq installation, and `<path-to-libpqxx>` with the path to the libpqxx installation; for example, `/usr/local/opt`.

1. Start the application.

    ```sh
    $ ./sample-app
    ```

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic C++ application that works with YugabyteDB Managed.

[Explore the application logic](../develop/build-apps/cpp/cloud-ysql-cpp/#explore-the-application-logic)

  {{% /tab %}}

  {{% tab header="C#" lang="csharp" %}}

The following tutorial shows a small [C# application](https://github.com/yugabyte/yugabyte-simple-csharp-app) that connects to a YugabyteDB cluster using the [Npgsql driver](../drivers-orms/csharp/) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Managed in C#.

This tutorial requires the following:

- [.NET 6.0 SDK](https://dotnet.microsoft.com/en-us/download) or later.

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-csharp-app.git && cd yugabyte-simple-csharp-app
    ```

    The `yugabyte-simple-csharp-app.csproj` file includes the following package reference to include the driver:

    ```cpp
    <PackageReference Include="npgsql" Version="6.0.3" />
    ```

1. Provide connection parameters.

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    1. Open the `sample-app.cs` file.

    2. Set the following configuration-related parameters:

        - **urlBuilder.Host** - the host name of your YugabyteDB cluster. To obtain a YugabyteDB Managed cluster host name, sign in to YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **urlBuilder.Port** - the port number that will be used by the driver (the default YugabyteDB YSQL port is 5433).
        - **urlBuilder.Database** - the name of the database you are connecting to (the default database is named `yugabyte`).
        - **urlBuilder.Username** and **urlBuilder.Password** - the username and password for the YugabyteDB database. If you are using the credentials you created when deploying a cluster in YugabyteDB Managed, these can be found in the credentials file you downloaded.
        - **urlBuilder.SslMode** - the SSL mode to use. YugabyteDB Managed [requires SSL connections](../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql); use `SslMode.VerifyFull`.
        - **urlBuilder.RootCertificate** - the full path to the YugabyteDB Managed [cluster CA certificate](../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/).

    3. Save the file.

1. Build and run the application.

    ```sh
    dotnet run
    ```

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic C# application that works with YugabyteDB Managed.

[Explore the application logic](../develop/build-apps/csharp/cloud-ysql-csharp/#explore-the-application-logic)

  {{% /tab %}}

  {{% tab header="Ruby" lang="ruby" %}}

The following tutorial shows a small [Ruby application](https://github.com/yugabyte/yugabyte-simple-ruby-app) that connects to a YugabyteDB cluster using the [Ruby Pg driver](../reference/drivers/ysql-client-drivers/#pg) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Managed in Ruby.

This tutorial requires the following:

- Ruby 3.1 or later.
- OpenSSL 1.1.1 or later (used by libpq and pg to establish secure SSL connections).
- [libpq](../reference/drivers/ysql-client-drivers/#libpq). Homebrew users on macOS can install using `brew install libpq`. You can download the PostgreSQL binaries and source from [PostgreSQL Downloads](https://www.postgresql.org/download/).
- [Ruby pg](../reference/drivers/ysql-client-drivers/#pg). To install Ruby pg, run the following command:

    ```sh
    gem install pg -- --with-pg-include=<path-to-libpq>/libpq/include --with-pg-lib=<path-to-libpq>/libpq/lib
    ```

    Replace `<path-to-libpq>` with the path to the libpq installation; for example, `/usr/local/opt`.

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-ruby-app.git && cd yugabyte-simple-ruby-app
    ```

1. Provide connection parameters.

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    1. Open the `sample-app.rb` file.

    2. Set the following configuration-related parameters:

        - **host** - the host name of your YugabyteDB cluster. To obtain a YugabyteDB Managed cluster host name, sign in to YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **port** - the port number that will be used by the driver (the default YugabyteDB YSQL port is 5433).
        - **dbname** - the name of the database you are connecting to (the default database is named `yugabyte`).
        - **user** and **password** - the username and password for the YugabyteDB database. If you are using the credentials you created when deploying a cluster in YugabyteDB Managed, these can be found in the credentials file you downloaded.
        - **sslmode** - the SSL mode to use. YugabyteDB Managed [requires SSL connections](../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql); use `verify-full`.
        - **sslrootcert** - the full path to the YugabyteDB Managed [cluster CA certificate](../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/).

    3. Save the file.

1. Make the application file executable.

    ```sh
    chmod +x sample-app.rb
    ```

1. Run the application.

    ```sh
    $ ./sample-app.rb
    ```

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic Ruby application that works with YugabyteDB Managed.

[Explore the application logic](../develop/build-apps/ruby/cloud-ysql-ruby/#explore-the-application-logic)

  {{% /tab %}}

  {{% tab header="Rust" lang="rust" %}}

The following tutorial shows a small [Rust application](https://github.com/yugabyte/yugabyte-simple-rust-app) that connects to a YugabyteDB cluster using the [Rust-Postgres driver](../reference/drivers/ysql-client-drivers/#rust-postgres) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Managed in Rust.

This tutorial requires the following:

- [Rust](https://www.rust-lang.org/tools/install) development environment. The sample application was created for Rust 1.58 but should work for earlier and later versions.

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-rust-app.git && cd yugabyte-simple-rust-app
    ```

1. Provide connection parameters.

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    1. Open the `sample-app.rs` file in the `src` directory.

    2. Set the following configuration-related constants:

        - **HOST** - the host name of your YugabyteDB cluster. To obtain a YugabyteDB Managed cluster host name, sign in to YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **PORT** - the port number that will be used by the driver (the default YugabyteDB YSQL port is 5433).
        - **DB_NAME** - the name of the database you are connecting to (the default database is named `yugabyte`).
        - **USER** and **PASSWORD** - the username and password for the YugabyteDB database. If you are using the credentials you created when deploying a cluster in YugabyteDB Managed, these can be found in the credentials file you downloaded.
        - **SSL_MODE** - the SSL mode to use. YugabyteDB Managed [requires SSL connections](../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql); use `SslMode::Require`.
        - **SSL_ROOT_CERT** - the full path to the YugabyteDB Managed [cluster CA certificate](../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/).

    3. Save the file.

1. Build and run the application.

    ```sh
    $ cargo run
    ```

The driver is included in the dependencies list of the `Cargo.toml` file and installed automatically the first time you run the application.

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic Rust application that works with YugabyteDB Managed.

[Explore the application logic](../develop/build-apps/rust/cloud-ysql-rust/#explore-the-application-logic)

  {{% /tab %}}

  {{% tab header="PHP" lang="php" %}}

The following tutorial shows a small [PHP application](https://github.com/yugabyte/yugabyte-simple-php-app) that connects to a YugabyteDB cluster using the [php-pgsql](../reference/drivers/ysql-client-drivers/#php-pgsql) driver and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Managed in PHP.

This tutorial requires the following:

- PHP runtime. The sample application was created using PHP 8.1 but should work with earlier and later versions. Homebrew users on macOS can install PHP using `brew install php`.
- [php-pgsql driver](../reference/drivers/ysql-client-drivers/#php-pgsql).
  - On macOS, Homebrew automatically installs the driver with `brew install php`.
  - Ubuntu users can install the driver using the `sudo apt-get install php-pgsql` command.
  - CentOS users can install the driver using the `sudo yum install php-pgsql` command.

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-php-app.git && cd yugabyte-simple-php-app
    ```

1. Provide connection parameters.

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    1. Open the `sample-app.php` file.

    1. Set the following configuration-related constants:

        - **HOST** - the host name of your YugabyteDB cluster. To obtain a YugabyteDB Managed cluster host name, sign in to YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **PORT** - the port number for the driver to use; this is already set to the default YugabyteDB YSQL port (5433).
        - **DB_NAME** - the name of the database to connect to (the default database is named `yugabyte`).
        - **USER** and **PASSWORD** - the username and password for the YugabyteDB database. If you are using the credentials you created when deploying a cluster in YugabyteDB Managed, these can be found in the credentials file you downloaded.
        - **SSL_MODE** - the SSL mode to use. YugabyteDB Managed [requires SSL connections](../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql); use `verify-full`.
        - **SSL_ROOT_CERT** - the full path to the YugabyteDB Managed [cluster CA certificate](../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/).

    1. Save the file.

1. Run the application.

    ```sh
    $ php sample-app.php
    ```

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic PHP application that works with YugabyteDB Managed.

[Explore the application logic](../develop/build-apps/php/cloud-ysql-php/#explore-the-application-logic)

  {{% /tab %}}

{{< /tabpane >}}

## Learn more

[Explore more applications](../develop/build-apps/)

[Drivers and ORMS](../drivers-orms/)

[Deploy clusters in YugabyteDB Managed](../yugabyte-cloud/cloud-basics/)

[Connect to applications in YugabyteDB Managed](../yugabyte-cloud/cloud-connect/connect-applications/)
