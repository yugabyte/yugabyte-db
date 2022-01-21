---
title: Explore Yugabyte SQL
linkTitle: Explore distributed SQL
description: Use distributed SQL to explore core features of YugabteDB.
headcontent:
image: /images/section_icons/index/quick_start.png
menu:
  latest:
    identifier: qs-explore-1-ysql
    parent: cloud-quickstart
    weight: 400
type: page
isTocNested: true
showAsideToc: true
---

After [creating a free cluster](../qs-add/), [connecting to the cluster](../qs-connect/) using the cloud shell, and [creating a database (yb_demo) and loading some data](../qs-data/), you can start exploring YugabyteDB's PostgreSQL-compatible, fully-relational Yugabyte SQL API.

This exercise assumes you are already connected to your cluster using the `ysqlsh` shell, have created the `yb_demo` database, and loaded the sample data.

## Explore YugabyteDB

To display the schema of the `products` table, enter the following command:

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
   15
(1 row)
```

The following query selects the `id`, `title`, `category`, and `price` columns for the first five products.

```sql
yb_demo=# SELECT id, title, category, price, rating
          FROM products
          LIMIT 5;
```

```output
 id |           title           | category  |      price       | rating 
----+---------------------------+-----------+------------------+--------
  3 | Synergistic Granite Chair | Doohickey | 35.3887448815391 |      4
 14 | Awesome Concrete Shoes    | Widget    | 25.0987635927189 |      4
  9 | Practical Bronze Computer | Widget    | 58.3131209852614 |    4.2
 12 | Sleek Paper Toucan        | Gizmo     | 77.3428505441222 |    4.4
  5 | Enormous Marble Wallet    | Gadget    | 82.7450976850356 |      4
(5 rows)
```

### The JOIN clause

Use a JOIN clause to combine rows from two or more tables, based on a related column between them.

The following JOIN query selects the `total` column from the `orders` table, and for each of these orders, fetches the `id`, `name`, and `email` from the `users` table of the corresponding users that placed those orders. The related column between the two tables is the user's id.

```sql
yb_demo=# SELECT users.id, users.name, users.email, orders.id, orders.total
          FROM orders INNER JOIN users ON orders.user_id=users.id
          LIMIT 5;
```

```output
 id |     name           |         email                | id |      total       
----+--------------------+------------------------------+----+------------------
  4 | Arnold Adams       | adams.arnold@gmail.com       | 22 | 49.0560710142838
 15 | Bertrand Romaguera | romaguera.bertrand@gmail.com | 76 | 28.0989026289413
  1 | Hudson Borer       | borer-hudson@yahoo.com       |  9 | 81.6742695904106
 10 | Tressa White       | white.tressa@yahoo.com       | 54 | 122.116378514938
  4 | Arnold Adams       | adams.arnold@gmail.com       | 23 | 56.5115886738793
(5 rows)
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

To place the order, you can run the following transaction:

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
yb_demo=# SELECT * FROM orders WHERE id = (SELECT max(id) FROM orders);
```

```output
 id |         created_at         | user_id | product_id | discount | quantity |     subtotal     | tax |      total       
----+----------------------------+---------+------------+----------+----------+------------------+-----+------------------
 77 | 2021-09-08 20:03:12.308302 |       1 |          2 |        0 |       10 | 700.798961307176 |   0 | 700.798961307176
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

### Create a view

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
 Facebook | 31.3725490196078
(1 row)
```

## Next steps

- [Develop an application](../../cloud-develop/)
- [Deploy production clusters](../../cloud-basics/create-clusters/)
- [Authorize access to your cluster](../../cloud-secure-clusters/add-connections/)
- [Connect to clusters](../../cloud-connect/)
- [Add database users](../../cloud-secure-clusters/add-users/)
