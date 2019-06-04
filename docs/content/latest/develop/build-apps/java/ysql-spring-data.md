---
title: Build a Java App
linkTitle: Build a Java App
description: Build a Java App
menu:
  latest:
    parent: build-apps
    name: Java
    identifier: java-2
    weight: 550
type: page
isTocNested: true
showAsideToc: true
---


<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/latest/develop/build-apps/java/ysql-jdbc" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - JDBC
    </a>
  </li>
  <li >
    <a href="/latest/develop/build-apps/java/ysql-spring-data" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - Spring Data JPA with Hibernate
    </a>
  </li>
  <li>
    <a href="/latest/develop/build-apps/java/ycql" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

## Prerequisites

This tutorial assumes that you have:

- installed YugaByte DB and created a universe with YSQL enabled. If not, please follow these steps in the [Quick Start guide](../../../../quick-start/explore-ysql/).
- installed JDK version 1.8+ and maven 3.3+


## Clone the orm-examples repo

```sh
$ git clone https://github.com/YugaByte/orm-examples.git
```

This repository has a Spring Boot example that implements a simple REST API server. The scenario is that of an e-commerce application. Database access in this application is managed through Spring Data JPA which internally uses Hibernate as the JPA provider. It consists of the following.

- The users of the e-commerce site are stored in the users table.
- The products table contains a list of products the e-commerce site sells.
- The orders placed by the users are populated in the orders table. An order can consist of multiple line items, each of these are inserted in the orderline table.

The source for the above application can be found in the [repo](https://github.com/YugaByte/orm-examples/tree/master/java/spring/src/main/java/com/yugabyte/springdemo).

There are a number of options that can be customized in the properties file located at `src/main/resources/application.properties`. Given YSQL's compatibility with the PostgreSQL language, the `spring.jpa.database` property is set to `POSTGRESQL` and the `spring.datasource.url` is set to the YSQL JDBC url `jdbc:postgresql://localhost:5433/postgres`.


## Build the app

```sh
$ cd ./java/spring
```

```sh
$ mvn -DskipTests package
```

## Run the app

Bring the Sprint Boot REST API server at  http://localhost:8080

```sh
$ mvn spring-boot:run
```

## Send requests to the app


Create 2 users.
```sh
$ curl --data '{ "firstName" : "John", "lastName" : "Smith", "email" : "jsmith@yb.com" }' \
   -v -X POST -H 'Content-Type:application/json' http://localhost:8080/users
```
```sh
$ curl --data '{ "firstName" : "Tom", "lastName" : "Stewart", "email" : "tstewart@yb.com" }' \
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
```
ysqlsh (11.2)
Type "help" for help.

postgres=#
```
```sql
postgres=> SELECT count(*) FROM users;
```
```
 count 
-------
     2
(1 row)
```

```sql
postgres=> SELECT count(*) FROM products;
```
```
 count 
-------
     2
(1 row)
```

```sql
postgres=> SELECT count(*) FROM orders;
```
```
 count 
-------
     2
(1 row)
```

### Using the REST API

```sh
$ curl http://localhost:8080/users
```
```
{
  "content": [
    {
      "userId": 2,
      "firstName": "Tom",
      "lastName": "Stewart",
      "email": "tstewart@yb.com"
    },
    {
      "userId": 1,
      "firstName": "John",
      "lastName": "Smith",
      "email": "jsmith@yb.com"
    }
  ],
  ...
}  
```


```sh
$ curl http://localhost:8080/products
```
```
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
```
{
  "content": [
    {
      "orderTime": "2019-05-10T04:26:54.590+0000",
      "orderId": "999ae272-f2f4-46a1-bede-5ab765bb27fe",
      "user": {
        "userId": 2,
        "firstName": "Tom",
        "lastName": "Stewart",
        "email": "tstewart@yb.com"
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
        "email": "tstewart@yb.com"
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

As highlighted earlier, the source for the above application can be found in the [orm-examples](https://github.com/YugaByte/orm-examples/tree/master/java/spring/src/main/java/com/yugabyte/springdemo) repo.
