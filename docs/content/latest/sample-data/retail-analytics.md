---
title: Retail analytics sample database
headerTitle: Retail analytics sample database
linkTitle: Retail Analytics
description: Explore this retail analytics sample database on YugabyteDB using YSQL.
aliases:
  - /develop/realworld-apps/retail-analytics/
menu:
  latest:
    identifier: retail-analytics
    parent: sample-data
    weight: 500
isTocNested: true
showAsideToc: true
---

## About the Retail Analytics database

The Retail Analytics dataset includes sample data in the following tables:

- **Products**: Product information
- **Users**: Customers who have bought products
- **Orders**: Orders made by customers
- **Reviews**: Product reviews

## 1. Start local cluster

Follow [Quick Start](../quick-start/) instructions to run a local YugabyteDB cluster. Test the YSQL API as [documented](../../../quick-start/explore/ysql/) so that you can confirm that you have the YSQL service running on `localhost:5433`. 

## 2. Load data

### Download the sample schema

```sh
$ wget https://raw.githubusercontent.com/yugabyte/yb-sql-workshop/master/query-using-bi-tools/schema.sql
```

### Download the sample data

```sh
$ wget https://github.com/yugabyte/yb-sql-workshop/raw/master/query-using-bi-tools/sample-data.tgz
```

```sh
$ tar zxvf sample-data.tgz
```

```sh
$ ls data/
```

```output
orders.sql  products.sql  reviews.sql users.sql
```

### Connect to YugabyteDB using ysqlsh

```sh
$ ./bin/ysqlsh
```

```output
ysqlsh (11.2)
Type "help" for help.

yugabyte=#
```

### Create a database

You can do this as shown below.

```plpgsql
yugabyte=# CREATE DATABASE yb_demo;
```

```plpgsql
yugabyte=# GRANT ALL ON DATABASE yb_demo to yugabyte;
```

```plpgsql
yugabyte=# \c yb_demo;
```

### Load data

First create the four tables necessary to store the data.

```plpgsql
yugabyte=# \i 'schema.sql';
```

Now load the data into the tables.

```plpgsql
\i 'data/products.sql'
```

```plpgsql
\i 'data/users.sql'
```

```plpgsql
\i 'data/orders.sql'
```

```plpgsql
yugabyte=# \i 'data/reviews.sql'
```

## Explore the Retail Analytics sample database

### How are users signing up for my site?

```plpgsql
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

### What is the most effective channel for user signups?

```plpgsql
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

### What are the most effective channels for product sales by revenue?

```plpgsql
yb_demo=# SELECT source, ROUND(SUM(orders.total)) AS total_sales
          FROM users, orders WHERE users.id=orders.user_id
          GROUP BY source
          ORDER BY total_sales DESC;
```

```output
source     | total_sales
-----------+-------------
 Facebook  |      333454
 Google    |      325184
 Organic   |      319637
 Twitter   |      319449
 Affiliate |      297605
(5 rows)
```

### What is the min, max and average price of products in the store?

```plpgsql
yb_demo=# SELECT MIN(price), MAX(price), AVG(price) FROM products;
```

```output
min               |       max        |       avg
------------------+------------------+------------------
 15.6919436739704 | 98.8193368436819 | 55.7463996679207
(1 row)
```

### What percentage of the total sales is from the Facebook channel?

You can do this as shown below.

```plpgsql
yb_demo=# CREATE VIEW channel AS
            (SELECT source, ROUND(SUM(orders.total)) AS total_sales
             FROM users, orders
             WHERE users.id=orders.user_id
             GROUP BY source
             ORDER BY total_sales DESC);
```

Now that the view is created, you can see it in our list of relations.

```plpgsql
yb_demo=# \d
```

```output
List of relations
 Schema |   Name   | Type  |  Owner
--------+----------+-------+----------
 public | channel  | view  | postgres
 public | orders   | table | postgres
 public | products | table | postgres
 public | reviews  | table | postgres
 public | users    | table | postgres
(5 rows)
```

```plpgsql
yb_demo=# SELECT source, total_sales * 100.0 / (SELECT SUM(total_sales) FROM channel) AS percent_sales
          FROM channel WHERE source='Facebook';
```

```output
source    |  percent_sales
----------+------------------
 Facebook | 20.9018954710909
(1 row)
```
