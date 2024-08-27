---
title: YugabyteDB R2DBC Smart Driver
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect a Java application using YugabyteDB R2DBC Smart Driver for YSQL
badges: ysql
menu:
  preview:
    identifier: r2dbc-driver
    parent: java-drivers
    weight: 200
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li class="active">
    <a href="../yugabyte-jdbc/" class="nav-link">
      YSQL
    </a>
  </li>
  <li>
    <a href="../ycql/" class="nav-link">
      YCQL
    </a>
  </li>
</ul>

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../yugabyte-jdbc/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YugabyteDB JDBC Smart Driver
    </a>
  </li>

  <li >
    <a href="../yb-r2dbc/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YugabyteDB R2DBC Smart Driver
    </a>
  </li>

  <li >
    <a href="../postgres-jdbc/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL JDBC Driver
    </a>
  </li>

  <li >
    <a href="../ysql-vertx-pg-client/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      Vert.x Pg Client
    </a>
  </li>
</ul>

The [YugabyteDB R2DBC Smart Driver](https://github.com/yugabyte/r2dbc-postgresql) is an asynchronous Java driver for [YSQL](../../../api/ysql/) built on the [PostgreSQL R2DBC driver](https://github.com/pgjdbc/r2dbc-postgresql), with additional [connection load balancing](../../smart-drivers/) features.

{{< note title="YugabyteDB Aeon" >}}

To use smart driver load balancing features when connecting to clusters in YugabyteDB Aeon, applications must be deployed in a VPC that has been peered with the cluster VPC. For applications that access the cluster from outside the VPC network, use the upstream PostgreSQL driver instead; in this case, the cluster performs the load balancing. Applications that use smart drivers from outside the VPC network fall back to the upstream driver behaviour automatically. For more information, refer to [Using smart drivers with YugabyteDB Aeon](../../smart-drivers/#using-smart-drivers-with-yugabytedb-aeon).

{{< /note >}}

## CRUD operations

The following sections demonstrate how to perform common tasks required for Java application development using the YugabyteDB R2DBC smart driver.

To start building your application, make sure you have met the [prerequisites](../#prerequisites).

### Step 1: Set up the client dependency

#### Maven dependency

If you are using [Maven](https://maven.apache.org/guides/development/guide-building-maven.html), add the following to your `pom.xml` of your project.

```xml
<dependency>
  <groupId>com.yugabyte</groupId>
  <artifactId>r2dbc-postgresql</artifactId>
  <version>1.1.0-yb-1-ea</version>
</dependency>
```

Install the added dependency using `mvn install`.

### Step 2: Set up the database connection

The following table describes the connection parameters required to connect, including [smart driver parameters](../../smart-drivers/) for uniform and topology load balancing.

| Parameter | Description | Default |
| :-------- | :---------- | :------ |
| addHost | Host name of the YugabyteDB instance. You can also enter [multiple addresses](#use-multiple-addresses). | localhost |
| port | Listen port for YSQL | 5433 |
| database | Database name | yugabyte |
| username | User connecting to the database | yugabyte |
| password | User password | yugabyte |
| `loadBalanceHosts` | [Uniform load balancing](../../smart-drivers/#cluster-aware-connection-load-balancing) | Defaults to upstream driver behavior unless set to 'true' |
| `topologyKeys` | [Topology-aware load balancing](../../smart-drivers/#topology-aware-connection-load-balancing) | If `loadBalanceHosts` is true, uses uniform load balancing unless set to comma-separated geo-locations in the form `cloud.region.zone`. |
| `ybServersRefreshInterval` | If loadBalanceHosts is true, the interval in seconds to refresh the servers list | 300 |

You can provide the connection details in one of the following ways:

- URL

  ```sh
  "r2dbc:postgresql://username:password@addHost:port/database?loadBalanceHosts=true"
  ```

- Configuration builder

  ```sh
  PostgresqlConnectionFactory connectionFactory = new PostgresqlConnectionFactory(PostgresqlConnectionConfiguration.builder()
        .addHost("127.0.0.3")
        .username("yugabyte")
        .password("yugabyte")
        .database("yugabyte")
        .loadBalanceHosts(true)
        .ybServersRefreshInterval(10)
        .build());
  ```

After the driver establishes the initial connection, it fetches the list of available servers from the cluster, and load-balances subsequent connection requests across these servers.

#### Use multiple addresses

You can specify multiple hosts in the connection string to provide alternative options during the initial connection in case the primary address fails.

You can add multiple hosts in the configuration builder using the `addHost()` function as follows:

```sh
PostgresqlConnectionFactory connectionFactory = new PostgresqlConnectionFactory(PostgresqlConnectionConfiguration.builder()
        .addHost("host1", port1)
        .addHost("host2", port2)
        .username("yugabyte")
        .password("yugabyte")
        .database("yugabyte")
        .loadBalanceHosts(true)
        .ybServersRefreshInterval(10)
        .build());
```

The hosts are only used during the initial connection attempt. If the first host is down when the driver is connecting, the driver attempts to connect to the next host in the string, and so on.

### Step 2: Write your application

1. Create a new Java class called `QuickStartApp.java` in the base package directory of your project as follows:

    ```sh
    touch ./src/main/java/com/yugabyte/QuickStartApp.java
    ```

1. Copy the following code to set up a YugabyteDB table and query the table contents from the Java client.

    ```java
    package org.example;
    import io.r2dbc.postgresql.PostgresqlConnectionConfiguration;
    import io.r2dbc.postgresql.PostgresqlConnectionFactory;
    import io.r2dbc.postgresql.api.PostgresqlConnection;
    import io.r2dbc.spi.*;
    import reactor.core.publisher.Flux;
    import reactor.core.publisher.Mono;
    public class QuickStartApp {
        public static void main(String[] args) {
            // Configure connection to Postgres
            PostgresqlConnectionConfiguration config = PostgresqlConnectionConfiguration.builder()
                .addHost("127.0.0.1")
                .username("yugabyte")
                .password("yugabyte")
                .database("yugabyte")
                .loadBalanceHosts(true)
                .ybServersRefreshInterval(10)
                .build();
            // Create a connection factory
            PostgresqlConnectionFactory connectionFactory = new PostgresqlConnectionFactory(config);
            // Connect to the database
            Mono<PostgresqlConnection> connectionMono = connectionFactory.create();
            // Perform database operations
            connectionMono
                .flatMapMany(connection -> {
                    // Create a table
                    return executeStatement(connection, "CREATE TABLE IF NOT EXISTS employees (id SERIAL PRIMARY KEY, name VARCHAR(255), age int, language VARCHAR(255))")
                        .thenMany(Flux.range(1, 5)
                            .flatMap(i -> executeStatement(connection, "INSERT INTO employees (id,name,age,language) VALUES (" + i + ", 'John', " + (i + 35) + ", 'JAVA')")));
                })
                .thenMany(connectionMono.flatMapMany(connection -> {
                    // Retrieve inserted data
                    return connection.createStatement("SELECT * FROM employees")
                        .execute()
                            .flatMap(result -> {
                                return Flux.from(result.map((row, metadata) -> {
                                    int id = row.get("id", Integer.class);
                                    String name = row.get("name", String.class);
                                    int age = row.get("age", Integer.class);
                                    String lang = row.get("language", String.class);
                                    return "ID: " + id + ", Name: " + name + ", Age: " + age + ", Language: " + lang ;
                                }));
                            });
                }))
                .doOnNext(System.out::println)
                .blockLast(); // Block to keep the program running until all operations are completed
        }
        private static Mono<? extends Result> executeStatement(PostgresqlConnection connection, String sql) {
            Statement statement = connection.createStatement(sql);
            return Flux.from(statement.execute()).next();
        }
    }
    ```

## Run the application

Run the project `QuickStartApp.java` using the following command:

```sh
mvn -q package exec:java -DskipTests -Dexec.mainClass=com.yugabyte.QuickStartApp
```

You should see output similar to the following:

```text
ID: 5, Name: John, Age: 40, Language: JAVA
ID: 1, Name: John, Age: 36, Language: JAVA
ID: 4, Name: John, Age: 39, Language: JAVA
ID: 2, Name: John, Age: 37, Language: JAVA
ID: 3, Name: John, Age: 38, Language: JAVA
```

## Learn more

- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)
- [Smart Driver architecture](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/smart-driver.md)
- [Develop Spring Boot applications using the YugabyteDB JDBC Driver](../../../integrations/spring-framework/sdyb/)
- Build Java applications using [Hibernate ORM](../hibernate/)
- Build Java applications using [Ebean ORM](../ebean/)
