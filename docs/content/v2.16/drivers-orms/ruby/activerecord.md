---
title: Use an ORM
linkTitle: Use an ORM
description: Active Record ORM support for YugabyteDB
menu:
  v2.16:
    identifier: activerecord-orm
    parent: ruby-drivers
    weight: 410
type: docs
---

[Active Record](https://guides.rubyonrails.org/active_record_basics.html) is an Object Relational Mapping (ORM) tool for Ruby applications.

YugabyteDB YSQL API has full compatibility with Active Record ORM for data persistence in Ruby applications.

To start building your application, make sure you have met the [prerequisites](../#prerequisites).

## CRUD operations

This page provides details for getting started with Active Record ORM for connecting to YugabyteDB using the [orm-examples](https://github.com/YugabyteDB-Samples/orm-examples.git) repository.

This repository has a Ruby on Rails example that implements a basic REST API server. The scenario is that of an e-commerce application. Database access in this application is managed through Active Record ORM. It consists of the following.

- The users of the e-commerce site are stored in the users table.
- The products table contains a list of products the e-commerce site sells.
- The orders placed by the users are populated in the orders table. An order can consist of multiple line items, each of these are inserted in the orderline table.

The source for the above application can be found in the [repository](https://github.com/yugabyte/orm-examples/tree/master/ruby/ror). There are options that can be customized in the properties file located at `config/database.yml`.

### Clone the orm-examples repository

```sh
$ git clone https://github.com/YugabyteDB-Samples/orm-examples.git
```

### Build and run the application

To install the dependencies specified in your project Gemfile, do the following:

```sh
$ cd ./orm-examples/ruby/ror/
```

```sh
$ ./bin/bundle install
```

Create a database using the following command:

```sh
$ bin/rails db:create
```

You should see output similar to the following:

```output
Created database 'ysql_active_record'
```

Perform migration to create tables and add columns using the following command:

```sh
$ bin/rails db:migrate
```

Start the rails server using the following command:

```sh
$ bin/rails server
```

### Send requests to the application

Send requests to the application from another terminal as follows:

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

### Query results

#### Using the YSQL shell

```sh
$ ./bin/ysqlsh
```

```output
ysqlsh (11.2)
Type "help" for help.

yugabyte=#
```

Connect to the database mentioned in `config/database.yml` file. Default is `ysql_active_record`.

```sql
yugabyte=# \c ysql_active_record
```

```sql
ysql_active_record=# SELECT count(*) FROM users;
```

```output
 count
-------
     2
(1 row)
```

```sql
ysql_active_record=# SELECT count(*) FROM products;
```

```output
 count
-------
     2
(1 row)
```

```sql
ysql_active_record=# SELECT count(*) FROM orders;
```

```output
 count
-------
     2
(1 row)
```

#### Using the REST API

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
      "userId": 2,
      "orderTotal": 25,
      "products": []
    },
    {
      "orderTime": "2019-05-10T04:26:48.074+0000",
      "orderId": "1598c8d4-1857-4725-a9ab-14deb089ab4e",
      "userId": 2,
      "orderTotal": 15,
      "products": []
    }
  ],
  ...
}
```

## Learn more

- Build Ruby applications using [Pg Gem Driver](../ysql-pg/)
- Build Ruby applications using [YugabyteDB Ruby Driver for YCQL](../ycql/)