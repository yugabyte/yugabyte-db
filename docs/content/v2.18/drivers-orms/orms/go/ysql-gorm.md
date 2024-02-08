---
title: Go ORM example application that uses GORM and YSQL
headerTitle: Go ORM example application
linkTitle: Go
description: Go ORM example application that uses GORM and YSQL.
menu:
  v2.18:
    identifier: go-gorm
    parent: orm-tutorials
    weight: 660
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../ysql-gorm/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      GORM
    </a>
  </li>
</ul>

The following tutorial implements an ORM example using [GORM](https://gorm.io/), the ORM library for Golang, that implements a basic REST API server. The scenario is that of an e-commerce application. Database access in this application is managed using GORM.

The source for the above application can be found in the [repository](https://github.com/yugabyte/orm-examples/tree/master/golang/gorm). There are a number of options that can be customized in the properties file located at `src/config/config.json`.

## Prerequisites

This tutorial assumes that you have:

- YugabyteDB up and running. Download and install YugabyteDB by following the steps in [Quick start](../../../../quick-start/).
- Go 1.8, or later, is installed. The latest releases are available on the [Go Downloads page](https://golang.org/dl/).

### Go dependencies

To install the required Go dependencies, run the following commands.

```sh
go get github.com/jinzhu/gorm
go get github.com/jinzhu/gorm/dialects/postgres
go get github.com/google/uuid
go get github.com/gorilla/mux
go get github.com/lib/pq
go get github.com/lib/pq/hstore
```

## Clone the "orm-examples" repository

Clone the Yugabyte [`orm-examples` repository](https://github.com/yugabyte/orm-examples) by running the following command.

```sh
$ git clone https://github.com/YugabyteDB-Samples/orm-examples.git
```

Run the following `export` command to specify the `GOPATH` environment variable.

```sh
export GOPATH=$GOPATH:$HOME/orm-examples/golang/gorm
```

## Build and run the application

Change to the `gorm` directory.

```sh
$ cd ./golang/gorm
```

Create the `ysql_gorm` database in YugabyteDB by running the following `ysqlsh` command from the YugabyteDB home directory.

```sh
$ ./bin/ysqlsh -c "CREATE DATABASE ysql_gorm"
```

Build and start the REST API server by running the following shell script.

```sh
$ ./build-and-run.sh
```

The REST API server will start and listen for requests at `http://localhost:8080`.

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
