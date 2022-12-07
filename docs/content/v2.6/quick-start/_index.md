---
title: YugabyteDB Quick Start
headerTitle: Quick start
linkTitle: Quick start
description: Get started using YugabyteDB in less than five minutes on macOS.
aliases:
  - /quick-start/
layout: single
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../quick-start-yugabytedb-managed/" class="nav-link">
      Use a cloud cluster
    </a>
  </li>
  <li class="active">
    <a href="../" class="nav-link">
      Use a local cluster
    </a>
  </li>
</ul>

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li class="active">
    <a href="../quick-start/" class="nav-link">
      <i class="fa-brands fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a href="../quick-start/linux/" class="nav-link">
      <i class="fa-brands fa-linux" aria-hidden="true"></i>
      Linux
    </a>
  </li>
  <li>
    <a href="../quick-start/docker/" class="nav-link">
      <i class="fa-brands fa-docker" aria-hidden="true"></i>
      Docker
    </a>
  </li>
  <li>
    <a href="../quick-start/kubernetes/" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
</ul>

## Install YugabyteDB

### Prerequisites

1. macOS 10.12 or later.

1. Verify that you have Python 2 or 3 installed.

    ```sh
    $ python --version
    ```

    ```output
    Python 3.7.3
    ```

1. `wget` or `curl` is available.

    The instructions use the `wget` command to download files. If you prefer to use `curl` (included in macOS), you can replace `wget` with `curl -O`.

    To install `wget` on your Mac, you can run the following command if you use Homebrew:

    ```sh
    $ brew install wget
    ```

1. Each tablet maps to its own file, so if you experiment with a few hundred tables and a few tablets per table, you can soon end up creating a large number of files in the current shell. Make sure that this command shows a big enough value.

    ```sh
    $ launchctl limit maxfiles
    ```

    We recommend setting the soft and hard limits to 1048576.

    Edit `/etc/sysctl.conf`, if it exists, to include the following:

    ```sh
    kern.maxfiles=1048576
    kern.maxproc=2500
    kern.maxprocperuid=2500
    kern.maxfilesperproc=1048576
    ```

    If this file does not exist, then create the file `/Library/LaunchDaemons/limit.maxfiles.plist` and insert the following:

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

    Ensure that the `plist` file is owned by `root:wheel` and has permissions `-rw-r--r--`. To take effect, you need to reboot your computer or run this command:

    ```sh
    $ sudo launchctl load -w /Library/LaunchDaemons/limit.maxfiles.plist
    ```

    You might have to `unload` the service before loading it.

### Download YugabyteDB

1. Download the YugabyteDB `tar.gz` file using the following `wget` command.

    ```sh
    $ wget https://downloads.yugabyte.com/releases/2.6.18.0/yugabyte-2.6.18.0-b3-darwin-x86_64.tar.gz
    ```

1. Extract the package and then change directories to the YugabyteDB home.

    ```sh
    $ tar xvfz yugabyte-2.6.18.0-b9-darwin-x86_64.tar.gz && cd yugabyte-2.6.18.0/
    ```

### Configure

Some of the examples in the [Explore core features](../../explore/) section require extra loopback addresses that allow you to simulate the use of multiple hosts or nodes.

To add six loopback addresses, run the following commands, which require `sudo` access.

```sh
sudo ifconfig lo0 alias 127.0.0.2
sudo ifconfig lo0 alias 127.0.0.3
sudo ifconfig lo0 alias 127.0.0.4
sudo ifconfig lo0 alias 127.0.0.5
sudo ifconfig lo0 alias 127.0.0.6
sudo ifconfig lo0 alias 127.0.0.7
```

**Note**: The loopback addresses do not persist upon rebooting of your Mac.

To verify that the extra loopback addresses exist, run the following command.

```sh
$ ifconfig lo0
```

You should see some output like the following:

```output
lo0: flags=8049<UP,LOOPBACK,RUNNING,MULTICAST> mtu 16384
  options=1203<RXCSUM,TXCSUM,TXSTATUS,SW_TIMESTAMP>
  inet 127.0.0.1 netmask 0xff000000
  inet6 ::1 prefixlen 128
  inet6 fe80::1%lo0 prefixlen 64 scopeid 0x1
  inet 127.0.0.2 netmask 0xff000000
  inet 127.0.0.3 netmask 0xff000000
  inet 127.0.0.4 netmask 0xff000000
  inet 127.0.0.5 netmask 0xff000000
  inet 127.0.0.6 netmask 0xff000000
  inet 127.0.0.7 netmask 0xff000000
  nd6 options=201<PERFORMNUD,DAD>
```

## Create a local cluster

{{< note title="Note" >}}

This macOS Quick Start uses the [`yugabyted`](../../reference/configuration/yugabyted/) server. You can refer to the older [`yb-ctl`](../../admin/yb-ctl/) based instructions in the [v2.1 docs](/v2.1/quick-start/install/linux/).

{{< /note >}}

To create a single-node local cluster with a replication factor (RF) of 1, run the following command.

```sh
$ ./bin/yugabyted start
```

After the cluster is created, clients can connect to the YSQL and YCQL APIs at `localhost:5433` and `localhost:9042` respectively. You can also check `~/var/data` to see the data directory and `~/var/logs` to see the logs directory.

### Check cluster status

```sh
$ ./bin/yugabyted status
```

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

The [YB-Master Admin UI](../../reference/configuration/yb-master/#admin-ui) is available at [http://127.0.0.1:7000](http://127.0.0.1:7000) and the [YB-TServer Admin UI](../../reference/configuration/yb-tserver/#admin-ui) is available at [http://127.0.0.1:9000](http://127.0.0.1:9000).

#### Overview and YB-Master status

The yb-master Admin UI home page shows that you have a cluster with `Replication Factor` of 1 and `Num Nodes (TServers)` as 1. `Num User Tables` is 0 since there are no user tables created yet. The YugabyteDB version number is also shown for your reference.

![master-home](/images/admin/master-home-binary-rf1.png)

The Masters section highlights the 1 yb-master along with its corresponding cloud, region and zone placement.

#### YB-TServer status

Clicking `See all nodes` takes you to the Tablet Servers page where you can observe the 1 yb-tserver along with the time since it last connected to this yb-master via regular heartbeats. Since there are no user tables created yet, you can see that the `Load (Num Tablets)` is 0. As new tables get added, new tablets (aka shards) will be created automatically and distributed evenly across all the available tablet servers.

![master-home](/images/admin/master-tservers-list-binary-rf1.png)

## Build a Java application

### Prerequisites

This tutorial assumes that:

- YugabyteDB is up and running. If you are new to YugabyteDB, you can download, install, and have YugabyteDB up and running within five minutes by following the steps in [Quick start](../../quick-start/).
- Java Development Kit (JDK) 1.8, or later, is installed. JDK installers for Linux and macOS can be downloaded from [OpenJDK](http://jdk.java.net/), [AdoptOpenJDK](https://adoptopenjdk.net/), or [Azul Systems](https://www.azul.com/downloads/zulu-community/).
- [Apache Maven](https://maven.apache.org/index.html) 3.3 or later, is installed.

### Create and configure the Java project

1. Create a project called "MySample".

    ```sh
    $ mvn archetype:generate \
        -DgroupId=com.yugabyte \
        -DartifactId=MySample \
        -DarchetypeArtifactId=maven-archetype-quickstart \
        -DinteractiveMode=false

    $ cd MySample
    ```

1. Open the `pom.xml` file in a text editor.

1. Add the following below the `<url>` element.

    ```xml
    <properties>
      <maven.compiler.source>1.8</maven.compiler.source>
      <maven.compiler.target>1.8</maven.compiler.target>
    </properties>
    ```

1. Add the following within the `<dependencies>` element.

    ```xml
    <dependency>
      <groupId>org.postgresql</groupId>
      <artifactId>postgresql</artifactId>
      <version>42.2.14</version>
    </dependency>
    ```

    Your `pom.xml` file should now be similar to the following:

    ```xml
    <project xmlns="http://maven.apache.org/POM/4.0.0" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
      xsi:schemaLocation="http://maven.apache.org/POM/4.0.0 http://maven.apache.org/maven-v4_0_0.xsd">
      <modelVersion>4.0.0</modelVersion>
      <groupId>com.yugabyte</groupId>
      <artifactId>MySample</artifactId>
      <packaging>jar</packaging>
      <version>1.0-SNAPSHOT</version>
      <name>MySample</name>
      <url>http://maven.apache.org</url>
      <properties>
        <maven.compiler.source>1.8</maven.compiler.source>
        <maven.compiler.target>1.8</maven.compiler.target>
      </properties>
      <dependencies>
        <dependency>
          <groupId>junit</groupId>
          <artifactId>junit</artifactId>
          <version>3.8.1</version>
          <scope>test</scope>
        </dependency>
        <dependency>
          <groupId>org.postgresql</groupId>
          <artifactId>postgresql</artifactId>
          <version>42.2.14</version>
        </dependency>
      </dependencies>
    </project>
    ```

1. Save and close `pom.xml`.

1. Install the added dependency.

    ```sh
    $ mvn install
    ```

### Create the sample Java application

1. Copy the following Java code to a new file named `src/main/java/com/yugabyte/HelloSqlApp.java`:

    ```java
    package com.yugabyte;

    import java.sql.Connection;
    import java.sql.DriverManager;
    import java.sql.ResultSet;
    import java.sql.SQLException;
    import java.sql.Statement;

    public class HelloSqlApp {
      public static void main(String[] args) throws ClassNotFoundException, SQLException {
        Class.forName("org.postgresql.Driver");
        Connection conn = DriverManager.getConnection("jdbc:postgresql://localhost:5433/yugabyte",
                                                      "yugabyte", "yugabyte");
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

1. Run your new program.

    ```sh
    $ mvn -q package exec:java -DskipTests -Dexec.mainClass=com.yugabyte.HelloSqlApp
    ```

    You should see the following as the output:

    ```output
    Connected to the PostgreSQL server successfully.
    Created table employee
    Inserted data: INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'Java');
    Query returned: name=John, age=35, language: Java
    ```
