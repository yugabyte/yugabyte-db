---
title: Retail analytics sample database
headerTitle: Retail analytics sample database
linkTitle: Retail Analytics
description: Explore this retail analytics sample database on YugabyteDB using YSQL.
menu:
  v2.18:
    identifier: retail-analytics
    parent: sample-data
    weight: 500
type: docs
---

Install the PostgreSQL-compatible Retail Analytics dataset on the YugabyteDB distributed SQL database.

You can install and use the Retail Analytics sample database using:

- A local installation of YugabyteDB. To install YugabyteDB, refer to [Quick Start](../../quick-start/).
- Using cloud shell or a client shell to connect to a cluster in YugabyteDB Aeon. Refer to [Connect to clusters in YugabyteDB Aeon](../../yugabyte-cloud/cloud-connect/). To get started with YugabyteDB Aeon, refer to [Quick Start](../../yugabyte-cloud/cloud-quickstart/).

In either case, you use the YugabyteDB SQL shell ([ysqlsh](../../admin/ysqlsh/)) CLI to interact with YugabyteDB using [YSQL](../../api/ysql/).

## About the Retail Analytics database

The Retail Analytics dataset includes sample data in the following tables:

- **Products**: Product information
- **Users**: Customers who have bought products
- **Orders**: Orders made by customers
- **Reviews**: Product reviews

## Install the Retail Analytics sample database

The Retail Analytics SQL scripts reside in the `share` folder of your YugabyteDB or client shell installation. They can also be found in the `sample` directory of the [YugabyteDB GitHub repository](https://github.com/yugabyte/yugabyte-db/tree/master/sample). The following files will be used for this exercise:

- [schema.sql](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/schema.sql) — Creates the tables and constraints
- [orders.sql](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/orders.sql) — Loads product orders
- [products.sql](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/products.sql) — Loads products
- [reviews.sql](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/reviews.sql) — Loads product reviews
- [users.sql](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/users.sql) — Loads customer information

Follow the steps here to install the Retail Analytics sample database.

### Open the YSQL shell

If you are using a local installation of YugabyteDB, run the `ysqlsh` command from the `yugabyte` root directory.

```sh
$ ./bin/ysqlsh
```

If you are connecting to YugabyteDB Aeon, open the [ysqlsh cloud shell](../../yugabyte-cloud/cloud-connect/connect-cloud-shell/), or [run the YSQL connection string](../../yugabyte-cloud/cloud-connect/connect-client-shell/#ysqlsh) for your cluster from the `yugabyte-client` bin directory.

### Create a database

You can do this as shown below.

```sql
yugabyte=# CREATE DATABASE yb_demo;
```

```sql
yugabyte=# GRANT ALL ON DATABASE yb_demo to yugabyte;
```

```sql
yugabyte=# \c yb_demo;
```

### Load data

First create the four tables necessary to store the data.

```sql
yb_demo=# \i share/schema.sql;
```

Now load the data into the tables.

```sql
\i share/products.sql;
\i share/users.sql;
\i share/orders.sql;
\i share/reviews.sql;
```

## Explore the Retail Analytics database

Display the schema of the `products` table as follows:

```sql
yb_demo=# \d products
```

```output
                                        Table "public.products"
   Column   |            Type             | Collation | Nullable |               Default
------------+-----------------------------+-----------+----------+--------------------------------------
 id         | bigint                      |           | not null | nextval('products_id_seq'::regclass)
 created_at | timestamp without time zone |           |          |
 category   | text                        |           |          |
 ean        | text                        |           |          |
 price      | double precision            |           |          |
 quantity   | integer                     |           |          | 5000
 rating     | double precision            |           |          |
 title      | text                        |           |          |
 vendor     | text                        |           |          |
Indexes:
    "products_pkey" PRIMARY KEY, lsm (id HASH)
```

### Simple queries

To see how many products there are in this table, run the following query.

```sql
yb_demo=# SELECT count(*) FROM products;
```

```output
 count
-------
   200
(1 row)
```

The following query selects the `id`, `title`, `category`, `price`, and `rating` columns for the first five products.


```sql
yb_demo=# SELECT id, title, category, price, rating
          FROM products
          LIMIT 5;
```

```output
 id  |           title            | category |      price       | rating
-----+----------------------------+----------+------------------+--------
  22 | Enormous Marble Shoes      | Gizmo    | 21.4245199604423 |    4.2
  38 | Lightweight Leather Gloves | Gadget   | 44.0462485589292 |    3.8
 162 | Gorgeous Copper Knife      | Gadget   | 22.3785988001101 |    3.3
 174 | Rustic Iron Keyboard       | Gadget   | 74.4095392945406 |    4.4
  46 | Rustic Linen Keyboard      | Gadget   | 78.6996782532274 |      4
(5 rows)
```

To view the next 3 products, add an `OFFSET 5` clause to start from the fifth product.

```sql
yb_demo=# SELECT id, title, category, price, rating
          FROM products
          LIMIT 3 OFFSET 5;
```

```output
 id  |           title           | category  |      price       | rating
-----+---------------------------+-----------+------------------+--------
 152 | Enormous Aluminum Clock   | Widget    | 32.5971248660044 |    3.6
   3 | Synergistic Granite Chair | Doohickey | 35.3887448815391 |      4
 197 | Aerodynamic Concrete Lamp | Gizmo     | 46.7640712447334 |    4.6
(3 rows)
```

### The JOIN clause

Use a JOIN clause to combine rows from two or more tables, based on a related column between them.

The following JOIN query selects the `total` column from the `orders` table, and for each of these orders, fetches the `id`, `name`, and `email` from the `users` table of the corresponding users that placed those orders. The related column between the two tables is the user's id.

```sql
yb_demo=# SELECT users.id, users.name, users.email, orders.id, orders.total
          FROM orders INNER JOIN users ON orders.user_id=users.id
          LIMIT 10;
```

```output
  id  |        name         |             email             |  id   |      total
------+---------------------+-------------------------------+-------+------------------
  616 | Rex Thiel           | rex-thiel@gmail.com           |  4443 | 101.414602060277
 2289 | Alanis Kovacek      | alanis.kovacek@yahoo.com      | 17195 | 71.8499366564206
   37 | Jaleel Collins      | jaleel.collins@gmail.com      |   212 | 38.8821451022809
 2164 | Cordia Farrell      | cordia.farrell@gmail.com      | 16223 | 37.7489430287531
 1528 | Donny Murazik       | murazik-donny@hotmail.com     | 11546 | 52.3082273751586
 1389 | Henriette O'Connell | connell-o-henriette@yahoo.com | 10551 | 69.3117644687696
 2408 | Blake Jast          | jast.blake@hotmail.com        | 18149 | 150.788925887077
 1201 | Kaycee Keebler      | kaycee-keebler@gmail.com      |  8937 | 48.3440955866708
 1421 | Cornell Cartwright  | cornell-cartwright@gmail.com  | 10772 | 191.867670306882
  523 | Deonte Hoeger       | hoeger.deonte@hotmail.com     |  3710 | 71.4010754169826
(10 rows)
```

### Distributed transactions

To track quantities accurately, each product being ordered in some quantity by a user has to decrement the corresponding product inventory quantity. These operations should be performed inside a transaction.

Imagine the user with id `1` wants to order `10` units of the product with id `2`.

Before running the transaction, you can verify the quantity of product `2` in stock by running the following query:

```sql
yb_demo=# SELECT id, category, price, quantity FROM products WHERE id=2;
```

```output
SELECT id, category, price, quantity FROM products WHERE id=2;
 id | category  |      price       | quantity
----+-----------+------------------+----------
  2 | Doohickey | 70.0798961307176 |     5000
(1 row)
```

To place the order, run the following transaction:

```sql
yb_demo=# BEGIN TRANSACTION;

/* First insert a new order into the orders table. */
INSERT INTO orders
  (id, created_at, user_id, product_id, discount, quantity, subtotal, tax, total)
VALUES (
  (SELECT max(id)+1 FROM orders)                 /* id */,
  now()                                          /* created_at */,
  1                                              /* user_id */,
  2                                              /* product_id */,
  0                                              /* discount */,
  10                                             /* quantity */,
  (10 * (SELECT price FROM products WHERE id=2)) /* subtotal */,
  0                                              /* tax */,
  (10 * (SELECT price FROM products WHERE id=2)) /* total */
) RETURNING id;

/* Next decrement the total quantity from the products table. */
UPDATE products SET quantity = quantity - 10 WHERE id = 2;

COMMIT;
```

Verify that the order got inserted by running the following command:

```sql
yb_demo=# select * from orders where id = (select max(id) from orders);
```

```output
  id   |         created_at         | user_id | product_id | discount | quantity |     subtotal     | tax |      total
-------+----------------------------+---------+------------+----------+----------+------------------+-----+------------------
 18761 | 2020-01-30 09:24:29.784078 |       1 |          2 |        0 |       10 | 700.798961307176 |   0 | 700.798961307176
(1 row)
```

To verify that total quantity of product id `2` in the inventory has been updated, run the following query:

```sql
yb_demo=# SELECT id, category, price, quantity FROM products WHERE id=2;
```

```output
 id | category  |      price       | quantity
----+-----------+------------------+----------
  2 | Doohickey | 70.0798961307176 |     4990
(1 row)
```

### Built-in functions

YSQL supports a rich set of built-in functions.

To find out how users are signing up for the site, list the unique set of `source` channels present in the database using the `DISTINCT` function, as follows:

```sql
yb_demo=# SELECT DISTINCT(source) FROM users;
```

```output
source
-----------
 Facebook
 Twitter
 Organic
 Affiliate
 Google
(5 rows)
```

Use the `MIN`, `MAX`, and `AVG` functions to show prices of products in the store, as follows:

```sql
yb_demo=# SELECT MIN(price), MAX(price), AVG(price) FROM products;
```

```output
min               |       max        |       avg
------------------+------------------+------------------
 15.6919436739704 | 98.8193368436819 | 55.7463996679207
(1 row)
```

### Aggregations

Use the `GROUP BY` clause to aggregate data.

To determine the most effective channel for user sign ups, run the following command:

```sql
yb_demo=# SELECT source, count(*) AS num_user_signups
          FROM users
          GROUP BY source
          ORDER BY num_user_signups DESC;
```

```output
source     | num_user_signups
-----------+------------------
 Facebook  |              512
 Affiliate |              506
 Google    |              503
 Twitter   |              495
 Organic   |              484
(5 rows)
```

Discover the most effective channel for product sales by revenue by running the following command:

```sql
yb_demo=# SELECT source, ROUND(SUM(orders.total)) AS total_sales
          FROM users LEFT JOIN orders ON users.id=orders.user_id
          GROUP BY source
          ORDER BY total_sales DESC;
```

```output
  source   | total_sales
-----------+-------------
 Facebook  |      333454
 Google    |      325184
 Twitter   |      320150
 Organic   |      319637
 Affiliate |      297605
(5 rows)
```

### Views

To answer questions such as what percentage of the total sales is from the Facebook channel, you can create a view.

```sql
yb_demo=# CREATE VIEW channel AS
            (SELECT source, ROUND(SUM(orders.total)) AS total_sales
             FROM users LEFT JOIN orders ON users.id=orders.user_id
             GROUP BY source
             ORDER BY total_sales DESC);
```

Now that the view is created, you can see it in the list of relations.

```sql
yb_demo=# \d
```

```output
               List of relations
 Schema |      Name       |   Type   |  Owner
--------+-----------------+----------+----------
 public | channel         | view     | yugabyte
 public | orders          | table    | yugabyte
 public | orders_id_seq   | sequence | yugabyte
 public | products        | table    | yugabyte
 public | products_id_seq | sequence | yugabyte
 public | reviews         | table    | yugabyte
 public | reviews_id_seq  | sequence | yugabyte
 public | users           | table    | yugabyte
 public | users_id_seq    | sequence | yugabyte
(9 rows)
```

```sql
yb_demo=# SELECT source,
            total_sales * 100.0 / (SELECT SUM(total_sales) FROM channel) AS percent_sales
          FROM channel
          WHERE source='Facebook';
```

```output
  source  |  percent_sales
----------+------------------
 Facebook | 20.8927150492159
(1 row)
```
