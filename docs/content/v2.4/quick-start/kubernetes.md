---
title: YugabyteDB Quick start
headerTitle: Quick start
linkTitle: Quick start
description: Get started using YugabyteDB in less than five minutes on Kubernetes (Minikube).
aliases:
  - /quick-start-kubernetes/
type: docs
---

<div class="custom-tabs tabs-style-2">
  <ul class="tabs-name">
    <li>
      <a href="./quick-start-yugabytedb-managed/" class="nav-link">
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
    <li>
      <a href="../docker/" class="nav-link">
        <i class="fab fa-docker" aria-hidden="true"></i>
        Docker
      </a>
    </li>
    <li class="active">
      <a href="../kubernetes/" class="nav-link">
        <i class="fas fa-cubes" aria-hidden="true"></i>
        Kubernetes
      </a>
    </li>
  </ul>
</div>

## Install YugabyteDB

### Prerequisites

- [Minikube](https://github.com/kubernetes/minikube) is installed on your localhost machine.

The Kubernetes version used by Minikube should be v1.13.0 or later. The default Kubernetes version being used by Minikube displays when you run the `minikube start` command. To install Minikube, see [Install Minikube](https://kubernetes.io/docs/tasks/tools/install-minikube/) in the Kubernetes documentation.

- [kubectl](https://kubernetes.io/docs/reference/kubectl/overview/) is installed.

To install `kubectl`, see [Install kubectl](https://kubernetes.io/docs/tasks/tools/install-kubectl/) in the Kubernetes documentation.

- [Helm 3+](https://helm.sh/) is installed. If you have Helm 2 then make sure you have Tiller installed on the Kubernetes cluster and thereafter change the helm commands accordingly.

To install `helm`, see [Install helm](https://helm.sh/docs/intro/install/) in the Helm documentation.

### Start Kubernetes

- Start Kubernetes using Minikube by running the following command. Note that minikube by default brings up a single-node Kubernetes environment with 2GB RAM, 2 CPUS, and a disk of 20GB. We recommend starting minkube with at least 8GB RAM, 4 CPUs and 40GB disk as shown below.

```sh
$ minikube start --memory=8192 --cpus=4 --disk-size=40g --vm-driver=virtualbox
```

```
...
Configuring environment for Kubernetes v1.14.2 on Docker 18.09.6
...
```

- Review Kubernetes dashboard by running the following command.

```sh
$ minikube dashboard
```

- Confirm that your kubectl is configured correctly by running the following command.

```sh
$ kubectl version
```

```
Client Version: version.Info{Major:"1", Minor:"14+", GitVersion:"v1.14.10-dispatcher", ...}
Server Version: version.Info{Major:"1", Minor:"14", GitVersion:"v1.14.2", ...}
```

- Confirm that your Helm 3 is configured correctly by running the following command.

```sh
$ helm version
```

```
version.BuildInfo{Version:"v3.0.3", GitCommit:"...", GitTreeState:"clean", GoVersion:"go1.13.6"}
```

### Download YugabyteDB Helm Chart

#### Add charts repository

To add the YugabyteDB charts repository, run the following command.

```sh
$ helm repo add yugabytedb https://charts.yugabyte.com
```

#### Fetch updates from the repository

Make sure that you have the latest updates to the repository by running the following command.

```sh
$ helm repo update
```

#### Validate the chart version

```sh
$ helm search repo yugabytedb/yugabyte
```

```sh
NAME                 CHART VERSION  APP VERSION  DESCRIPTION
yugabytedb/yugabyte  2.4.4.0        2.4.4.4-b7   YugabyteDB is the high-performance distributed ...
```

Now you are ready to create a local YugabyteDB cluster.

## Create a local cluster

Create a YugabyteDB cluster in Minikube using the commands below. Note that for Helm 3, you have to first create a namespace.

```sh
$ kubectl create namespace yb-demo
$ helm install yb-demo yugabytedb/yugabyte \
--set resource.master.requests.cpu=0.5,resource.master.requests.memory=0.5Gi,\
resource.tserver.requests.cpu=0.5,resource.tserver.requests.memory=0.5Gi,\
replicas.master=1,replicas.tserver=1 --namespace yb-demo
```

Note that in Minikube, the LoadBalancers for `yb-master-ui` and `yb-tserver-service` will remain in pending state since load balancers are not available in a Minikube environment. If you would like to turn off these services then pass the `enableLoadBalancer=False` flag as shown below.

```sh
$ helm install yb-demo yugabytedb/yugabyte \
--set resource.master.requests.cpu=0.5,resource.master.requests.memory=0.5Gi,\
resource.tserver.requests.cpu=0.5,resource.tserver.requests.memory=0.5Gi,\
replicas.master=1,replicas.tserver=1,enableLoadBalancer=False --namespace yb-demo
```

### Check cluster status with kubectl

Run the following command to see that you now have two services with one pod each — 1 yb-master pod (`yb-master-0`) and 1 yb-tserver pod (`yb-tserver-0`) running. For details on the roles of these pods in a YugabyteDB cluster (aka Universe), see [Universe](../../architecture/concepts/universe/) in the Concepts section.

```sh
$ kubectl --namespace yb-demo get pods
```

```
NAME           READY     STATUS              RESTARTS   AGE
yb-master-0    0/2       ContainerCreating   0          5s
yb-tserver-0   0/2       ContainerCreating   0          4s
```

Eventually, all the pods will have the `Running` state.

```
NAME           READY     STATUS    RESTARTS   AGE
yb-master-0    2/2       Running   0          13s
yb-tserver-0   2/2       Running   0          12s
```

To see the status of the three services, run the following command.

```sh
$ kubectl --namespace yb-demo get services
```

```
NAME                 TYPE           CLUSTER-IP     EXTERNAL-IP   PORT(S)                                        AGE
yb-master-ui         LoadBalancer   10.98.66.255   <pending>     7000:31825/TCP                                 119s
yb-masters           ClusterIP      None           <none>        7100/TCP,7000/TCP                              119s
yb-tserver-service   LoadBalancer   10.106.5.69    <pending>     6379:31320/TCP,9042:30391/TCP,5433:30537/TCP   119s
yb-tservers          ClusterIP      None           <none>        7100/TCP,9000/TCP,6379/TCP,9042/TCP,5433/TCP   119s
```

### Check cluster status with Admin UI

To check the cluster status, you need to access the Admin UI on port `7000` exposed by the `yb-master-ui` service. In order to do so, you need to find the port forward the port.

```sh
$ kubectl --namespace yb-demo port-forward svc/yb-master-ui 7000:7000
```

Now, you can view the [yb-master-0 Admin UI](../../reference/configuration/yb-master/#admin-ui) at the http://localhost:7000.

#### Overview and YB-Master status

The `yb-master-0` home page shows that you have a cluster with **Replication Factor** of 1 and **Num Nodes (TServers)** as `1`. The **Num User Tables** is `0` because there are no user tables created yet. The YugabyteDB version is also displayed for your reference.

![master-home](/images/admin/master-home-kubernetes-rf1.png)

The **Masters** section highlights the YB-Master service along its corresponding cloud, region and zone placement information.

#### YB-TServer status

Click **See all nodes** to go to the **Tablet Servers** page where you can observe the one YB-TServer along with the time since it last connected to the YB-Master using regular heartbeats. As new tables get added, new tablets will get automatically created and distributed evenly across all the available YB-TServers.

![tserver-list](/images/admin/master-tservers-list-kubernetes-rf1.png)

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
