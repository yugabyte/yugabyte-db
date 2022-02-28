---
title: Build a C# application that uses Entity Framework and YSQL
headerTitle: Build a C# application
linkTitle: C#
description: Build a C# application that uses Entity Framework and the YSQL API .
menu:
  latest:
    identifier: build-apps-cpp-3-ysql
    parent: build-apps
    weight: 556
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../ysql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li>
    <a href="../ysql-entity-framework/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - Entity Framework
    </a>
  </li>
  <li>
    <a href="../ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

The following tutorial implements a REST API server using the [Entity Framework](https://docs.microsoft.com/en-us/ef/) ORM. The scenario is that of an e-commerce application where database access is managed using the [Entity Framework Core](https://docs.microsoft.com/en-us/ef/core/). It includes the following tables:

- `users` — the users of the e-commerce site
- `products` — the products being sold
- `orders` — the orders placed by the users
- `orderline` — each line item of an order

The source for the above application can be found in the [Using ORMs with YugabyteDB](https://github.com/yugabyte/orm-examples/tree/master/csharp/entityframework) repository.

## Prerequisites

The tutorial assumes that you have:

- YugabyteDB up and running. If you are new to YugabyteDB, follow the steps in [Quick start](../../../../quick-start/) to have YugabyteDB up and running in minutes.

- [.NET](https://dotnet.microsoft.com/en-us/download/dotnet/2.0)

## Clone the "orm-examples" repository

```sh
$ git clone https://github.com/yugabyte/orm-examples.git && cd orm-examples/csharp/entityframework
```

## Database configuration

- To modify the database connection settings, change the default `ConnectionStrings` in `appsettings.json` file which is in the following format:

```sh
Host=$hostName; Port=$dbPort; Username=$dbUser; Password=$dbPassword; Database=$database
```

| Properties | Description | Default |
| :--------- | :---------- | :------ |
| Host | Database server IP address or DNS name. | 127.0.0.1 |
| Port | Database port where it accepts client connections. | 5433 |
| Username | The username to connect to the database. | yugabyte |
| Password | The password to connect to the database. Leave the password field blank. |
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
yugabyte=# select * from users;
```

```output
 user_id | first_name | last_name |      user_email
---------+------------+-----------+----------------------
       1 | John       | Smith     | jsmith@example.com
     101 | Tom        | Stewart   | tstewart@example.com
(2 rows)
```

```sh
yugabyte=# select * from products;
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

```sh
$ ./bin/ysqlsh
```

```output
ysqlsh (11.2)
Type "help" for help.
yugabyte=#
```

```sql
yugabyte=# SELECT count(*) FROM users;
```

```output
 count
-------
     2
(1 row)
```

```sql
yugabyte=# SELECT count(*) FROM products;
```

```output
 count
-------
     2
(1 row)
```

```sql
yugabyte=# SELECT count(*) FROM orders;
```

```output
 count
-------
     2
(1 row)
```

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

The application source is available in the [orm-examples](https://github.com/yugabyte/orm-examples/tree/master/csharp/entityframework) repository.
