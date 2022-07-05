---
title: YugabyteDB Quick start
headerTitle: Quick start
linkTitle: Quick start
description: Get started using YugabyteDB in less than five minutes on Linux.
aliases:
  - /quick-start/linux/
type: docs
---

<div class="custom-tabs tabs-style-2">
  <ul class="tabs-name">
    <li>
      <a href="../../quick-start-yugabytedb-managed/" class="nav-link">
        Use a cloud cluster
      </a>
    </li>
    <li class="active">
      <a href="../../" class="nav-link">
        Use a local cluster
      </a>
    </li>
  </ul>
</div>

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

### Prerequisites

1. One of the following operating systems

    - <i class="icon-centos"></i> CentOS 7

    - <i class="icon-ubuntu"></i> Ubuntu 16.04 or later

1. Verify that you have Python 2 or 3 installed.

    ```sh
    $ python --version
    ```

    ```
    Python 3.7.3
    ```

    {{< note title="Note" >}}

By default, CentOS 8 doesn't have an unversioned system-wide `python` command to avoid locking users to a specific version of Python.
One way to fix this is to set `python3` the alternative for `python` by running: `sudo alternatives --set python /usr/bin/python3`.

Starting from Ubuntu 20.04, `python` isn't available anymore. An easy fix is to install `sudo apt install python-is-python3`.

    {{< /note >}}

1. `wget` or `curl` is available.

    The instructions use the `wget` command to download files. If you prefer to use `curl`, you can replace `wget` with `curl -O`.

    To install `wget`:

    - CentOS: `yum install wget`
    - Ubuntu: `apt install wget`

    To install `curl`:

    - CentOS: `yum install curl`
    - Ubuntu: `apt install curl`

1. Each tablet maps to its own file, so if you experiment with a few hundred tables and a few tablets per table, you can soon end up creating a large number of files in the current shell. Make sure to [configure ulimit values](../../deploy/manual-deployment/system-config#ulimits).

### Download YugabyteDB

1. Download the YugabyteDB package using the following `wget` command.

    ```sh
    wget https://downloads.yugabyte.com/releases/2.4.8.0/yugabyte-2.4.8.0-b16-linux-x86_64.tar.gz
    ```

1. Extract the package and then change directories to the YugabyteDB home.

    ```sh
    tar xvfz yugabyte-2.4.8.0-b16-linux-x86_64.tar.gz && cd yugabyte-2.4.8.0/
    ```

### Configure YugabyteDB

To configure YugabyteDB, run the following shell script.

```sh
./bin/post_install.sh
```

## Create a local cluster

{{< note title="Note" >}}

This Linux Quick Start is based on the new [`yugabyted`](../../reference/configuration/yugabyted/) server. You can refer to the older [`yb-ctl`](../../admin/yb-ctl/) based instructions in the [v2.1 docs](/v2.1/quick-start/install/linux/).

Note that yugabyted currently supports creating a single-node cluster only. Ability to create multi-node clusters is under [active development](https://github.com/yugabyte/yugabyte-db/issues/2057).

{{< /note >}}

To create a single-node local cluster with a replication factor (RF) of 1, run the following command.

```sh
$ ./bin/yugabyted start
```

After the cluster is created, clients can connect to the YSQL and YCQL APIs at `localhost:5433` and `localhost:9042` respectively. You can also check `./var/data` to see the data directory and `./var/logs` to see the logs directory.

### Check cluster status

```sh
$ ./bin/yugabyted status
```
```
+--------------------------------------------------------------------------------------------------+
|                                            yugabyted                                             |
+--------------------------------------------------------------------------------------------------+
| Status              : Running                                                                    |
| Web console         : http://127.0.0.1:7000                                                      |
| JDBC                : jdbc:postgresql://127.0.0.1:5433/yugabyte?user=yugabyte&password=yugabyte  |
| YSQL                : bin/ysqlsh                                                                 |
| YCQL                : bin/ycqlsh                                                                 |
| Data Dir            : var/data                                                                   |
| Log Dir             : var/logs                                                                   |
| Universe UUID       : fad6c687-e1dc-4dfd-af4b-380021e19be3                                       |
+--------------------------------------------------------------------------------------------------+
```

### Check cluster status with Admin UI

The [YB-Master Admin UI](../../reference/configuration/yb-master/#admin-ui) is available at [http://127.0.0.1:7000](http://127.0.0.1:7000) and the [YB-TServer Admin UI](../../reference/configuration/yb-tserver/#admin-ui) is available at [http://127.0.0.1:9000](http://127.0.0.1:9000).

#### Overview and YB-Master status

The yb-master Admin UI home page shows that you have a cluster with `Replication Factor` of 1 and `Num Nodes (TServers)` as 1. The `Num User Tables` is 0 since there are no user tables created yet. The YugabyteDB version number is also shown for your reference.

![master-home](/images/admin/master-home-binary-rf1.png)

The Masters section highlights the 1 yb-master along with its corresponding cloud, region and zone placement.

#### YB-TServer status

Clicking on the `See all nodes` takes us to the Tablet Servers page where you can observe the 1 yb-tserver along with the time since it last connected to this yb-master via regular heartbeats. Since there are no user tables created yet, you can see that the `Load (Num Tablets)` is 0. As new tables get added, new tablets (aka shards) will get automatically created and distributed evenly across all the available tablet servers.

![master-home](/images/admin/master-tservers-list-binary-rf1.png)

## Build a Java application

### Prerequisites

This tutorial assumes that:

- YugabyteDB is up and running. If you are new to YugabyteDB, you can download, install, and have YugabyteDB up and running within five minutes by following the steps in [Quick start](../../quick-start/).
- Java Development Kit (JDK) 1.8, or later, is installed. JDK installers for Linux and macOS can be downloaded from [OpenJDK](http://jdk.java.net/), [AdoptOpenJDK](https://adoptopenjdk.net/), or [Azul Systems](https://www.azul.com/downloads/zulu-community/).
- [Apache Maven](https://maven.apache.org/index.html) 3.3 or later, is installed.

### Create the sample Java application

#### Create the project's POM

Create a file, named `pom.xml`, and then copy the following content into it. The Project Object Model (POM) includes configuration information required to build the project. You can change the PostgreSQL dependency version, depending on the PostgreSQL JDBC driver you want to use.

```mvn
<?xml version="1.0"?>
<project
  xsi:schemaLocation="http://maven.apache.org/POM/4.0.0 http://maven.apache.org/xsd/maven-4.0.0.xsd"
  xmlns="http://maven.apache.org/POM/4.0.0"
  xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <modelVersion>4.0.0</modelVersion>

  <groupId>com.yugabyte.sample.apps</groupId>
  <artifactId>hello-world</artifactId>
  <version>1.0</version>
  <packaging>jar</packaging>

  <dependencies>
    <dependency>
      <groupId>org.postgresql</groupId>
      <artifactId>postgresql</artifactId>
      <version>42.2.14</version>
    </dependency>
  </dependencies>

  <build>
    <plugins>
      <plugin>
        <groupId>org.apache.maven.plugins</groupId>
        <artifactId>maven-compiler-plugin</artifactId>
        <version>3.7.0</version>
        <configuration>
          <source>1.8</source>
          <target>1.8</target>
        </configuration>
      </plugin>
      <plugin>
        <groupId>org.apache.maven.plugins</groupId>
        <artifactId>maven-dependency-plugin</artifactId>
        <version>2.1</version>
        <executions>
          <execution>
            <id>copy-dependencies</id>
            <phase>prepare-package</phase>
            <goals>
              <goal>copy-dependencies</goal>
            </goals>
            <configuration>
              <outputDirectory>${project.build.directory}/lib</outputDirectory>
              <overWriteReleases>true</overWriteReleases>
              <overWriteSnapshots>true</overWriteSnapshots>
              <overWriteIfNewer>true</overWriteIfNewer>
            </configuration>
          </execution>
        </executions>
      </plugin>
    </plugins>
  </build>
</project>
```

#### Write the sample Java application

Create the appropriate directory structure as expected by Maven.

```sh
$ mkdir -p src/main/java/com/yugabyte/sample/apps
```

Copy the following contents into the file `src/main/java/com/yugabyte/sample/apps/YBSqlHelloWorld.java`.

```java
package com.yugabyte.sample.apps;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;

public class YBSqlHelloWorld {

    public static void main(String[] args) throws ClassNotFoundException, SQLException {
        Class.forName("org.postgresql.Driver");
        try (Connection conn = DriverManager.getConnection("jdbc:postgresql://localhost:5433/yugabyte", "yugabyte", "yugabyte");
            Statement stmt = conn.createStatement();) {
            System.out.println("Connected to the PostgreSQL server successfully.");

            String createTableQuery = "CREATE TABLE IF NOT EXISTS employee(id int primary key, name varchar, age int, language text) ";
            stmt.executeUpdate(createTableQuery);
            System.out.println("Created table employee");

            String insertQuery = "INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'Java');";
            stmt.executeUpdate(insertQuery);
            System.out.println("Inserted data: INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'Java');");

            ResultSet rs = stmt.executeQuery("select * from employee");
            while (rs.next())
                System.out.println("Query returned: "+ "name=" + rs.getString(2) + ", age=" + rs.getString(3) + ", language=" + rs.getString(4));
        } catch (SQLException e) {
            System.err.println(e.getMessage());

        }
    }
}

```

#### Build the project

To build the project, run the following `mvn package` command.

```sh
$ mvn package
```

You should see a `BUILD SUCCESS` message.

#### Run the application

To run the application , run the following command.

```sh
$ java -cp "target/hello-world-1.0.jar:target/lib/*" com.yugabyte.sample.apps.YBSqlHelloWorld
```

You should see the following as the output.

```
Created table employee
Inserted data: INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'Java');
Query returned: name=John, age=35, language: Java
```
