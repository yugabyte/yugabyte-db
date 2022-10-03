---
title: Build a Java application that uses YSQL
headerTitle: Build a Java application
linkTitle: More examples
description: Build a sample Java application with the Vert.x PG Client and use the YSQL API to connect to and interact with YugabyteDB.
menu:
  preview:
    parent: cloud-java
    identifier: java-10
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
    <a href="../ysql-vertx-pg-client/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - Vert.x PG Client
    </a>
  </li>
  <li >
    <a href="../ysql-jdbc-ssl/" class="nav-link">
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
</ul>

[Vert.x PG Client](https://vertx.io/docs/vertx-pg-client/java/) is the client for PostgreSQL with simple APIs to communicate with the database. It is a reactive and non-blocking client for handling the database connections with a single threaded API.

Since YugabyteDB is wire compatible with PostgreSQL, Vert.x PG Client works with YugabyteDB as well.

## Prerequisites

This tutorial assumes that:

- YugabyteDB up and running. Download and install the latest version of YugabyteDB by following the steps in [Quick start](../../../../quick-start/).
{{< note title="Note" >}}
The `executeBatch()` API of the Vert.x PG Client is supported in the YugabyteDB version - `2.15.2.0-b0` onwards.
{{< /note >}}
- Java Development Kit (JDK) 1.8, or later, is installed.
- [Apache Maven](https://maven.apache.org/index.html) 3.3 or later, is installed.

## Create and configure the Java project

1. Create a project called "vertx-pg-example".

    ```sh
    $ mvn archetype:generate \
        -DgroupId=com.yugabyte \
        -DartifactId=vertx-pg-example \
        -DarchetypeArtifactId=maven-archetype-quickstart \
        -DinteractiveMode=false

    $ cd vertx-pg-example
    ```
1. Add the following properties in the `pom.xml` file within the `<project>` element.

    ```xml
    <properties>
      <maven.compiler.source>1.8</maven.compiler.source>
      <maven.compiler.target>1.8</maven.compiler.target>
    </properties>
    ```

1. Add the following dependency for the Vert.x PG Client within the `<dependencies>` element in the `pom.xml` file.

    ```xml
    <dependency>
        <groupId>io.vertx</groupId>
        <artifactId>vertx-pg-client</artifactId>
        <version>4.3.2</version>
    </dependency>
    ```

1. Install the added dependency.

    ```sh
    $ mvn install
    ```

## Create the sample Java application

1. Copy the following Java code to a new file `src/main/java/com/yugabyte/vertxPgExample.java`:

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

1. Run the program.

    ```sh
    $ mvn -q package exec:java -DskipTests -Dexec.mainClass=com.yugabyte.vertxPgExample
    ```

    You should see the following as the output:

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

