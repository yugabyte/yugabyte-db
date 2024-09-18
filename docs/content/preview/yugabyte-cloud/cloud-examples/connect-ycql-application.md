---
title: Connect a YCQL Java application
headerTitle: Connect a YCQL Java application
linkTitle: Connect a YCQL Java application
description: Build a sample Java application for YugabyteDB Aeon with the Yugabyte Java Driver for YCQL v4.6.
aliases:
  - /preview/yugabyte-cloud/cloud-develop/connect-ycql-application/
menu:
  preview_yugabyte-cloud:
    parent: cloud-examples
    identifier: connect-ycql-application
    weight: 200
type: docs
---

The following instructions show how you can build a Java application connected to YugabyteDB Aeon using the Yugabyte Java Driver for YCQL v4.6.

## Maven

To build a sample Java application with the [Yugabyte Java Driver for YCQL](https://github.com/yugabyte/cassandra-java-driver), add the following Maven dependency to your application:

```xml
 <dependency>
   <groupId>com.yugabyte</groupId>
   <artifactId>java-driver-core</artifactId>
   <version>4.6.0-yb-6</version>
 </dependency>
```

## Create the sample Java application

### Prerequisites

This tutorial assumes that you have the following:

- A YugabyteDB Aeon cluster, with your database credentials (username and password)
- JDK version 1.8 or later
- Maven 3.3 or later

Add your computer to the cluster IP allow list. Refer to [Assign IP allow lists](../../cloud-secure-clusters/add-connections).

You also need to download and install your YugabyteDB Aeon cluster CA certificate and obtain the cluster connection parameters as follows:

1. Sign in to YugabyteDB Aeon, select your cluster, and click **Connect**.

1. Click **Connect to your Application**.

1. Click **Download CA Cert** to download the cluster `root.crt` certificate to your computer.

1. Click **YCQL** to display the connection parameters. These include:

    - LocalDatacenter - The name of the local data center for the cluster.
    - Host - The cluster host name.
    - Port - The port number of the YCQL client API on the YugabyteDB database (9042).

### Create the project's POM

Create a Project Object Model (POM) file, named `pom.xml`, and then copy the following content into it. The POM includes configuration information required to build the project.

```xml
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
  <properties>
    <maven.compiler.source>1.8</maven.compiler.source>
    <maven.compiler.target>1.8</maven.compiler.target>
  </properties>

  <dependencies>
    <dependency>
      <groupId>com.yugabyte</groupId>
      <artifactId>java-driver-core</artifactId>
      <version>4.6.0-yb-6</version>
    </dependency>
  </dependencies>

  <build>
    <plugins>
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

### Write a sample Java application

Create the appropriate directory structure as expected by Maven.

```sh
$ mkdir -p src/main/java/com/yugabyte/sample/apps
```

Copy the following contents into the file `src/main/java/com/yugabyte/sample/apps/YBCqlHelloWorld.java`.

```java
package com.yugabyte.sample.apps;
import java.io.FileInputStream;
import java.net.InetSocketAddress;
import java.security.KeyStore;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.util.List;
import javax.net.ssl.SSLContext;
import javax.net.ssl.TrustManagerFactory;
import com.datastax.oss.driver.api.core.CqlSession;
import com.datastax.oss.driver.api.core.cql.ResultSet;
import com.datastax.oss.driver.api.core.cql.Row;

public class YBCqlHelloWorld {
    // Load the cluster root certificate
    private static SSLContext createSSLHandler(String certfile) {
        try {
            CertificateFactory cf = CertificateFactory.getInstance("X.509");
            FileInputStream fis = new FileInputStream(certfile);
            X509Certificate ca;
            try {
              ca = (X509Certificate) cf.generateCertificate(fis);
            } catch (Exception e) {
              System.err.println("Exception generating certificate from input file: " + e);
              return null;
            } finally {
              fis.close();
            }

            // Create a KeyStore containing our trusted CAs
            String keyStoreType = KeyStore.getDefaultType();
            KeyStore keyStore = KeyStore.getInstance(keyStoreType);
            keyStore.load(null, null);
            keyStore.setCertificateEntry("ca", ca);

            // Create a TrustManager that trusts the CAs in our KeyStore
            String tmfAlgorithm = TrustManagerFactory.getDefaultAlgorithm();
            TrustManagerFactory tmf = TrustManagerFactory.getInstance(tmfAlgorithm);
            tmf.init(keyStore);

            SSLContext sslContext = SSLContext.getInstance("TLS");
            sslContext.init(null, tmf.getTrustManagers(), null);
            return sslContext;
        } catch (Exception e) {
            System.err.println("Exception creating sslContext: " + e);
            return null;
        }
    }

    public static void main(String[] args) {
        try {
            if (args.length != 4) {
                System.out.println("Usage YBCqlHelloWorld " +
                    "<ip-address> <ssl_cert_path> <username> <password>");
                System.exit(-1);
            }

            // Create a YCQL client.
            CqlSession session = CqlSession
                .builder()
                .addContactPoint(new InetSocketAddress(args[0], 9042))
                .withSslContext(createSSLHandler(args[1]))
                .withAuthCredentials(args[2], args[3])
                .withLocalDatacenter("datacenter1")
                .build();
            // Create keyspace 'ybdemo' if it does not exist.
            String createKeyspace = "CREATE KEYSPACE IF NOT EXISTS ybdemo;";
            session.execute(createKeyspace);
            System.out.println("Created keyspace ybdemo");
            // Create table 'employee', if it does not exist.
            String createTable = "CREATE TABLE IF NOT EXISTS ybdemo.employee (id int PRIMARY KEY, " +
                "name varchar, " + "age int, " + "language varchar);";
            session.execute(createTable);
            System.out.println("Created table employee");
            // Insert a row.
            String insert = "INSERT INTO ybdemo.employee (id, name, age, language)" +
                " VALUES (1, 'John', 35, 'Java');";
            session.execute(insert);
            System.out.println("Inserted data: " + insert);
            // Query the row and print out the result.
            String select = "SELECT name, age, language FROM ybdemo.employee WHERE id = 1;";
            ResultSet selectResult = session.execute(select);
            List < Row > rows = selectResult.all();
            String name = rows.get(0).getString(0);
            int age = rows.get(0).getInt(1);
            String language = rows.get(0).getString(2);
            System.out.println("Query returned " + rows.size() + " row: " + "name=" + name +
                ", age=" + age + ", language: " + language);
            // Close the client.
            session.close();
        } catch (Exception e) {
            System.err.println("Error: " + e.getMessage());
        }
    }
}
```

Edit the `.withLocalDatacenter` line by replacing "datacenter1" with the LocalDatacenter from your cluster connection parameters.

You can also find the local data center name by running the following YCQL query from YugabyteDB Aeon Shell:

```sql
admin@ycqlsh:yugabyte> SELECT * FROM system.local;
```

### Add the root certificate to the project

To add the root certificate you downloaded to the application project, create a `resources` directory in your Java project.

```sh
$ mkdir -p src/main/resources
```

Then copy the root certificate `root.crt` into the `src/main/resources` directory.

### Build the project

To build the project, run the following `mvn package` command.

```sh
$ mvn package
```

You should see a `BUILD SUCCESS` message.

### Run the application

To use the application, run the following command.

```sh
$ java -cp "target/hello-world-1.0.jar:target/lib/*" \
    com.yugabyte.sample.apps.YBCqlHelloWorld \
    [YUGABYTE_CLOUD_HOSTNAME] [ROOT_CERT_PATH] [YCQL_USER] [YCQL_PASSWORD]
```

Replace the following command line variables with the appropriate connection parameters and database credentials:

| Variable | Description |
| :------- | :---------- |
| YUGABYTE_CLOUD_HOSTNAME | The hostname of your YugabyteDB Aeon cluster |
| ROOT_CERT_PATH | The path to root.crt |
| YCQL_USER | Your Yugabyte database username |
| YCQL_PASSWORD | Your Yugabyte database password |

For example:

```sh
$ java -cp "target/hello-world-1.0.jar:target/lib/*" \
    com.yugabyte.sample.apps.YBCqlHelloWorld \
    42424242-42d0-4c1a-b424-d42424ab2f42.aws.ybdb.io \
    src/main/resources/root.crt admin qwerty
```

You should see the following output:

```output
Created keyspace ybdemo
Created table employee
Inserted data: INSERT INTO ybdemo.employee (id, name, age, language) VALUES (1, 'John', 35, 'Java');
Query returned 1 row: name=John, age=35, language: Java
```
