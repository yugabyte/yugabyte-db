---
title: YugabyteDB JDBC Driver
linkTitle: YugabyteDB JDBC Driver
description: YugabyteDB JDBC Driver
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
section: REFERENCE
menu:
  latest:
    identifier: yugabytedb-jdbc-driver
    parent: connectors
    weight: 2920
---

The YugabyteDB JDBC Driver is based on the [PostgreSQL JDBC Driver](https://github.com/pgjdbc/pgjdbc). The YugabyteDB implementation adds a `YBClusterAwareDataSource` that requires only an initial _contact point_ for the YugabyteDB cluster. Then it discovers the rest of the nodes and automatically responds to nodes being started, stopped, added, or removed. Internally, the driver maintains a connection pool for each node and chooses a live node to get a connection. Whenever the connection is closed, the connection will be returned to the respective pool.

## Get the YugabyteDB JDBC Driver

### From Maven

Add the following lines to your Apache Maven project.

```
<dependency>
  <groupId>com.yugabyte</groupId>
  <artifactId>jdbc-yugabytedb</artifactId>
  <version>42.2.7-yb-3</version>
</dependency>
```

## Use the driver

1. Create the data source by passing an initial contact point.

    ```java
    String jdbcUrl = "jdbc:postgresql://127.0.0.1:5433/yugabyte";
    YBClusterAwareDataSource ds = new YBClusterAwareDataSource(jdbcUrl);
    ```

2. Use like a regular connection pooling data source.

    ```java
    // Using try-with-resources to auto-close the connection when done.
    try (Connection connection = ds.getConnection()) {
        // Use the connection as usual.
    } catch (java.sql.SQLException e) {
        // Handle/Report error.
    }
    ```

## Develop and test locally

1. Clone the [YugabyteDB JDBC Driver GitHub repository](https://github.com/yugabyte/jdbc-yugabytedb).

    ```sh
    git clone https://github.com/yugabyte/jdbc-yugabytedb.git && cd jdbc-yugabytedb
    ```

2. Build and install into your local Maven directory.

    ```sh
     mvn clean install -DskipTests
    ```

3. Add the lines below to your project.

    ```xml
    <dependency>
        <groupId>com.yugabyte</groupId>
        <artifactId>jdbc-yugabytedb</artifactId>
        <version>42.2.7-yb-3-SNAPSHOT</version>
    </dependency>
    ```
