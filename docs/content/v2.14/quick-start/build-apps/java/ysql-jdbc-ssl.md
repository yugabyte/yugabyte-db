---
title: Build a Java application that uses YSQL over SSL
headerTitle: Build a Java application
linkTitle: Java
description: Build a sample Java application with the PostgreSQL JDBC Driver and use the YSQL API to connect to and interact with YugabyteDB via SSL/TLS.
menu:
  v2.14:
    parent: build-apps
    name: Java
    identifier: java-2
    weight: 550
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../ysql-yb-jdbc/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - YB - JDBC
    </a>
  </li>
   <li >
    <a href="../ysql-jdbc/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - JDBC
    </a>
  </li>
  <li >
    <a href="../ysql-jdbc-ssl/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - JDBC SSL/TLS
    </a>
  </li>
  <li >
    <a href="../ysql-hibernate/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - Hibernate
    </a>
  </li>
  <li >
    <a href="../ysql-sdyb/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - Spring Data YugabyteDB
    </a>
  </li>
  <li >
    <a href="../ysql-spring-data/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - Spring Data JPA
    </a>
  </li>
  <li>
    <a href="../ysql-ebean/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - Ebean
    </a>
  </li>
  <li>
    <a href="../ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
  <li>
    <a href="../ycql-4.6/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL (4.6)
    </a>
  </li>
</ul>

## Prerequisites

This tutorial assumes that:

- YugabyteDB is up and running. If you are new to YugabyteDB, you can download, install, and have YugabyteDB up and running within five minutes by following the steps in [Quick start](../../../../quick-start/).
- Set up SSL/TLS depending on the platform you choose to create your local cluster. To set up a cluster in Minikube with SSL/TLS, see [SSL certificates for a cluster in Kubernetes](#set-up-a-cluster-with-minikube-optional).
- Java Development Kit (JDK) 1.8 or later is installed. JDK installers for Linux and macOS can be downloaded from [OpenJDK](http://jdk.java.net/), [AdoptOpenJDK](https://adoptopenjdk.net/), or [Azul Systems](https://www.azul.com/downloads/zulu-community/).
- [Apache Maven](https://maven.apache.org/index.html) 3.3 or later is installed.
- [OpenSSL](https://www.openssl.org/) 1.1.1 or later is installed.

## SSL certificates for a cluster in Kubernetes (Optional)

1. Create a minikube cluster by adding `tls.enabled=true` to the command line described in [Quick start](../../../../quick-start/create-local-cluster/kubernetes/).

   ```sh
   $ kubectl create namespace yb-demo
   $ helm install yb-demo yugabytedb/yugabyte \
   --version {{<yb-version version="v2.14" format="short">}} \
   --set resource.master.requests.cpu=0.5,resource.master.requests.memory=0.5Gi,\
   resource.tserver.requests.cpu=0.5,resource.tserver.requests.memory=0.5Gi,\
   replicas.master=1,replicas.tserver=1,tls.enabled=true --namespace yb-demo
   ```

1. Verify that SSL is enabled using `ysqlsh`.

   ```sh
    $ ysqlsh
    ```

    ```output
    ysqlsh (11.2-YB-2.9.0.0-b0)
    SSL connection (protocol: TLSv1.3, cipher: TLS_AES_256_GCM_SHA384, bits: 256, compression: off)
    Type "help" for help.
    ```

1. Check for the key and certificate files in yb-tserver.

   ```sh
   $ kubectl exec -n yb-demo -it yb-tserver-0 -- bash
   [root@yb-tserver-0 cores]# ls -al /root/.yugabytedb/
   ```

   ```output
   total 4
   drwxrwxrwt 3 root root  140 Oct 22 06:04 .
   dr-xr-x--- 1 root root 4096 Oct 22 06:19 ..
   drwxr-xr-x 2 root root  100 Oct 22 06:04 ..2021_10_22_06_04_46.596961191
   lrwxrwxrwx 1 root root   31 Oct 22 06:04 ..data -> ..2021_10_22_06_04_46.596961191
   lrwxrwxrwx 1 root root   15 Oct 22 06:04 root.crt -> ..data/root.crt
   lrwxrwxrwx 1 root root   21 Oct 22 06:04 yugabytedb.crt -> ..data/yugabytedb.crt
   lrwxrwxrwx 1 root root   21 Oct 22 06:04 yugabytedb.key -> ..data/yugabytedb.key
   ```

1. Download these files to your system and proceed to step 2 under [Set up SSL certificates](#set-up-ssl-certificates-for-java-applications).

   ```sh
   % mkdir YBClusterCerts; cd YBClusterCerts
   % kubectl exec -n "yb-demo" "yb-tserver-0" -- tar -C "/root/.yugabytedb" -cf - . |tar xf -
   Defaulted container "yb-tserver" out of: yb-tserver, yb-cleanup
   % ls
   root.crt yugabytedb.crt yugabytedb.key
   ```

## Set up SSL certificates for Java applications

To build a Java application that connects to YugabyteDB over an SSL connection, you need the root certificate (`ca.crt`), and node certificate (`yugabytedb.crt`) and key (`yugabytedb.key`) files. If you have not generated these files, follow the instructions in [Create server certificates](../../../../secure/tls-encryption/server-certificates/).

1. Download the certificate (`yugabytedb.crt`, `yugabytedb.key`, and `ca.crt`) files (see [Copy configuration files to the nodes](../../../../secure/tls-encryption/server-certificates/#copy-configuration-files-to-the-nodes)).

1. If you do not have access to the system `cacerts` Java truststore you can create your own truststore.

    ```sh
    $ keytool -keystore ybtruststore -alias ybtruststore -import -file ca.crt
    ```

    Enter a password when you're prompted to enter one for your keystore.

1. Export the truststore. In the following command, replace `<YOURSTOREPASS>` with the password you used for your keystore creation.

    ```sh
    $ keytool -exportcert -keystore ybtruststore -alias ybtruststore -storepass <YOURSTOREPASS> -file ybtruststore.crt
    ```

1. Convert and export to PEM format with `ybtruststore.pem`.

   ```sh
   $ openssl x509 -inform der -in ybtruststore.crt -out ybtruststore.pem
   ```

1. Verify the `yugabytedb.crt` client certificate with `ybtruststore`.

    ```sh
    $ openssl verify -CAfile ybtruststore.pem -purpose sslclient yugabytedb.crt
    ```

1. Convert the client certificate to DER format.

    ```sh
    $ openssl x509 –in yugabytedb.crt -out yugabytedb.crt.der -outform der
    ```

1. Convert the client key to pk8 format.

    ```sh
    $ openssl pkcs8 -topk8 -inform PEM -in yugabytedb.key -outform DER -nocrypt -out yugabytedb.key.pk8
    ```

## Create and configure the Java project

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

1. Add the following below the `<url>` element if you're using Java 8.

    ```xml
    <properties>
      <maven.compiler.source>1.8</maven.compiler.source>
      <maven.compiler.target>1.8</maven.compiler.target>
    </properties>
    ```

    If you're using Java 11, it should be:

    ```xml
    <properties>
      <maven.compiler.source>11</maven.compiler.source>
      <maven.compiler.target>11</maven.compiler.target>
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

1. Create an ssl resource directory.

    ```sh
    $ mkdir -p src/main/resources/ssl
    ```

1. Copy the `yugabytedb.crt.der` and `yugabytedb.key.pk8` certificates into the `ssl` directory created in the previous step.

## Create the sample Java application with TLS connection

1. Copy the following Java code to a new file named `src/main/java/com/yugabyte/HelloSqlSslApp.java`:

    ```java
    package com.yugabyte;

    import java.sql.Connection;
    import java.sql.DriverManager;
    import java.sql.ResultSet;
    import java.sql.SQLException;
    import java.sql.Statement;

    public class HelloSqlSslApp {
      public static void main(String[] args) throws ClassNotFoundException, SQLException {
        Class.forName("org.postgresql.Driver");
        Connection conn = DriverManager.getConnection("jdbc:postgresql://localhost:5433/yugabyte?ssl=true&sslmode=require&sslcert=src/main/resources/ssl/yugabytedb.crt.der&sslkey=src/main/resources/ssl/yugabytedb.key.pk8", "yugabyte", "yugabyte");
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

1. Build the project.

    ```sh
    $ mvn clean install
    ```

1. Run your new program.

    ```sh
    $ mvn -q package exec:java -DskipTests -Dexec.mainClass=com.yugabyte.HelloSqlSslApp
    ```

    You should see the following output:

    ```output
    Connected to the PostgreSQL server successfully.
    Created table employee
    Inserted data: INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'Java');
    Query returned: name=John, age=35, language: Java
    ```
