---
title: Build a Node.js application that uses Sequelize ORM and YSQL
headerTitle: Build a Node.js application
description: Build a Node.js application that uses Sequelize ORM and YSQL.
menu:
  v2.14:
    parent: build-apps
    name: Node.js
    identifier: nodejs-2
    weight: 551
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="{{< relref "./ysql-pg.md" >}}" class="nav-link ">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - PostgreSQL driver
    </a>
  </li>
  <li >
    <a href="{{< relref "./ysql-sequelize.md" >}}" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - Sequelize
    </a>
  </li>
  <li>
    <a href="{{< relref "./ysql-prisma.md" >}}" class="nav-link ">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YSQL - Prisma
    </a>
  </li>
  <li>
    <a href="{{< relref "./ycql.md" >}}" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

## Prerequisites

This tutorial assumes that you have installed:
- YugabyteDB and created a cluster. Refer to [Quick Start](../../../../quick-start/).
- [node.js](https://nodejs.org/en/) version 16 or later.

## Clone the orm-examples repository

```sh
$ git clone https://github.com/YugabyteDB-Samples/orm-examples.git
```

This repository has a Node.js example that implements a REST API server. The scenario is that of an e-commerce application. Database access in this application is managed through the Sequelize ORM. It consists of the following:

- The `users` table contains the users of the e-commerce site.
- The `products` table contains a list of products the e-commerce site sells.
- Orders placed by the users are populated in the `orders` table. An order can consist of multiple line items, each of these are inserted in the `orderline` table.

The application source is in the [repository](https://github.com/yugabyte/orm-examples/tree/master/node/sequelize). You can customize a number of options using the properties file located at `config/config.json`.

## Build the application

```sh
$ cd ./node/sequelize/
```

```sh
npm install
```

## Specifying SSL configuration
This configuration can be used while connecting to a YB Managed cluster or a local YB cluster with SSL enabled.

Use the configuration in the following way in the `models/index.js` file when you create the sequelize object:
```js
sequelize = new Sequelize("<db_name>", "<user_name>","<password>" , {
    dialect: 'postgres',
    port: 5433,
    host: "<host_name>",
    dialectOptions: {
        ssl: {
            rejectUnauthorized: true,
            ca: fs.readFileSync('<path_to_root_crt>').toString(),
        }
    }
  });
```

## Run the application

Start the Node.js API server at <http://localhost:8080> with DEBUG logs on.

```sh
$ DEBUG=sequelize:* npm start
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

### Using ysqlsh

```sh
$ ./bin/ysqlsh
```

```output
ysqlsh (11.2)
Type "help" for help.

yugabyte=#
```

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

### Using the REST API

```sh
$ curl http://localhost:8080/users
```

```output.json
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

```output.json
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

```output.json
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

The application source is in the [orm-examples repository](https://github.com/yugabyte/orm-examples/tree/master/node/sequelize).
