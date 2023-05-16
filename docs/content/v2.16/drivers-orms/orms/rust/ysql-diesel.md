---
title: Rust ORM example application that uses Diesel and YSQL
headerTitle: Rust ORM example application
linkTitle: Rust
description: Rust ORM example application that uses Diesel and YSQL API to connect to and interact with YugabyteDB.
menu:
  v2.16:
    identifier: rust-diesel
    parent: orm-tutorials
    weight: 730
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../ysql-diesel/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      Diesel ORM
    </a>
  </li>

</ul>

The following tutorial implements a REST API server using the Rust [Diesel](https://diesel.rs) ORM. The scenario is that of an e-commerce application where database access is managed using the [Entity Framework Core](https://docs.microsoft.com/en-us/ef/core/).

The source for the application can be found in the [Using ORMs with YugabyteDB](https://github.com/yugabyte/orm-examples/tree/master/rust/diesel) repository.

## Prerequisites

This tutorial assumes that you have:

- YugabyteDB up and running. Download and install YugabyteDB by following the steps in [Quick start](../../../../quick-start/).
- [Rust](https://www.rust-lang.org/tools/install) 1.31 or later.

## Clone the "orm-examples" repository

```sh
$ git clone https://github.com/YugabyteDB-Samples/orm-examples.git && cd orm-examples/rust/diesel
```

Build the REST API server (written using Diesel and Rocket) as follows:

```sh
$ cargo build --release
```

If you encounter a build failure, install [libpq](../../../../reference/drivers/ysql-client-drivers/#libpq) and try again.

## Set up the database connection

The database connection settings are managed using the `DATABASE_URL` in the `.env` file, which is in the following format:

```sh
DATABASE_URL=postgres://<username>:<password>@<host>:<port>/<database>
```

| Property | Description | Default |
| :--- | :--- | :--- |
| host | Database server IP address or DNS name. | localhost |
| port | Database port where it accepts YSQL connections. | 5433 |
| username | Database username. | yugabyte |
| password | User password. | yugabyte |
| database | Database instance. | ysql_diesel |

The default values are valid for a local YugabyteDB installation. If you are using a different configuration, change these values in the URL as required.

From your local YugabyteDB installation directory, connect to the [YSQL](../../../admin/ysqlsh/) shell using:

```sh
$ ./bin/ysqlsh
```

```output
ysqlsh (11.2)
Type "help" for help.

yugabyte=#
```

Create the `ysql_diesel` database using:

```sql
yugabyte=# CREATE DATABASE ysql_diesel;
```

Connect to the database using:

```sql
yugabyte=# \c ysql_diesel;
```

## Start the REST API server

In the `diesel` directory, start REST server at port 8080.

```sh
$ ./target/release/yugadiesel
```

## Send requests to the application

Create two users.

```sh
$ curl --data '{ "firstName" : "John", "lastName" : "Smith", "email" : "jsmith@example.com" }' \
   -v -X POST -H 'Content-Type:application/json' http://localhost:8080/users
```

```sh
$ curl --data '{ "firstName" : "Tom", "lastName" : "Stewart", "email" : "tstewart@example.com" }' \
   -v -X POST -H 'Content-Type:application/json' http://localhost:8080/users
```

Create two products.

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

In your YSQL shell, verify the `userId` and `productId` from the `ysql_diesel` database using the following YSQL commands:

```sql
ysql_diesel=# select * from users;
```

```output
 user_id | first_name | last_name |      user_email
---------+------------+-----------+----------------------
       1 | John       | Smith     | jsmith@example.com
     101 | Tom        | Stewart   | tstewart@example.com
(2 rows)
```

```sql
ysql_diesel=# select * from products;
```

```output
 product_id |    description    | price | product_name
------------+-------------------+-------+--------------
          1 | 200 page notebook |  7.50 | Notebook
          2 | Mechanical pencil |  2.50 | Pencil
(2 rows)
```

Create two orders using the `userId` for John.

```sh
$ curl \
  --data '{ "userId": 1, "products": [ { "productId": 1, "units": 2 } ] }' \
  -v -X POST -H 'Content-Type:application/json' http://localhost:8080/orders
```

```sh
$ curl \
  --data '{ "userId": 1, "products": [ { "productId": 1, "units": 2 }, { "productId": 2, "units": 4 } ] }' \
  -v -X POST -H 'Content-Type:application/json' http://localhost:8080/orders
```

## Query results

### Using the YSQL shell

In your YSQL shell, list the tables created by the application.

```sql
ysql_diesel=#  \d
```

```output
                      List of relations
 Schema |             Name              |   Type   |  Owner
--------+-------------------------------+----------+----------
 public | __diesel_schema_migrations    | table    | yugabyte
 public | order_lines                   | table    | yugabyte
 public | order_lines_order_id_seq      | sequence | yugabyte
 public | order_lines_order_line_id_seq | sequence | yugabyte
 public | order_lines_product_id_seq    | sequence | yugabyte
 public | orders                        | table    | yugabyte
 public | orders_order_id_seq           | sequence | yugabyte
 public | orders_user_id_seq            | sequence | yugabyte
 public | products                      | table    | yugabyte
 public | products_product_id_seq       | sequence | yugabyte
 public | users                         | table    | yugabyte
 public | users_user_id_seq             | sequence | yugabyte
(12 rows)
```

Note the 4 tables and 3 sequences in the list above.

```sql
ysql_diesel=# SELECT count(*) FROM users;
```

```output
 count
-------
     2
(1 row)
```

```sql
ysql_diesel=# SELECT count(*) FROM products;
```

```output
 count
-------
     2
(1 row)
```

```sql
ysql_diesel=# SELECT count(*) FROM orders;
```

```output
 count
-------
     2
(1 row)
```

```sql
ysql_diesel=# SELECT * FROM order_lines;
```

```output
 order_line_id | order_id | product_id | units
---------------+----------+------------+-------
             1 |        1 |          1 |     2
             2 |        2 |          1 |     2
             3 |        2 |          2 |     4
(3 rows)
```

`order_lines` is a child table of the parent `orders` table, and is connected using a foreign key constraint. The `users` table is connected with `orders` using a foreign key constraint so that no order can be placed with an invalid user, and that user has to be present in the users table.

### Using the REST API

To use the REST API server to verify that the users, products, and orders were created in the `ysql_diesel` database, enter the following commands. The results are output in JSON format.

```sh
curl http://localhost:8080/users
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
curl http://localhost:8080/products
```

```output.json
{
  "content": [
    {
      "productId": 1,
      "productName": "Notebook",
      "price": 7.5
    },
    {
      "productId": 2,
      "productName": "Pencil",
      "price": 2.5
    }
  ],
  ...
}
```

```sh
curl http://localhost:8080/orders
```

```output.json
{
  "content": [
    {
      "orderId": 1,
      "orderTotal": "15.00",
      "userId": 1
    },
    {
      "orderId": 2,
      "orderTotal": "25.00",
      "userId": 1
    }
  ]
}
```
