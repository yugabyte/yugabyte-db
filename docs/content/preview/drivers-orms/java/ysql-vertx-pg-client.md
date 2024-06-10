---
title: Vert.x PG Client for YSQL
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect a Java application using Vert.x PG driver
menu:
  preview:
    identifier: vertx-pg-client
    parent: java-drivers
    weight: 600
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
    <a href="../yb-r2dbc/" class="nav-link">
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
    <a href="../ysql-vertx-pg-client/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      Vert.x Pg Client
    </a>
  </li>

</ul>

The [Vert.x Pg Client](https://vertx.io/docs/vertx-pg-client/java/) driver for PostgreSQL is a reactive and non-blocking client for handling database connections with a single threaded API. Because YugabyteDB is wire-compatible with PostgreSQL, Vert.x PG Client is fully compatible with YugabyteDB.

## CRUD operations

The following sections demonstrate how to perform common tasks required for Java application development using the Vert.x PG Client.

To start building your application, make sure you have met the [prerequisites](../#prerequisites).

{{< note title="Note" >}}
The Vert.x PG Client `executeBatch()` API is supported in YugabyteDB version `2.15.2.0-b0` and later.
{{< /note >}}

### Step 1: Set up the client dependency

#### Maven dependency

If you are using [Maven](https://maven.apache.org/guides/development/guide-building-maven.html), add the following to your `pom.xml` of your project.

```xml
 <dependency>
     <groupId>io.vertx</groupId>
     <artifactId>vertx-pg-client</artifactId>
     <version>4.3.2</version>
 </dependency>
 ```

Install the added dependency using `mvn install`.

### Step 2: Set up the database connection

After setting up the dependencies, implement a Java client application that uses the Vert.x Pg client to connect to your YugabyteDB cluster and run a query on the sample data.

Java applications can connect to and query the YugabyteDB database using the `PgPool` class. The `io.vertx.*` package includes all the interfaces required for working with YugabyteDB.

Use the `PgPool.getConnection` method to create a connection object for the YugabyteDB Database. This can be used to perform DDLs and DMLs against the database.

The following table describes the connection parameters required to connect.

| Pg Client parameter | Description | Default |
| :------------------ | :---------- | :------ |
| setHost | Hostname of the YugabyteDB instance | localhost |
| setPort | Listen port for YSQL | 5433 |
| setDatabase | Database name | yugabyte |
| setUser | User connecting to the database | yugabyte |
| setPassword | User password | yugabyte |

### Step 3: Write your application

Create a new Java class called `QuickStartApp.java` in the base package directory of your project as follows:

```sh
touch ./src/main/java/com/yugabyte/QuickStartApp.java
```

Copy the following code to set up a YugabyteDB table and query the table contents from the Java client.

```java
package com.yugabyte;

import io.vertx.core.Promise;
import io.vertx.core.Vertx;
import io.vertx.pgclient.PgConnectOptions;
import io.vertx.pgclient.PgPool;
import io.vertx.sqlclient.PoolOptions;
import io.vertx.sqlclient.Tuple;
import io.vertx.sqlclient.Row;
import io.vertx.sqlclient.RowStream;

public class vertxPgExample {
    public static void main(String[] args) {

        PgConnectOptions options = new PgConnectOptions()
            .setPort(5433)
            .setHost("127.0.0.1")
            .setDatabase("yugabyte")
            .setUser("yugabyte")
            .setPassword("yugabyte");

        Vertx vertx = Vertx.vertx();
        // creating the PgPool with configuration as option and maxsize 10.
        PgPool pool = PgPool.pool(vertx, options, new PoolOptions().setMaxSize(10));

        //getting a connection from the pool and running the example on that
        pool.getConnection().compose(connection -> {
            Promise<Void> promise = Promise.promise();
            // create a test table
            connection.query("create table test(id int primary key, name text)").execute()
                    .compose(v -> {
                        // insert some test data
                        return connection.query("insert into test values (1, 'Hello'), (2, 'World'), (3,'Example'), (4, 'Vertx'), (5, 'Yugabyte')").execute();
                    })
                    .compose(v -> {
                        // prepare the query
                        return connection.prepare("select * from test order by id");
                    })
                    .map(preparedStatement -> {
                        // create a stream for the prepared statement
                        return preparedStatement.createStream(50, Tuple.tuple());
                    })
                    .onComplete(ar -> {
                        if (ar.succeeded()) {
                            RowStream<Row> stream = ar.result();
                            stream
                                    .exceptionHandler(promise::fail)
                                    .endHandler(promise::complete)
                                    .handler(row -> System.out.println(row.toJson())); // Printing each row as JSON
                        } else {
                            promise.fail(ar.cause());
                        }
                    });
            return promise.future().onComplete(v -> {
                // close the connection
                connection.close();
            });
        }).onComplete(ar -> {
            if (ar.succeeded()) {
                System.out.println("Example ran successfully!");
            } else {
                ar.cause().printStackTrace();
            }
        });

    }
}
```

Run the project `QuickStartApp.java` using the following command:

```sh
mvn -q package exec:java -DskipTests -Dexec.mainClass=com.yugabyte.QuickStartApp
```

You should see output similar to the following:

```output
{"id":1,"name":"Hello"}
{"id":2,"name":"World"}
{"id":3,"name":"Example"}
{"id":4,"name":"Vertx"}
{"id":5,"name":"Yugabyte"}
Example ran successfully!
```

## Limitation

[Pub/sub](https://vertx.io/docs/vertx-pg-client/java/#_pubsub) feature of Vert.x PG client is currently not supported with YugabyteDB. This limitation will go away when `LISTEN`/`NOTIFY` support is added to YugabyteDB. See [GitHub issue](https://github.com/yugabyte/yugabyte-db/issues/1872).

## Learn more

- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)
- [Develop Spring Boot applications using the YugabyteDB JDBC Driver](../../../integrations/spring-framework/sdyb/)
- Build Java applications using [Hibernate ORM](../hibernate/)
- Build Java applications using [Ebean ORM](../ebean/)
