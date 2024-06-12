---
title: Python ORM example application that uses SQLAlchemy and YSQL
headerTitle: Python ORM example application
linkTitle: Python
description: Python ORM example application that uses SQLAlchemy and YSQL.
menu:
  v2.20:
    identifier: python-sqlalchemy
    parent: orm-tutorials
    weight: 670
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../ysql-sqlalchemy/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      SQLAlchemy ORM
    </a>
  </li>
  <li>
    <a href="../ysql-django/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      Django ORM
    </a>
  </li>
</ul>

This SQLAlchemy ORM example, running on Python, implements a basic REST API server for an e-commerce application scenario. Database access in this application is managed through [SQL Alchemy ORM](https://docs.sqlalchemy.org/en/13/orm/).

The source for this application can be found in the [`python/sqlalchemy` directory](https://github.com/yugabyte/orm-examples/tree/master/python/sqlalchemy) of Yugabyte's [Using ORMs with YugabyteDB](https://github.com/yugabyte/orm-examples) GitHub repository.

## Prerequisites

This tutorial assumes that you have:

- YugabyteDB up and running. Download and install YugabyteDB by following the steps in [Quick start](../../../../quick-start/).
- Python 3 is installed
- the Python packages (dependencies) : [SQLAlchemy (`SQLAlchemy`)](https://www.sqlalchemy.org/), [psycopg2 (`psycopg2-binary`)](https://pypi.org/project/psycopg2/) and [JSONpickle (`jsonpickle`)](https://jsonpickle.github.io/) installed:

    To install these three packages, run the following command:

    ```sh
    $ pip3 install psycopg2-binary sqlalchemy jsonpickle
    ```

## Clone the "orm-examples" repository

Clone the Yugabyte [`orm-examples` repository](https://github.com/yugabyte/orm-examples) by running the following command.

```sh
$ git clone https://github.com/YugabyteDB-Samples/orm-examples.git
```

## Set up the database connection

Update the database settings in the `src/config.py` file to match the following. If YSQL authentication is enabled, add the password (default for the `yugabyte` user is `yugabyte`).

```python
import logging

listen_port = 8080
db_user = 'yugabyte'
db_password = 'yugabyte'
database = 'ysql_sqlalchemy'
schema = 'ysql_sqlalchemy'
db_host = 'localhost'
db_port = 5433

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s:%(levelname)s:%(message)s"
    )
```

## Start the REST API server

Run the following Python script to start the server.

```sh
python3 ./src/rest-service.py
```

The REST API server will start and listen for your requests at `http://localhost:8080`.

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
