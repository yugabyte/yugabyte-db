---
title: Build a Node.js application that uses Sequelize ORM and YSQL
headerTitle: Build a Node.js application
description: Build a Node.js application that uses Sequelize ORM and YSQL.
menu:
  preview:
    parent: build-apps
    name: Node.js
    identifier: nodejs-4
    weight: 551
type: page
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="{{< relref "./ysql-pg.md" >}}" class="nav-link ">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - PostgreSQL driver
    </a>
  </li>
  <li >
    <a href="{{< relref "./ysql-sequelize.md" >}}" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - Sequelize
    </a>
  </li>
  <li>
    <a href="{{< relref "./ysql-prisma.md" >}}" class="nav-link active">
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

This tutorial assumes that you have installed YugabyteDB and created a cluster. Refer to [Quick Start](../../../../quick-start/).

## Clone the orm-examples repository

```sh
$ git clone https://github.com/yugabyte/orm-examples.git
```

This repository has a Node.js example that implements a REST API server. The scenario is that of an e-commerce application. Database access in this application is managed through the [Prisma](https://prisma.io). It consists of the following:

- The `users` table contains the users of the e-commerce site.
- The `products` table contains a list of products the e-commerce site sells.
- Orders placed by the users are populated in the `orders` table. An order can consist of multiple line items, each of these are inserted in the `orderline` table.

The application source is in the [repository](https://github.com/yugabyte/orm-examples/tree/master/node/prisma).

## Build the application

```sh
$ cd ./node/prisma/
```

```sh
npm install
```
## Specify the Configuration for the YugabyteDB Cluster 

Add the `DATABASE_URL` in the `.env` file to be used the example using:
```
echo 'DATABASE_URL="postgresql://<user>:<password>@<host>:<port>/<db_name>"' > .env 
```

## Apply the Migrations 

Create the tables in the YugabyteDB by applying the migration for the data models in the file `prisma/schema.prisma` using the following command and generate the <b>PrismaClient</b>: 
```
prisma migrate dev --name first_migration
```
 
## Run the application

Start the Node.js API server at <http://localhost:8080> :

```sh
$ npm start
```
Note: If your `PORT` 8080 is already in use, change the port using `export PORT=<new_port>`

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
  --data '{ "userId": "1", "products": [ { "productId": 1, "units": 2 } ] }' \
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
  ]
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
  ]
}
```

```sh
$ curl http://localhost:8080/orders
```

```output.json
{
  "content": [
    {
      "orderId": "999ae272-f2f4-46a1-bede-5ab765bb27fe",
      "userId": 2,
      "orderTotal": 25,
      "orderLines":[
        {
          "productId": 1,
          "quantity": 2
        },
        { 
          "productId": 2, 
          "quantity": 4
        }
      ]
    },
    {
      "orderId": "1598c8d4-1857-4725-a9ab-14deb089ab4e",
      "userId": 1,
      "orderTotal": 15,
      "orderLines":[
        {
          "productId": 1,
          "quantity": 2
        }
      ]
    }
  ]
}
```

## Explore the source

The application source is in the [orm-examples repository](https://github.com/yugabyte/orm-examples/tree/master/node/prisma).
