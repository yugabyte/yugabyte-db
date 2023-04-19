---
title: Build a Python application that uses YSQL and Django
headerTitle: Build a Python application
linkTitle: Python
description: Build a Python application with Django that uses YSQL.
menu:
  v2.14:
    parent: build-apps
    name: Python
    identifier: python-4
    weight: 553
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="{{< relref "./ysql-psycopg2.md" >}}" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - psycopg2
    </a>
  </li>
  <li >
    <a href="{{< relref "./ysql-aiopg.md" >}}" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - aiopg
    </a>
  </li>
  <li >
    <a href="{{< relref "./ysql-sqlalchemy.md" >}}" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - SQL Alchemy
    </a>
  </li>
  <li>
    <a href="{{< relref "./ycql.md" >}}" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
  <li>
    <a href="{{< relref "./ysql-django.md" >}}" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - Django
    </a>
  </li>
</ul>

The following tutorial creates an e-commerce application running in Python, connects to a YugabyteDB cluster, and performs REST API calls to send requests and query the results.

## Before you begin

This tutorial assumes that you have satisfied the following prerequisites.

### YugabyteDB

YugabyteDB is up and running. If you are new to YugabyteDB, you can have YugabyteDB up and running within five minutes by following the steps in [Quick start](../../../../quick-start/).

### Python

[Python 3](https://www.python.org/downloads/) or later is installed.

### Django

[Django 2.2](https://www.djangoproject.com/download/) or later is installed.

## Clone the orm-examples repository

```sh
$ git clone https://github.com/YugabyteDB-Samples/orm-examples.git
```

This repository has a Django ORM example that implements a simple REST API server. Database access in this application is managed through the Django ORM. The e-commerce database `ysql_django` includes the following tables:

- `users` stores users of the e-commerce site.
- `products` contains a list of products the e-commerce site sells.
- `orders` contains orders placed by the users.
- `orderline` stores multiple line items from an order.

The source for the above application can be found in the `python/django` directory of Yugabyte's [Using ORMs with YugabyteDB](https://github.com/yugabyte/orm-examples) repository.

## Set up the application

- Customize the database connection setting according to your environment in the `ybstore/settings.py` file. This file is in the `orm-examples/python/django` directory.

```python
DATABASES = {
    'default': {
        'ENGINE': 'django.db.backends.postgresql',
        'NAME': 'ysql_django',
        'USER': 'yugabyte',
        'PASSWORD': '',
        'HOST': '127.0.0.1',
        'PORT': '5433',
    }
}
```

- Generate a [Django secret key](https://docs.djangoproject.com/en/dev/ref/settings/#secret-key) and paste the generated key in the following line of the `settings.py` file:

```python
SECRET_KEY = 'YOUR-SECRET-KEY'
```

- Create a database using the YugabyteDB YSQL shell (ysqlsh). From the location of your local [YugabyteDB](#yugabytedb) cluster, run the following shell command:

```sh
bin/ysqlsh -c "CREATE DATABASE ysql_django"
```

- From the `orm-examples/python/django` directory, run the following command to create the migrations and migrate the changes to the database:

```sh
python3 manage.py makemigrations && python3 manage.py migrate
```

## Start the REST API server

Run the following Python script to start the REST API server at port 8080, or specify a port of your own choice.

```sh
python3 manage.py runserver 8080
```

The REST API server starts and listens for your requests at `http://localhost:8080`.

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

## Explore the source

The source for the above application can be found in the [orm-examples repository](https://github.com/yugabyte/orm-examples/tree/master/python/django).
