---
title: YugabyteDB Quick Start
headerTitle: Quick start
linkTitle: Quick start
description: Get started using YugabyteDB in less than five minutes on macOS.
layout: single
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="/preview/tutorials/quick-start-yugabytedb-managed/" class="nav-link">
      Use a cloud cluster
    </a>
  </li>
  <li class="active">
    <a href="/preview/tutorials/quick-start/" class="nav-link">
      Use a local cluster
    </a>
  </li>
</ul>

Test YugabyteDB's APIs and core features by creating a local cluster on a single host.

The local cluster setup on a single host is intended for development and learning. For production deployment, performance benchmarking, or deploying a true multi-node on multi-host setup, see [Deploy YugabyteDB](../deploy/).

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li class="active">
    <a href="/preview/tutorials/quick-start/" class="nav-link">
      <i class="fa-brands fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a href="/preview/tutorials/quick-start/linux/" class="nav-link">
      <i class="fa-brands fa-linux" aria-hidden="true"></i>
      Linux
    </a>
  </li>
  <li>
    <a href="/preview/tutorials/quick-start/docker/" class="nav-link">
      <i class="fa-brands fa-docker" aria-hidden="true"></i>
      Docker
    </a>
  </li>
  <li>
    <a href="/preview/tutorials/quick-start/kubernetes/" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
</ul>

## Install YugabyteDB

Installing YugabyteDB involves completing [prerequisites](#prerequisites) and [downloading the packaged database](#download-yugabytedb).

### Prerequisites

Before installing YugabyteDB, ensure that you have the following available:

1. <i class="fa-brands fa-apple" aria-hidden="true"></i> macOS 10.12 or later.

1. Python 3. To check the version, execute the following command:

    ```sh
    python --version
    ```

    ```output
    Python 3.7.3
    ```

1. `wget` or `curl`.

    Note that the following instructions use the `wget` command to download files. If you prefer to use `curl` (included in macOS), you can replace `wget` with `curl -O`.

    To install `wget` on your Mac, you can run the following command if you use Homebrew:

    ```sh
    brew install wget
    ```

1. Since each tablet maps to its own file, it is easy to create a very large number of files in the current shell by experimenting with several hundred tables and several tablets per table. Execute the following command to ensure that the limit is set to a large number:

    ```sh
    launchctl limit
    ```

    It is recommended to have at least the following soft and hard limits:

    ```output
    maxproc     2500        2500
    maxfiles    1048576     1048576
    ```

    Edit `/etc/sysctl.conf`, if it exists, to include the following:

    ```sh
    kern.maxfiles=1048576
    kern.maxproc=2500
    kern.maxprocperuid=2500
    kern.maxfilesperproc=1048576
    ```

    If this file does not exist, create the following two files:

    - `/Library/LaunchDaemons/limit.maxfiles.plist` and insert the following:

      ```xml
      <?xml version="1.0" encoding="UTF-8"?>
      <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
      <plist version="1.0">
        <dict>
          <key>Label</key>
            <string>limit.maxfiles</string>
          <key>ProgramArguments</key>
            <array>
              <string>launchctl</string>
              <string>limit</string>
              <string>maxfiles</string>
              <string>1048576</string>
              <string>1048576</string>
            </array>
          <key>RunAtLoad</key>
            <true/>
          <key>ServiceIPC</key>
            <false/>
        </dict>
      </plist>
      ```

    - `/Library/LaunchDaemons/limit.maxproc.plist` and insert the following:

      ```xml
      <?xml version="1.0" encoding="UTF-8"?>
      <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
      <plist version="1.0">
        <dict>
          <key>Label</key>
            <string>limit.maxproc</string>
          <key>ProgramArguments</key>
            <array>
              <string>launchctl</string>
              <string>limit</string>
              <string>maxproc</string>
              <string>2500</string>
              <string>2500</string>
            </array>
          <key>RunAtLoad</key>
            <true/>
          <key>ServiceIPC</key>
            <false/>
        </dict>
      </plist>
      ```


    Ensure that the `plist` files are owned by `root:wheel` and have permissions `-rw-r--r--`. To take effect, you need to reboot your computer or run the following commands:

      ```sh
    sudo launchctl load -w /Library/LaunchDaemons/limit.maxfiles.plist
    sudo launchctl load -w /Library/LaunchDaemons/limit.maxproc.plist
      ```

    You might need to `unload` the service before loading it.

### Download YugabyteDB

You download YugabyteDB as follows:

1. Download the YugabyteDB `tar.gz` file by executing the following `wget` command:

    ```sh
    wget https://downloads.yugabyte.com/releases/{{< yb-version version="stable">}}/yugabyte-{{< yb-version version="stable" format="build">}}-darwin-x86_64.tar.gz
    ```

1. Extract the package and then change directories to the YugabyteDB home, as follows:

    ```sh
    tar xvfz yugabyte-{{< yb-version version="stable" format="build">}}-darwin-x86_64.tar.gz && cd yugabyte-{{< yb-version version="stable">}}/
    ```

## Create a local cluster

To create a single-node local cluster with a replication factor (RF) of 1, run the following command:

```sh
./bin/yugabyted start
```

{{<note title="Note for macOS Monterey" >}}

macOS Monterey enables AirPlay receiving by default, which listens on port 7000. This conflicts with YugabyteDB and causes `yugabyted start` to fail. To resolve the issue, you can disable AirPlay receiving, then start YugabyteDB, and then, optionally, re-enable AirPlay receiving. Alternatively, you can change the default port number using the `--master_webserver_port` flag when you start the cluster as follows:

```sh
./bin/yugabyted start --master_webserver_port=9999
```

{{< /note >}}

After the cluster has been created, clients can connect to the YSQL and YCQL APIs at http://localhost:5433 and http://localhost:9042 respectively. You can also check `~/var/data` to see the data directory and `~/var/logs` to see the logs directory.

If you have previously installed YugabyteDB version 2.8 or later and created a cluster on the same computer, you may need to [upgrade the YSQL system catalog](../manage/upgrade-deployment/#upgrade-the-ysql-system-catalog) to run the latest features.

### Check cluster status

Execute the following command to check the cluster status:

```sh
./bin/yugabyted status
```

Expect an output similar to the following:


```output
+--------------------------------------------------------------------------------------------------+
|                                            yugabyted                                             |
+--------------------------------------------------------------------------------------------------+
| Status              : Running. Leader Master is present                                          |
| Web console         : http://127.0.0.1:7000                                                      |
| JDBC                : jdbc:postgresql://127.0.0.1:5433/yugabyte?user=yugabyte&password=yugabyte  |
| YSQL                : bin/ysqlsh   -U yugabyte -d yugabyte                                       |
| YCQL                : bin/ycqlsh   -u cassandra                                                  |
| Data Dir            : /Users/myuser/var/data                                                     |
| Log Dir             : /Users/myuser/var/logs                                                     |
| Universe UUID       : fad6c687-e1dc-4dfd-af4b-380021e19be3                                       |
+--------------------------------------------------------------------------------------------------+
```

### Check cluster status with Admin UI

The cluster you have created consists of two processes: [YB-Master](../architecture/concepts/yb-master/) which keeps track of various metadata (list of tables, users, roles, permissions, and so on) and [YB-TServer](../architecture/concepts/yb-tserver/) which is responsible for the actual end-user requests for data updates and queries.

Each of the processes exposes its own Admin UI that can be used to check the status of the corresponding process, as well as perform certain administrative operations. The [YB-Master Admin UI](../reference/configuration/yb-master/#admin-ui) is available at [http://127.0.0.1:7000](http://127.0.0.1:7000) (replace the port number if you've started `yugabyted` with the `--master_webserver_port` flag) and the [YB-TServer Admin UI](../reference/configuration/yb-tserver/#admin-ui) is available at [http://127.0.0.1:9000](http://127.0.0.1:9000).

#### Overview and YB-Master status

The following illustration shows the YB-Master home page with a cluster with a replication factor of 1, a single node, and no tables. The YugabyteDB version is also displayed.

![master-home](/images/admin/master-home-binary-rf1.png)

The **Masters** section displays the 1 YB-Master along with its corresponding cloud, region, and zone placement.

#### YB-TServer status

Click **See all nodes** to open the **Tablet Servers** page that lists the YB-TServer along with the time since it last connected to the YB-Master using regular heartbeats. Because there are no user tables, **User Tablet-Peers / Leaders** is 0. As tables are added, new tablets (also known as shards) will be created automatically and distributed evenly across all the available tablet servers.

![master-home](/images/admin/master-tservers-list-binary-rf1.png)

## Build a Java application

### Prerequisites

Before building a Java application, perform the following:

- While YugabyteDB is running, use the [yb-ctl](/preview/admin/yb-ctl/#root) utility to create a universe with a 3-node RF-3 cluster with some fictitious geo-locations assigned, as follows:

  ```sh
  cd <path-to-yugabytedb-installation>

  ./bin/yb-ctl create --rf 3 --placement_info "aws.us-west.us-west-2a,aws.us-west.us-west-2a,aws.us-west.us-west-2b"
  ```

- Ensure that Java Development Kit (JDK) 1.8 or later is installed. JDK installers can be downloaded from [OpenJDK](http://jdk.java.net/).
- Ensure that [Apache Maven](https://maven.apache.org/index.html) 3.3 or later is installed.

### Create and configure the Java project

Perform the following to create a sample Java project:

1. Create a project called DriverDemo, as follows:

    ```sh
    mvn archetype:generate \
        -DgroupId=com.yugabyte \
        -DartifactId=DriverDemo \
        -DarchetypeArtifactId=maven-archetype-quickstart \
        -DinteractiveMode=false

    cd DriverDemo
    ```

1. Open the `pom.xml` file in a text editor and add the following block below the `<url>` element:

    ```xml
    <properties>
      <maven.compiler.source>1.8</maven.compiler.source>
      <maven.compiler.target>1.8</maven.compiler.target>
    </properties>
    ```

1. Add the following dependencies for the driver HikariPool within the `<dependencies>` element in `pom.xml`:

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
      <version>5.0.0</version>
    </dependency>
    ```

1. Save and close the `pom.xml` file.

1. Install the added dependency by executing the following command:

    ```sh
    mvn install
    ```

### Create a sample Java application

The following steps demonstrate how to create two Java applications, `UniformLoadBalance` and `TopologyAwareLoadBalance`. In each, you can create connections in one of two ways: using the `DriverManager.getConnection()` API or using `YBClusterAwareDataSource` and `HikariPool`. Both approaches are described.

#### Uniform load balancing

1. Create a file called `./src/main/java/com/yugabyte/UniformLoadBalanceApp.java` by executing the following command:

    ```sh
    touch ./src/main/java/com/yugabyte/UniformLoadBalanceApp.java
    ```

1. Paste the following into `UniformLoadBalanceApp.java`:

    ```java
    package com.yugabyte;

    import com.zaxxer.hikari.HikariConfig;
    import com.zaxxer.hikari.HikariDataSource;

    import java.sql.Connection;
    import java.sql.DriverManager;
    import java.sql.SQLException;
    import java.util.ArrayList;
    import java.util.List;
    import java.util.Properties;
    import java.util.Scanner;

    public class UniformLoadBalanceApp {

      public static void main(String[] args) {
        makeConnectionUsingDriverManager();
        makeConnectionUsingYbClusterAwareDataSource();

        System.out.println("Execution of Uniform Load Balance Java App complete!!");
      }

      public static void makeConnectionUsingDriverManager() {
        // List to store the connections so that they can be closed at the end
        List<Connection> connectionList = new ArrayList<>();

        System.out.println("Lets create 6 connections using DriverManager");

        String yburl = "jdbc:yugabytedb://127.0.0.1:5433/yugabyte?user=yugabyte&password=yugabyte&load-balance=true";

        try {
          for(int i=0; i<6; i++) {
            Connection connection = DriverManager.getConnection(yburl);
            connectionList.add(connection);
          }

          System.out.println("You can verify the load balancing by visiting http://<host>:13000/rpcz as discussed before");
          System.out.println("Enter a integer to continue once verified:");
          int x = new Scanner(System.in).nextInt();

          System.out.println("Closing the connections!!");
          for(Connection connection : connectionList) {
             connection.close();
          }
        }
        catch (SQLException exception) {
          exception.printStackTrace();
        }
      }

      public static void makeConnectionUsingYbClusterAwareDataSource() {
        System.out.println("Now, Lets create 10 connections using YbClusterAwareDataSource and Hikari Pool");

        Properties poolProperties = new Properties();
        poolProperties.setProperty("dataSourceClassName", "com.yugabyte.ysql.YBClusterAwareDataSource");
        // The pool will create  10 connections to the servers
        poolProperties.setProperty("maximumPoolSize", String.valueOf(10));
        poolProperties.setProperty("dataSource.serverName", "127.0.0.1");
        poolProperties.setProperty("dataSource.portNumber", "5433");
        poolProperties.setProperty("dataSource.databaseName", "yugabyte");
        poolProperties.setProperty("dataSource.user", "yugabyte");
        poolProperties.setProperty("dataSource.password", "yugabyte");
        // If you want to provide additional end points
        String additionalEndpoints = "127.0.0.2:5433,127.0.0.3:5433";
        poolProperties.setProperty("dataSource.additionalEndpoints", additionalEndpoints);

        HikariConfig config = new HikariConfig(poolProperties);
        config.validate();
        HikariDataSource hikariDataSource = new HikariDataSource(config);

        System.out.println("Wait for some time for Hikari Pool to set up and create the connections...");
        System.out.println("You can verify the load balancing by visiting http://<host>:13000/rpcz as discussed before.");
        System.out.println("Enter a integer to continue once verified:");
        int x = new Scanner(System.in).nextInt();

        System.out.println("Closing the Hikari Connection Pool!!");
        hikariDataSource.close();

      }

    }
    ```

    When using `DriverManager.getConnection()`, you need to include the `load-balance=true` property in the connection URL. In the case of `YBClusterAwareDataSource`, load balancing is enabled by default.

1. Run the application, as follows:

    ```sh
    mvn -q package exec:java -DskipTests -Dexec.mainClass=com.yugabyte.UniformLoadBalanceApp
    ```

#### Topology-aware load balancing

1. Create a file called `./src/main/java/com/yugabyte/TopologyAwareLoadBalanceApp.java` by executing the following command:

    ```sh
    touch ./src/main/java/com/yugabyte/TopologyAwareLoadBalanceApp.java
    ```

1. Paste the following into `TopologyAwareLoadBalanceApp.java`:

    ```java
    package com.yugabyte;

    import com.zaxxer.hikari.HikariConfig;
    import com.zaxxer.hikari.HikariDataSource;

    import java.sql.Connection;
    import java.sql.DriverManager;
    import java.sql.SQLException;
    import java.util.ArrayList;
    import java.util.List;
    import java.util.Properties;
    import java.util.Scanner;

    public class TopologyAwareLoadBalanceApp {

      public static void main(String[] args) {

        makeConnectionUsingDriverManager();
        makeConnectionUsingYbClusterAwareDataSource();

        System.out.println("Execution of Uniform Load Balance Java App complete!!");
      }

      public static void makeConnectionUsingDriverManager() {
        // List to store the connections so that they can be closed at the end
        List<Connection> connectionList = new ArrayList<>();

        System.out.println("Lets create 6 connections using DriverManager");
        String yburl = "jdbc:yugabytedb://127.0.0.1:5433/yugabyte?user=yugabyte&password=yugabyte&load-balance=true"
          + "&topology-keys=aws.us-west.us-west-2a";

        try {
          for(int i=0; i<6; i++) {
            Connection connection = DriverManager.getConnection(yburl);
            connectionList.add(connection);
          }

          System.out.println("You can verify the load balancing by visiting http://<host>:13000/rpcz as discussed before");
          System.out.println("Enter a integer to continue once verified:");
          int x = new Scanner(System.in).nextInt();

          System.out.println("Closing the connections!!");
          for(Connection connection : connectionList) {
            connection.close();
          }

        }
        catch (SQLException exception) {
          exception.printStackTrace();
        }

      }

      public static void makeConnectionUsingYbClusterAwareDataSource() {
        System.out.println("Now, Lets create 10 connections using YbClusterAwareDataSource and Hikari Pool");

        Properties poolProperties = new Properties();
        poolProperties.setProperty("dataSourceClassName", "com.yugabyte.ysql.YBClusterAwareDataSource");
        // The pool will create 10 connections to the servers
        poolProperties.setProperty("maximumPoolSize", String.valueOf(10));
        poolProperties.setProperty("dataSource.serverName", "127.0.0.1");
        poolProperties.setProperty("dataSource.portNumber", "5433");
        poolProperties.setProperty("dataSource.databaseName", "yugabyte");
        poolProperties.setProperty("dataSource.user", "yugabyte");
        poolProperties.setProperty("dataSource.password", "yugabyte");
        // If you want to provide additional end points
        String additionalEndpoints = "127.0.0.2:5433,127.0.0.3:5433";
        poolProperties.setProperty("dataSource.additionalEndpoints", additionalEndpoints);

        // If you want to load balance between specific geo locations using topology keys
        String geoLocations = "aws.us-west.us-west-2a";
        poolProperties.setProperty("dataSource.topologyKeys", geoLocations);
        HikariConfig config = new HikariConfig(poolProperties);
        config.validate();
        HikariDataSource hikariDataSource = new HikariDataSource(config);

        System.out.println("Wait for some time for Hikari Pool to set up and create the connections...");
        System.out.println("You can verify the load balancing by visiting http://<host>:13000/rpcz as discussed before.");
        System.out.println("Enter a integer to continue once verified:");
        int x = new Scanner(System.in).nextInt();

        System.out.println("Closing the Hikari Connection Pool!!");
        hikariDataSource.close();

      }

    }
    ```

    When using `DriverManager.getConnection()`, you need to include the `load-balance=true` property in the connection URL. In the case of `YBClusterAwareDataSource`, load balancing is enabled by default, but you must set property `dataSource.topologyKeys`.

1. Run the application, as follows:

    ```sh
     mvn -q package exec:java -DskipTests -Dexec.mainClass=com.yugabyte.TopologyAwareLoadBalanceApp
    ```
