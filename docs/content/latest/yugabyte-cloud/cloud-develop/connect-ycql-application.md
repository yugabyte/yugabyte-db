---
title: Connect a YCQL Java application
headerTitle: Connect a YCQL Java application
linkTitle: Connect a YCQL Java application
description: Build a sample Java application with the Yugabyte Java Driver for YCQL v4.6.
menu:
  latest:
    parent: cloud-develop
    identifier: connect-ycql-application
    weight: 550
type: page
isTocNested: true
showAsideToc: true
---

The following instructions show how you can build a Java application connected to Yugabyte Cloud using the Yugabyte Java Driver for YCQL v4.6.

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

This tutorial assumes that you have:

- a Yugabyte Cloud cluster, with your database credentials (username and password).
- installed JDK version 1.8 or later.
- installed Maven 3.3 or later.

You also need to download and install your Yugabyte Cloud cluster CA certificate as follows:

1. Sign in to Yugabyte Cloud, select your cluster, and click **Connect**.

1. Click **Connect to your Application**.

1. Click **Download CA Cert** to download the cluster `root.crt` certificate to your computer.

1. Generate a truststore using the downloaded root certificate.

    ```sh
    $ keytool -keystore ybtruststore -alias ybtruststore -import -file root.crt
    ```

1. Note the truststore password, it will be needed during application implementation.

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

Initialize a custom SSLContext by loading the truststore created with the Yugabyte Cloud root certificate. You do this by adding the following `YugabyteSSLContext` class to your project. Copy the following contents into the file `src/main/java/com/yugabyte/sample/apps/YugabyteSSLContext.java`.

```java
package com.yugabyte.sample.apps;

import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.security.KeyStore;
import java.security.SecureRandom;

import javax.net.ssl.SSLContext;
import javax.net.ssl.TrustManagerFactory;

public class YugabyteSSLContext {

  public SSLContext getSSLcontext(String trustStorePath, String trustStorePassword) throws Exception {
    TrustManagerFactory tmf = null;
    try (InputStream tsf = Files.newInputStream(Paths.get(trustStorePath))) {
      KeyStore ts = KeyStore.getInstance("JKS");
      ts.load(tsf, trustStorePassword.toCharArray());
      tmf = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
      tmf.init(ts);
    }
    
    SSLContext sslContext = SSLContext.getInstance("SSL");
    sslContext.init(null, tmf != null ? tmf.getTrustManagers() : null, new SecureRandom());
    
    return sslContext;
  }
}
```

Copy the following contents into the file `src/main/java/com/yugabyte/sample/apps/YBCqlHelloWorld.java`.

```java
package com.yugabyte.sample.apps;
import java.net.InetSocketAddress;
import java.util.List;
import com.datastax.oss.driver.api.core.CqlSession;
import com.datastax.oss.driver.api.core.cql.ResultSet;
import com.datastax.oss.driver.api.core.cql.Row;
public class YBCqlHelloWorld {
    private static final String YUGABYTE_TRUSTSTORE_PATH = "src/main/resource/ybtruststore";
    private static final String YUGABYTE_TRUSTSTORE_PASSWORD = "";
    private static final String YCQL_USER = "";
    private static final String YCQL_PASSWORD = "";
    private static final String YUGABYTE_CLOUD_HOSTNAME = "";
    public static void main(String[] args) {
        try {
            // Instantiate YugabyteSSLContext and pass in the truststore details.
            YugabyteSSLContext ybSSLContext = new YugabyteSSLContext();
            SSLContext sslContext = ybSSLContext.getSSLcontext(YUGABYTE_TRUSTSTORE_PATH, YUGABYTE_TRUSTSTORE_PASSWORD);

            // Create a YCQL client.
            CqlSession session = CqlSession
                .builder()
                .addContactPoint(new InetSocketAddress(YUGABYTE_CLOUD_HOSTNAME, 9042))
                .withLocalDatacenter("datacenter1")
                .withSslContext(sslContext)
                .withAuthCredentials(YCQL_USER, YCQL_PASSWORD)
                .build();
            // Create keyspace 'ybdemo' if it does not exist.
            String createKeyspace = "CREATE KEYSPACE IF NOT EXISTS ybdemo;";
            session.execute(createKeyspace);
            System.out.println("Created keyspace ybdemo");
            // Create table 'employee', if it does not exist.
            String createTable = "CREATE TABLE IF NOT EXISTS ybdemo.employee (id int PRIMARY KEY, " + "name varchar, " +
                "age int, " + "language varchar);";
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
            System.out.println("Query returned " + rows.size() + " row: " + "name=" + name + ", age=" + age +
                ", language: " + language);
            // Close the client.
            session.close();
        } catch (Exception e) {
            System.err.println("Error: " + e.getMessage());
        }
    }
}
```

Replace the following variables in `YBCqlHelloWorld.java` with the appropriate values:

| Variable | Description |
|----------|-------------|
| LocalDatacenter | The name of the local data center |
| YUGABYTE_CLOUD_HOSTNAME | The hostname of your Yugabyte Cloud cluster |
| YUGABYTE_TRUSTSTORE_PASSWORD | The password for the truststore |
| YCQL_USER | Your Yugabyte database user name |
| YCQL_PASSWORD | Your Yugabyte database password |

To determine the `local` data center to use, run the following YCQL query from Yugabyte Cloud Shell:

```sql
admin@ycqlsh:yugabyte> SELECT * FROM system.local;
```

### Add the trustore to the project

To add the truststore you created to the application project, create a `resources` directory in your Java project.

```sh
$ mkdir -p src/main/resources
```

Then copy the truststore `ybtruststore` into the `src/main/resources` directory.

### Build the project

To build the project, run the following `mvn package` command.

```sh
$ mvn package
```

You should see a `BUILD SUCCESS` message.

### Run the application

To use the application, run the following command.

```sh
$ java -cp "target/hello-world-1.0.jar:target/lib/*" com.yugabyte.sample.apps.YBCqlHelloWorld
```

You should see the following as the output.

```output
Created keyspace ybdemo
Created table employee
Inserted data: INSERT INTO ybdemo.employee (id, name, age, language) VALUES (1, 'John', 35, 'Java');
Query returned 1 row: name=John, age=35, language: Java
```
