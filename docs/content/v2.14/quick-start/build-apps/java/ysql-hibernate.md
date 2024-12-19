---
title: Build a Java application that uses YSQL
headerTitle: Build a Java application
linkTitle: Java
description: Build a sample Java application with Hibernate ORM and use the YSQL API to connect to and interact with YugabyteDB.
menu:
  v2.14:
    parent: build-apps
    name: Java
    identifier: java-8
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
    <a href="../ysql-jdbc-ssl/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - JDBC SSL/TLS
    </a>
  </li>
  <li >
    <a href="../ysql-hibernate/" class="nav-link active">
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

- YugabyteDB up and running. If you are new to YugabyteDB, you can download, install, and have YugabyteDB up and running within five minutes by following the steps in [Quick start](/preview/tutorials/quick-start/).
- Java Development Kit (JDK) 1.8, or later, is installed. JDK installers for Linux and macOS can be downloaded from [OpenJDK](http://jdk.java.net/), [AdoptOpenJDK](https://adoptopenjdk.net/), or [Azul Systems](https://www.azul.com/downloads/zulu-community/).
- [Apache Maven](https://maven.apache.org/index.html) 3.3, or later, is installed.

## Clone the "orm-examples" repository

```sh
$ git clone https://github.com/YugabyteDB-Samples/orm-examples.git
```

The [Using ORMs with YugabyteDB `orm-examples` repository](https://github.com/yugabyte/orm-examples) has a [Hibernate ORM](https://hibernate.org/orm/) example that implements a simple REST API server. The scenario is that of an e-commerce application. Database access in this application is managed through Hibernate ORM. It consists of the following:

- The users of the e-commerce site are stored in the `users` table.
- The `products` table contains a list of products the e-commerce site sells.
- The orders placed by the users are populated in the `orders` table. An order can consist of multiple line items, each of these are inserted in the `orderline` table.

The source for this example application can be found in the [repository](https://github.com/yugabyte/orm-examples/tree/master/java/hibernate).

There are a number of options that can be customized in the properties file located at `src/main/resources/hibernate.cfg.xml`. Given YSQL's compatibility with the PostgreSQL language, the `hibernate.dialect` property is set to `org.hibernate.dialect.PostgreSQLDialect` and the `hibernate.connection.url` is set to the YSQL JDBC URL: `jdbc:postgresql://localhost:5433/yugabyte`.

## Build the application

```sh
$ cd orm-examples/java/hibernate
```

```sh
$ mvn -DskipTests package
```

## Run the application

Start the Hibernate application's REST API server at `http://localhost:8080`.

```sh
$ mvn exec:java -Dexec.mainClass=com.yugabyte.hibernatedemo.server.BasicHttpServer
```

## Send requests to the application

Create 2 users.

```sh
$ curl --data '{ "firstName" : "John", "lastName" : "Smith", "email" : "jsmith@example.com" }' \
   -v -X POST -H 'Content-Type:application/json' http://localhost:8080/users
```

```sh
$ curl --data '{ "firstName" : "Tom", "lastName" : "Stewart", "email" : "tstewart@example.com" }' \
   -v -X POST -H 'Content-Type:application/json' http://localhost:8080/users
```

Create 2 products.

```sh
$ curl \
  --data '{ "productName": "Notebook", "description": "200 page notebook", "price": 7.50 }' \
  -v -X POST -H 'Content-Type:application/json' http://localhost:8080/products
```

```sh
$ curl \
  --data '{ "productName": "Pencil", "description": "Mechanical pencil", "price": 2.50 }' \
  -v -X POST -H 'Content-Type:application/json' http://localhost:8080/products
```

Create 2 orders.

```sh
$ curl \
  --data '{ "userId": "2", "products": [ { "productId": 1, "units": 2 } ] }' \
  -v -X POST -H 'Content-Type:application/json' http://localhost:8080/orders
```

```sh
$ curl \
  --data '{ "userId": "2", "products": [ { "productId": 1, "units": 2 }, { "productId": 2, "units": 4 } ] }' \
  -v -X POST -H 'Content-Type:application/json' http://localhost:8080/orders
```

## Query results

### Using the YSQL shell

```sh
$ ./bin/ysqlsh
```

```output
ysqlsh (11.2)
Type "help" for help.

yugabyte=#
```

List the tables created by the app.

```plpgsql
yugabyte=# \d
```

```output
List of relations
 Schema |          Name           |   Type   |  Owner
--------+-------------------------+----------+----------
 public | orderline               | table    | yugabyte
 public | orders                  | table    | yugabyte
 public | orders_user_id_seq      | sequence | yugabyte
 public | products                | table    | yugabyte
 public | products_product_id_seq | sequence | yugabyte
 public | users                   | table    | yugabyte
 public | users_user_id_seq       | sequence | yugabyte
(7 rows)
```

Note the 4 tables and 3 sequences in the list above.

```plpgsql
yugabyte=# SELECT count(*) FROM users;
```

```output
 count
-------
     2
(1 row)
```

```plpgsql
yugabyte=# SELECT count(*) FROM products;
```

```output
 count
-------
     2
(1 row)
```

```plpgsql
yugabyte=# SELECT count(*) FROM orders;
```

```output
 count
-------
     2
(1 row)
```

```plpgsql
yugabyte=# SELECT * FROM orderline;
```

```output
 order_id                             | product_id | units
--------------------------------------+------------+-------
 45659918-bbfd-4a75-a202-6feff13e186b |          1 |     2
 f19b64ec-359a-47c2-9014-3c324510c52c |          1 |     2
 f19b64ec-359a-47c2-9014-3c324510c52c |          2 |     4
(3 rows)
```

Note that `orderline` is a child table of the parent `orders` table connected using a foreign key constraint.

### Using the REST API

```sh
$ curl http://localhost:8080/users
```

```json
{
  "content": [
    {
      "userId": 2,
      "firstName": "Tom",
      "lastName": "Stewart",
      "email": "tstewart@example.com"
    },
    {
      "userId": 1,
      "firstName": "John",
      "lastName": "Smith",
      "email": "jsmith@example.com"
    }
  ],
  ...
}
```

```sh
$ curl http://localhost:8080/products
```

```json
{
  "content": [
    {
      "productId": 2,
      "productName": "Pencil",
      "description": "Mechanical pencil",
      "price": 2.5
    },
    {
      "productId": 1,
      "productName": "Notebook",
      "description": "200 page notebook",
      "price": 7.5
    }
  ],
  ...
}
```

```sh
$ curl http://localhost:8080/orders
```

```json
{
  "content": [
    {
      "orderTime": "2019-05-10T04:26:54.590+0000",
      "orderId": "999ae272-f2f4-46a1-bede-5ab765bb27fe",
      "user": {
        "userId": 2,
        "firstName": "Tom",
        "lastName": "Stewart",
        "email": "tstewart@example.com"
      },
      "userId": null,
      "orderTotal": 25,
      "products": []
    },
    {
      "orderTime": "2019-05-10T04:26:48.074+0000",
      "orderId": "1598c8d4-1857-4725-a9ab-14deb089ab4e",
      "user": {
        "userId": 2,
        "firstName": "Tom",
        "lastName": "Stewart",
        "email": "tstewart@example.com"
      },
      "userId": null,
      "orderTotal": 15,
      "products": []
    }
  ],
  ...
}
```

## Explore the source

As highlighted earlier, the source for the above application is available in the [orm-examples repository](https://github.com/yugabyte/orm-examples/tree/master/java/hibernate).
