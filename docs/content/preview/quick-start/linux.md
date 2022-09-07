---
title: YugabyteDB Quick start
headerTitle: Quick start
linkTitle: Quick start
description: Test YugabyteDB's APIs and core features by creating a local cluster on a single host.
headcontent: Create a local cluster on a single host
aliases:
  - /quick-start/linux/
type: docs
rightNav:
  hideH4: true
---

<div class="custom-tabs tabs-style-2">
  <ul class="tabs-name">
    <li>
      <a href="../../quick-start-yugabytedb-managed/" class="nav-link">
        Use a cloud cluster
      </a>
    </li>
    <li class="active">
      <a href="../../quick-start/" class="nav-link">
        Use a local cluster
      </a>
    </li>
  </ul>
</div>

The local cluster setup on a single host is intended for development and learning. For production deployment, performance benchmarking, or deploying a true multi-node on multi-host setup, see [Deploy YugabyteDB](../../deploy/).

<div class="custom-tabs tabs-style-1">
  <ul class="tabs-name">
    <li>
      <a href="../" class="nav-link">
        <i class="fab fa-apple" aria-hidden="true"></i>
        macOS
      </a>
    </li>
    <li class="active">
      <a href="../linux/" class="nav-link">
        <i class="fab fa-linux" aria-hidden="true"></i>
        Linux
      </a>
    </li>
    <li>
      <a href="../docker/" class="nav-link">
        <i class="fab fa-docker" aria-hidden="true"></i>
        Docker
      </a>
    </li>
    <li>
      <a href="../kubernetes/" class="nav-link">
        <i class="fas fa-cubes" aria-hidden="true"></i>
        Kubernetes
      </a>
    </li>
  </ul>
</div>

## Install YugabyteDB

Installing YugabyteDB involves completing [prerequisites](#prerequisites) and [downloading the YugabyteDB package](#download-yugabytedb).

### Prerequisites

Before installing YugabyteDB, ensure that you have the following available:

1. One of the following operating systems:

    - <i class="icon-centos"></i> CentOS 7 or later

    - <i class="icon-ubuntu"></i> Ubuntu 16.04 or later

1. Python 3. To check the version, execute the following command:

    ```sh
    python --version
    ```

    ```output
    Python 3.7.3
    ```

    By default, CentOS 8 does not have an unversioned system-wide `python` command. To fix this, set `python3` as the alternative for `python` by running `sudo alternatives --set python /usr/bin/python3`.

    Starting from Ubuntu 20.04, `python` is no longer available. To fix this, run `sudo apt install python-is-python3`.

1. `wget` or `curl`.

    The instructions use the `wget` command to download files. If you prefer to use `curl`, you can replace `wget` with `curl -O`.

    To install `wget`:

    - On CentOS, run `yum install wget`
    - On Ubuntu, run `apt install wget`

    To install `curl`:

    - On CentOS, run `yum install curl`
    - On Ubuntu, run `apt install curl`

1. Because each tablet maps to its own file, you can create a very large number of files in the current shell by experimenting with several hundred tables and several tablets per table. You need to [configure ulimit values](../../deploy/manual-deployment/system-config/#ulimits).

### Download YugabyteDB

YugabyteDB supports both x86 and ARM (aarch64) CPU architectures. Download packages ending in `x86_64.tar.gz` to run on x86, and packages ending in `aarch64.tar.gz` to run on ARM.

Download YugabyteDB as follows:

1. Download the YugabyteDB package using one of the following `wget` commands:

    ```sh
    wget https://downloads.yugabyte.com/releases/{{< yb-version version="preview">}}/yugabyte-{{< yb-version version="preview" format="build">}}-linux-x86_64.tar.gz
    ```

    Or:

    ```sh
    wget https://downloads.yugabyte.com/releases/{{< yb-version version="preview">}}/yugabyte-{{< yb-version version="preview" format="build">}}-el8-aarch64.tar.gz
    ```

1. Extract the package and then change directories to the YugabyteDB home.

    ```sh
    tar xvfz yugabyte-{{< yb-version version="preview" format="build">}}-linux-x86_64.tar.gz && cd yugabyte-{{< yb-version version="preview">}}/
    ```

    Or:

    ```sh
    tar xvfz yugabyte-{{< yb-version version="preview" format="build">}}-el8-aarch64.tar.gz && cd yugabyte-{{< yb-version version="preview">}}/
    ```

### Configure YugabyteDB

To configure YugabyteDB, run the following shell script:

```sh
./bin/post_install.sh
```

## Create a local cluster

To create a single-node local cluster with a replication factor (RF) of 1, run the following command:

```sh
./bin/yugabyted start
```

After the cluster has been created, clients can connect to the YSQL and YCQL APIs at `http://localhost:5433` and `http://localhost:9042` respectively. You can also check `~/var/data` to see the data directory and `~/var/logs` to see the logs directory.

If you have previously installed YugabyteDB 2.8 or later and created a cluster on the same computer, you may need to [upgrade the YSQL system catalog](../../manage/upgrade-deployment/#upgrade-the-ysql-system-catalog) to run the latest features.

### Check the cluster status

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

### Use the Admin UI

The cluster you have created consists of two processes: [YB-Master](../../architecture/concepts/yb-master/) which keeps track of various metadata (list of tables, users, roles, permissions, and so on) and [YB-TServer](../../architecture/concepts/yb-tserver/) which is responsible for the actual end-user requests for data updates and queries.

Each of the processes exposes its own Admin UI that can be used to check the status of the corresponding process, and perform certain administrative operations. The [YB-Master Admin UI](../../reference/configuration/yb-master/#admin-ui) is available at <http://127.0.0.1:7000> and the [YB-TServer Admin UI](../../reference/configuration/yb-tserver/#admin-ui) is available at <http://127.0.0.1:9000>.

#### Overview and YB-Master status

The following illustration shows the YB-Master home page with a cluster with a replication factor of 1, a single node, and no tables. The YugabyteDB version is also displayed.

![master-home](/images/admin/master-home-binary-rf1.png)

The **Masters** section highlights the 1 YB-Master along with its corresponding cloud, region, and zone placement.

#### YB-TServer status

Click **See all nodes** to open the **Tablet Servers** page that lists the YB-TServer along with the time since it last connected to the YB-Master using regular heartbeats. Because there are no user tables, **User Tablet-Peers / Leaders** is 0. As tables are added, new tablets (also known as shards) will be created automatically and distributed evenly across all the available tablet servers.

![master-home](/images/admin/master-tservers-list-binary-rf1.png)

## Connect to the database

Using the YugabyteDB SQL shell, [ysqlsh](../../admin/ysqlsh/), you can connect to your cluster and interact with it using distributed SQL. ysqlsh is installed with YugabyteDB and is located in the bin directory of the YugabyteDB home directory.

To open the YSQL shell, run `ysqlsh`.

```sh
$ ./bin/ysqlsh
```

```output
ysqlsh (11.2-YB-2.1.0.0-b0)
Type "help" for help.

yugabyte=#
```

To load sample data and explore an example using ysqlsh, refer to [Retail Analytics](../../sample-data/retail-analytics/).

## Build a Java application

The following tutorial shows a small Java application that connects to a YugabyteDB cluster using the topology-aware Yugabyte JDBC driver and performs basic SQL operations.

For examples using other languages, refer to [Build an application](../../develop/build-apps/).

### Prerequisites

- Java Development Kit (JDK) 1.8 or later. JDK installers can be downloaded from [OpenJDK](http://jdk.java.net/).
- [Apache Maven](https://maven.apache.org/index.html) 3.3 or later.

### Start a local multi-node cluster

First, destroy the currently running single-node cluster:

```sh
./bin/yugabyted destroy
```

Create the first node as follows:

```sh
./bin/yugabyted start --advertise_address=127.0.0.1 --base_dir=$HOME/yugabyte-{{< yb-version version="preview" >}}/node1 --cloud_location=aws.us-east.us-east-1a
```

The additional nodes need loopback addresses configured that allow you to simulate the use of multiple hosts or nodes:

```sh
sudo ifconfig lo0 alias 127.0.0.2
sudo ifconfig lo0 alias 127.0.0.3
```

The loopback addresses do not persist upon rebooting your computer.

Add two more nodes to the cluster using the join option:

```sh
./bin/yugabyted start --advertise_address=127.0.0.2 --join=127.0.0.1 --base_dir=$HOME/yugabyte-{{< yb-version version="preview" >}}/node2 --cloud_location=aws.us-east.us-east-2a

./bin/yugabyted start --advertise_address=127.0.0.3 --join=127.0.0.1 --base_dir=$HOME/yugabyte-{{< yb-version version="preview" >}}/node3 --cloud_location=aws.us-east.us-east-3a
```

After starting the yugabyted processes on all the nodes, configure the data placement constraint of the YugabyteDB cluster:

```sh
./bin/yugabyted configure --fault_tolerance=zone
```

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

1. Add the following dependencies for the driver HikariPool in the `<dependencies>` element in `pom.xml`:

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
        //List to store the connections so that they can be closed at the end
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

        System.out.println("Wait for some time for Hikari Pool to setup and create the connections...");
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

        // If you want to load balance between specific geo locations using topology keys
        String geoLocations = "aws.us-west.us-west-2a";
        poolProperties.setProperty("dataSource.topologyKeys", geoLocations);
        HikariConfig config = new HikariConfig(poolProperties);
        config.validate();
        HikariDataSource hikariDataSource = new HikariDataSource(config);

        System.out.println("Wait for some time for Hikari Pool to setup and create the connections...");
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
