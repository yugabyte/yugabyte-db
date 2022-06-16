---
title: YugabyteDB Quick start
headerTitle: Quick start
linkTitle: Quick start
description: Get started using YugabyteDB in less than five minutes on Docker.
aliases:
  - /docker/
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
      <a href="../" class="nav-link">
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
    <li>
      <a href="../linux/" class="nav-link">
        <i class="fab fa-linux" aria-hidden="true"></i>
        Linux
      </a>
    </li>
    <li class="active">
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

{{< note title="Note" >}}

The Docker option to run local clusters is recommended only for advanced Docker users. This is because running stateful apps like YugabyteDB in Docker is more complex and error-prone than stateless apps.

{{< /note >}}

### Prerequisites

You must have the Docker runtime installed on your localhost. Follow the links below to download and install Docker if you have not done so already.

<i class="fab fa-apple" aria-hidden="true"></i> [Docker for Mac](https://store.docker.com/editions/community/docker-ce-desktop-mac)

<i class="fab fa-centos"></i> [Docker for CentOS](https://store.docker.com/editions/community/docker-ce-server-centos)

<i class="fab fa-ubuntu"></i> [Docker for Ubuntu](https://store.docker.com/editions/community/docker-ce-server-ubuntu)

<i class="icon-debian"></i> [Docker for Debian](https://store.docker.com/editions/community/docker-ce-server-debian)

<i class="fab fa-windows" aria-hidden="true"></i> [Docker for Windows](https://store.docker.com/editions/community/docker-ce-desktop-windows)

### Install

Pull the YugabyteDB container.

```sh
$ docker pull yugabytedb/yugabyte
```

## Create a local cluster

To create a 1-node cluster with a replication factor (RF) of 1, run the command below.

```sh
$ docker run -d --name yugabyte  -p7000:7000 -p9000:9000 -p5433:5433 -p9042:9042\
 yugabytedb/yugabyte:latest bin/yugabyted start\
 --daemon=false
```

As per the above docker run command, the data stored in YugabyteDB is not persistent across container restarts. If you want to make YugabyteDB persist data across restarts then you have to add the volume mount option to the docker run command as shown below.

```sh
docker run -d --name yugabyte  -p7000:7000 -p9000:9000 -p5433:5433 -p9042:9042\
 -v ~/yb_data:/home/yugabyte/var\
 yugabytedb/yugabyte:latest bin/yugabyted start\
 --daemon=false
```

Clients can now connect to the YSQL and YCQL APIs at `localhost:5433` and `localhost:9042` respectively.

### Check cluster status

```sh
$ docker ps
```

```
CONTAINER ID        IMAGE                 COMMAND                  CREATED             STATUS              PORTS                                                                                                                                                                     NAMES
5088ca718f70        yugabytedb/yugabyte   "bin/yugabyted start…"   46 seconds ago      Up 44 seconds       0.0.0.0:5433->5433/tcp, 6379/tcp, 7100/tcp, 0.0.0.0:7000->7000/tcp, 0.0.0.0:9000->9000/tcp, 7200/tcp, 9100/tcp, 10100/tcp, 11000/tcp, 0.0.0.0:9042->9042/tcp, 12000/tcp   yugabyte
```

### Check cluster status with Admin UI

The [yb-master Admin UI](../../reference/configuration/yb-master/#admin-ui) is available at http://localhost:7000 and the [yb-tserver Admin UI](../../reference/configuration/yb-tserver/#admin-ui) is available at http://localhost:9000. To avoid port conflicts, you should make sure other processes on your machine do not have these ports mapped to `localhost`.

#### Overview and YB-Master status

The yb-master home page shows that you have a cluster (or universe) with `Replication Factor` of 1 and `Num Nodes (TServers)` as 1. The `Num User Tables` is `0` since there are no user tables created yet. YugabyteDB version number is also shown for your reference.

![master-home](/images/admin/master-home-docker-rf1.png)

The Masters section highlights the cloud, region and zone placement for the yb-master servers.

#### YB-TServer status

Clicking on the `See all nodes` takes us to the Tablet Servers page where you can observe the 1 tserver along with the time since it last connected to this master via regular heartbeats.

![master-home](/images/admin/master-tservers-list-docker-rf1.png)

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
