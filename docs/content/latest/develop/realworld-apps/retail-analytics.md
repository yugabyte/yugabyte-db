---
title: Retail Analytics
linkTitle: Retail Analytics
description: Retail Analytics
aliases:
  - /develop/realworld-apps/retail-analytics/
menu:
  latest:
    identifier: retail-analytics
    parent: realworld-apps
    weight: 584
isTocNested: true
showAsideToc: true
---

## 1. Start Local Cluster with YSQL API Enabled

Follow [Quick Start](../../../quick-start/) instructions to run a local YugaByte DB cluster. Test YugaByte DB's PostgreSQL compatible YSQL API as [documented](../../../quick-start/test-postgresql/) so that you can confirm that you have the YSQL service running on `localhost:5433`. 

## 2. Load Data

### Download the Sample Schema

```sh
$ wget https://raw.githubusercontent.com/YugaByte/yb-sql-workshop/master/query-using-bi-tools/schema.sql
```

### Download the Sample Data

```sh
$ wget https://github.com/YugaByte/yb-sql-workshop/raw/master/query-using-bi-tools/sample-data.tgz
```

```sh
$ tar zxvf sample-data.tgz
```

```sh
$ ls data/
```

```
orders.sql  products.sql  reviews.sql users.sql
```

### Connect to YugaByte DB using psql

```sh
$ ./bin/psql -p 5433 -U postgres
```

```
psql (10.3, server 10.4)
Type "help" for help.

postgres=#
```

### Create a Database

You can do this as shown below.

```sql
postgres=> CREATE DATABASE yb_demo;
```

```sql
postgres=> GRANT ALL ON DATABASE yb_demo to postgres;
```

```sql
postgres=> \c yb_demo;
```

### Load Data

First create the 4 tables necessary to store the data.

```sql
postgres=> \i 'schema.sql';
```

Now load the data into the tables.

```sql
postgres=> \i 'data/products.sql'
```

```sql
postgres=> \i 'data/users.sql'
```

```sql
postgres=> \i 'data/orders.sql'
```

```sql
postgres=> \i 'data/reviews.sql'
```

## 3. Run Queries

### How are users signing up for my site?

```sql
yb_demo=> SELECT DISTINCT(source) FROM users;
```

```
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

```sql
yb_demo=> SELECT source, count(*) AS num_user_signups
          FROM users
          GROUP BY source
          ORDER BY num_user_signups DESC;
```

```
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

```sql
yb_demo=> SELECT source, ROUND(SUM(orders.total)) AS total_sales
          FROM users, orders WHERE users.id=orders.user_id
          GROUP BY source
          ORDER BY total_sales DESC;
```

```
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

```sql
yb_demo=> SELECT MIN(price), MAX(price), AVG(price) FROM products;
```

```
min               |       max        |       avg
------------------+------------------+------------------
 15.6919436739704 | 98.8193368436819 | 55.7463996679207
(1 row)
```

### What percentage of the total sales is from the Facebook channel?

You can do this as shown below.

```sql
yb_demo=> CREATE VIEW channel AS
            (SELECT source, ROUND(SUM(orders.total)) AS total_sales
             FROM users, orders
             WHERE users.id=orders.user_id
             GROUP BY source
             ORDER BY total_sales DESC);
```

Now that the view is created, we can see it in our list of relations.

```sql
yb_demo=> \d
```

```
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

```sql
yb_demo=> SELECT source, total_sales * 100.0 / (SELECT SUM(total_sales) FROM channel) AS percent_sales
          FROM channel WHERE source='Facebook';
```

```
source    |  percent_sales
----------+------------------
 Facebook | 20.9018954710909
(1 row)
```