---
title: Java ORM example application that uses Ebean and YSQL
headerTitle: Java ORM example application
linkTitle: Java
description: Java ORM example application that uses Ebean and YSQL API to connect to and interact with YugabyteDB.
menu:
  v2.16:
    identifier: java-ebean
    parent: orm-tutorials
    weight: 640
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../ysql-hibernate/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      Hibernate ORM
    </a>
  </li>
  <li>
    <a href="../ysql-spring-data/" class="nav-link ">
      <i class="icon-postgres" aria-hidden="true"></i>
      Spring Data JPA
    </a>
  </li>
   <li>
    <a href="../ysql-ebean/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      Ebean ORM
    </a>
  </li>
</ul>

The following tutorial implements a REST API server using the Java [Ebean](https://ebean.io/docs/) ORM. The scenario is that of an e-commerce application where database access is managed using the [Play framework](https://www.playframework.com/documentation/2.8.x/api/java/index.html); Play uses [Akka](https://doc.akka.io/docs/akka/current/typed/guide/introduction.html) internally and exposes Akka Streams and actors in Websockets and other streaming HTTP responses.

The source for this application can be found in the [Using ORMs with YugabyteDB](https://github.com/yugabyte/orm-examples/tree/master/java/ebean) repository.

## Prerequisites

This tutorial assumes that you have:

- YugabyteDB up and running. Download and install YugabyteDB by following the steps in [Quick start](../../../../quick-start/).
- Java Development Kit (JDK) 1.8 is installed. JDK installers for Linux and macOS can be downloaded from [OpenJDK](http://jdk.java.net/), [AdoptOpenJDK](https://adoptopenjdk.net/), or [Azul Systems](https://www.azul.com/downloads/zulu-community/). Homebrew users on macOS can install using `brew install AdoptOpenJDK/openjdk/adoptopenjdk8`.
- [sbt](https://www.scala-sbt.org/1.x/docs/) is installed.

## Clone the "orm-examples" repository

```sh
$ git clone https://github.com/YugabyteDB-Samples/orm-examples.git && cd orm-examples/java/ebean
```

## Set up the database connection

- Modify the database configuration section of the `conf/application.conf` file to include the YugabyteDB JDBC driver settings as follows:

  ```sh
  #Default database configuration using PostgreSQL database engine
  default.username=yugabyte
  default.password=""
  default.driver=com.yugabyte.Driver
  default.url="jdbc:yugabytedb://127.0.0.1:5433/ysql_ebean?load-balance=true"
  ```

- Add a dependency in `build.sbt` for the YugabyteDB JDBC driver.

  ```sh
  libraryDependencies += "com.yugabyte" % "jdbc-yugabytedb" % "42.3.0"
  ```

- From your local YugabyteDB installation directory, connect to the [YSQL](../../../../admin/ysqlsh/) shell using:

  ```sh
  $ ./bin/ysqlsh
  ```

  ```output
  ysqlsh (11.2)
  Type "help" for help.

  yugabyte=#
  ```

- Create the `ysql_ebean` database using:

  ```sql
  yugabyte=# CREATE DATABASE ysql_ebean;
  ```

- Connect to the database using:

  ```sql
  yugabyte=# \c ysql_ebean;
  ```

## Build the application

Create a `build.properties` file under the `project` directory and add the sbt version.

```sh
sbt.version=1.2.8
```

Build the REST API server from the `ebean` directory using:

```sh
$ sbt compile
```

{{< note title="Note" >}}

- Some sub-versions of JDK 1.8 require the `nashorn` package. If you get a compile error due to a missing `jdk.nashorn` package, add the dependency to the `build.sbt` file.

  ```sh
  libraryDependencies += "com.xenoamess" % "nashorn" % "jdk8u265-b01-x3"
  ```

- To change the default port (8080) for the REST API Server, set the `PlayKeys.playDefaultPort` value in the `build.sbt` file.
{{< /note >}}

## Run the application

Run the application from the `ebean` directory using:

```sh
$ sbt run
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

In your YSQL shell, verify the `userId` and `productId` from the `ysql_ebean` database using the following YSQL commands.

```sql
ysql_ebean=# select * from users;
```

```output
 user_id | first_name | last_name |      user_email
---------+------------+-----------+----------------------
       1 | John       | Smith     | jsmith@example.com
     101 | Tom        | Stewart   | tstewart@example.com
(2 rows)
```

```sql
ysql_ebean=# select * from products;
```

```output
 product_id |    description    | price | product_name
------------+-------------------+-------+--------------
          1 | 200 page notebook |  7.50 | Notebook
          2 | Mechanical pencil |  2.50 | Pencil
(2 rows)
```

Create 2 orders using the `userId` for John.

```sh
$ curl \
  --data '{ "userId": "1", "products": [ { "productId": 1, "units": 2 } ] }' \
  -v -X POST -H 'Content-Type:application/json' http://localhost:8080/orders
```

```sh
$ curl \
  --data '{ "userId": "1", "products": [ { "productId": 1, "units": 2 }, { "productId": 2, "units": 4 } ] }' \
  -v -X POST -H 'Content-Type:application/json' http://localhost:8080/orders
```

## Query results

### Using the YSQL shell

In your YSQL shell, list the tables created by the application.

```sql
ysql_ebean=#  \d
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

```sql
ysql_ebean=# SELECT count(*) FROM users;
```

```output
 count
-------
     2
(1 row)
```

```sql
ysql_ebean=# SELECT count(*) FROM products;
```

```output
 count
-------
     2
(1 row)
```

```sql
ysql_ebean=# SELECT count(*) FROM orders;
```

```output
 count
-------
     2
(1 row)
```

```sql
ysql_ebean=# SELECT * FROM orderline;
```

```output
 order_id                             | product_id | units
--------------------------------------+------------+-------
 45659918-bbfd-4a75-a202-6feff13e186b |          1 |     2
 f19b64ec-359a-47c2-9014-3c324510c52c |          1 |     2
 f19b64ec-359a-47c2-9014-3c324510c52c |          2 |     4
(3 rows)
```

`orderline` is a child table of the parent `orders` table, and is connected using a foreign key constraint. The `users` table is connected with `orders` using a foreign key constraint so that no order can be placed with an invalid user, and that user has to be present in the users table.

### Using the REST API

To use the REST API server to verify that the users, products, and orders were created in the `ysql_ebean` database, enter the following commands. The results are output in JSON format.

```sh
$ curl http://localhost:8080/users
```

```output.json
{
  "content": [
    {
      "userId": 1,
      "firstName": "John",
      "lastName": "Smith",
      "email": "jsmith@example.com"
    },
    {
      "userId": 101,
      "firstName": "Tom",
      "lastName": "Stewart",
      "email": "tstewart@example.com"
    }
  ],
  ...
}
```

```sh
$ curl http://localhost:8080/products
```

```output.json
{
  "content": [
    {
      "productId": 1,
      "productName": "Notebook",
      "description": "200 page notebook",
      "price": 7.5
    },
    {
      "productId": 2,
      "productName": "Pencil",
      "description": "Mechanical pencil",
      "price": 2.5
    }
  ],
  ...
}
```

```sh
$ curl http://localhost:8080/orders
```

```output.json
{
  "content": [
    {
      "orderTime": "2019-05-10T04:26:54.590+0000",
      "orderId": "999ae272-f2f4-46a1-bede-5ab765bb27fe",
      "orderOwner": {
        "userId": 1,
        "firstName": "John",
        "lastName": "Smith",
        "email": "jsmith@example.com"
      },
      "userId": null,
      "orderTotal": 25.0,
      "products": null
    },
    {
      "orderTime": "2019-05-10T04:26:48.074+0000",
      "orderId": "1598c8d4-1857-4725-a9ab-14deb089ab4e",
      "orderOwner": {
        "userId": 1,
        "firstName": "John",
        "lastName": "Smith",
        "email": "jsmith@example.com"
      },
      "userId": null,
      "orderTotal": 15.0,
      "products": null
    }
  ],
  ...
}
```
