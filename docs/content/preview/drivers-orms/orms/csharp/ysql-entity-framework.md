---
title: C# ORM example application that uses Entity Framework and YSQL
headerTitle: C# ORM example application
linkTitle: C#
description: C# ORM example application that uses Entity Framework and the YSQL API.
menu:
  preview:
    identifier: csharp-ef
    parent: orm-tutorials
    weight: 710
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../ysql-entity-framework/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      Entity Framework ORM
    </a>
  </li>
  <li>
    <a href="../ysql-dapper/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      Dapper ORM
    </a>
  </li>
</ul>

The following tutorial implements a REST API server using the [Entity Framework](https://docs.microsoft.com/en-us/ef/) ORM. The scenario is that of an e-commerce application where database access is managed using the [Entity Framework Core](https://docs.microsoft.com/en-us/ef/core/).

The source for the above application can be found in the [Using ORMs with YugabyteDB](https://github.com/yugabyte/orm-examples/tree/master/csharp/entityframework) repository.

## Prerequisites

This tutorial assumes that you have:

- YugabyteDB up and running. Download and install YugabyteDB by following the steps in [Quick start](../../../../quick-start/).
- [.NET SDK 6.0](https://dotnet.microsoft.com/en-us/download) or later.

## Clone the "orm-examples" repository

```sh
$ git clone https://github.com/YugabyteDB-Samples/orm-examples.git && cd orm-examples/csharp/entityframework
```

## Set up the database connection

To modify the database connection settings, change the default `ConnectionStrings` in `appsettings.json` file which is in the following format:

`Host=$hostName; Port=$dbPort; Username=$dbUser; Password=$dbPassword; Database=$database`

| Properties | Description | Default |
| :--------- | :---------- | :------ |
| Host | Database server IP address or DNS name. | 127.0.0.1 |
| Port | Database port where it accepts client connections. | 5433 |
| Username | The username to connect to the database. | yugabyte |
| Password | The password to connect to the database. | yugabyte |
| Database | Database instance in database server. | ysql_entityframework |

## Start the REST API server

To change default port for REST API Server, go to `Properties/launchSettings.json` and change the `applicationUrl` field.

- Build the REST API server.

  ```sh
  $ dotnet build
  ```

- Run the REST API server

  ```sh
  $ dotnet run
  ```

The REST server will run at `http://localhost:8080` by default.

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

Verify the `userId` and `productId` from the database using the following YSQL commands.

```sh
yugabyte=# \c ysql_entityframework
```

```output
You are now connected to database "ysql_entityframework" as user "yugabyte".
```

```sql
ysql_entityframework=# SELECT * FROM users;
```

```output
 user_id | first_name | last_name |      user_email
---------+------------+-----------+----------------------
       1 | John       | Smith     | jsmith@example.com
     101 | Tom        | Stewart   | tstewart@example.com
(2 rows)
```

```sql
ysql_entityframework=# SELECT * FROM products;
```

```output
 product_id |    description    | price | product_name
------------+-------------------+-------+--------------
          1 | 200 page notebook |  7.50 | Notebook
        101 | Mechanical pencil |  2.50 | Pencil
(2 rows)
```

Create 2 orders with products using the `userId` for John.

```sh
$ curl \
  --data '{ "userId": "1", "products": [ { "productId": 1, "units": 2 } ] }' \
  -v -X POST -H 'Content-Type:application/json' http://localhost:8080/orders
```

```sh
$ curl \
  --data '{ "userId": "1", "products": [ { "productId": 1, "units": 2 }, { "productId": 101, "units": 4 } ] }' \
  -v -X POST -H 'Content-Type:application/json' http://localhost:8080/orders
```

## Query results

### Use the YSQL shell

```sql
ysql_entityframework=# SELECT count(*) FROM users;
```

```output
 count
-------
     2
(1 row)
```

```sql
ysql_entityframework=# SELECT count(*) FROM products;
```

```output
 count
-------
     2
(1 row)
```

```sql
ysql_entityframework=# SELECT count(*) FROM orders;
```

```output
 count
-------
     2
(1 row)
```

### Use the REST API

To use the REST API server to verify that the users, products, and orders were created in the `ysql_entityframework` database, enter the following commands. The results are output in JSON format.

```sh
$ curl http://localhost:8080/users
```

```output.json
{
  "content": [
    {
      "userId": 101,
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

```output.json
{
  "content": [
    {
      "productId": 101,
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

```output.json
{
  "content": [
    {
      "orderId":"2692e1e9-0bbd-40e8-bf51-4fbcc4e9fea2",
      "orderTime":"2022-02-24T02:32:52.60555",
      "orderTotal":15.00,
      "userId":1,
      "users":null,
      "products":null
    },
    {
      "orderId":"f7343f22-7dfc-4a18-b4d3-9fcd17161518",
      "orderTime":"2022-02-24T02:33:06.832663",
      "orderTotal":25.00,
      "userId":1,
      "users":null,
      "products":null
    }
  ]
}
```
