---
title: PHP ORM example application
headerTitle: PHP ORM example application
linkTitle: PHP
description: PHP ORM example application.
menu:
  v2.18:
    identifier: php-orm
    parent: orm-tutorials
    weight: 660
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../ysql-laravel/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      Laravel ORM
    </a>
  </li>
</ul>

The following tutorial implements a REST API server using the [Laravel](https://laravel.com/docs/10.x/readme) web development framework. The scenario is that of an e-commerce application. Database access in this application is managed through Laravel's Eloquent ORM.

The source for this application can be found in the [Using ORMs with YugabyteDB](https://github.com/YugabyteDB-Samples/orm-examples/tree/master/php/laravel) repository.

## Prerequisites

This tutorial assumes that you have:

- YugabyteDB up and running. Download and install YugabyteDB by following the steps in [Quick start](/preview/tutorials/quick-start/).
- PHP 7.3+
- Laravel 8.40+
- Composer (Dependency Manager for PHP)

## Clone the "orm-examples" repository

```sh
$ git clone https://github.com/YugabyteDB-Samples/orm-examples.git
```

There are a number of options that can be customized in the `.env` file located in the Laravel project. Given YSQL's compatibility with the PostgreSQL language, the datasource `DB_CONNECTION` property is set to `pgsql`.

## Build the application

```sh
$ cd orm-examples/php/laravel
```

## Set up the database to use Laravel migrations

```sh
php artisan migrate:install
php artisan migrate:fresh
```

## Load the tables

```sh
php artisan db:seed
```

## Run the application

Start the Laravel application's REST API server at `http://localhost:8000`.

```sh
php artisan serve
```

## Send requests to the application

Create 2 users.

```sh
$ curl --data '{ "firstName" : "John", "lastName" : "Smith", "email" : "jsmith@example.com" }' \
   -v -X POST -H 'Content-Type:application/json' http://localhost:8000/users
```

```sh
$ curl --data '{ "firstName" : "Tom", "lastName" : "Stewart", "email" : "tstewart@example.com" }' \
   -v -X POST -H 'Content-Type:application/json' http://localhost:8000/users
```

Create 2 products.

```sh
$ curl \
  --data '{ "productName": "Notebook", "description": "200 page notebook", "price": 7.50 }' \
  -v -X POST -H 'Content-Type:application/json' http://localhost:8000/products
```

```sh
$ curl \
  --data '{ "productName": "Pencil", "description": "Mechanical pencil", "price": 2.50 }' \
  -v -X POST -H 'Content-Type:application/json' http://localhost:8000/products
```

Create 2 orders.

```sh
$ curl \
  --data '{ "userId": "2", "products": [ { "productId": 1, "units": 2 } ] }' \
  -v -X POST -H 'Content-Type:application/json' http://localhost:8000/orders
```

```sh
$ curl \
  --data '{ "userId": "2", "products": [ { "productId": 1, "units": 2 }, { "productId": 2, "units": 4 } ] }' \
  -v -X POST -H 'Content-Type:application/json' http://localhost:8000/orders
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

List the tables created by the app.

```plpgsql
yugabyte=# \d
```

```output
List of relations
 Schema |          Name           |   Type   |  Owner
--------+-------------------------+----------+----------
 public | orderline               | table    | yugabyte
 public | orders                  | table    | yugabyte
 public | orders_user_id_seq      | sequence | yugabyte
 public | products                | table    | yugabyte
 public | products_product_id_seq | sequence | yugabyte
 public | users                   | table    | yugabyte
 public | users_user_id_seq       | sequence | yugabyte
(7 rows)
```

Note the 4 tables and 3 sequences.

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

```plpgsql
yugabyte=# SELECT * FROM orderline;
```

```output
 order_id                             | product_id | units
--------------------------------------+------------+-------
 45659918-bbfd-4a75-a202-6feff13e186b |          1 |     2
 f19b64ec-359a-47c2-9014-3c324510c52c |          1 |     2
 f19b64ec-359a-47c2-9014-3c324510c52c |          2 |     4
(3 rows)
```

Note that `orderline` is a child table of the parent `orders` table connected using a foreign key constraint.

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