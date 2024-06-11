---
title: Python ORM example application that uses YSQL and Django
headerTitle: Python ORM example application
linkTitle: Python
description: Python ORM example application with Django that uses YSQL.
menu:
  stable:
    identifier: python-django
    parent: orm-tutorials
    weight: 680
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../ysql-sqlalchemy/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      SQLAlchemy ORM
    </a>
  </li>
  <li>
    <a href="../ysql-django/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      Django ORM
    </a>
  </li>
</ul>

The following tutorial implements a REST API server using the [Django](https://www.djangoproject.com/) ORM. The scenario is that of an e-commerce application where database access is managed using the ORM.

The source for the above application can be found in the `python/django` directory of Yugabyte's [Using ORMs with YugabyteDB](https://github.com/yugabyte/orm-examples) repository.

## Prerequisites

This tutorial assumes that you have:

- YugabyteDB up and running. Download and install YugabyteDB by following the steps in [Quick start](/preview/quick-start/).
- [Python 3](https://www.python.org/downloads/) or later is installed.
- [Django 2.2](https://www.djangoproject.com/download/) or later is installed.

## Clone the orm-examples repository

```sh
$ git clone https://github.com/YugabyteDB-Samples/orm-examples.git
```

## Set up the database connection

- Customize the database connection setting according to your environment in the `ybstore/settings.py` file. This file is in the `orm-examples/python/django` directory.

    ```python
    DATABASES =
    {
        'default':
        {
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

- Create a database using the YugabyteDB YSQL shell (ysqlsh). From the location of your local YugabyteDB cluster, run the following shell command:

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
