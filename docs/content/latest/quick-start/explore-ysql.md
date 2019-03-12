---
title: 3. Explore YSQL 
linkTitle: 3. Explore YSQL 
description: Explore YugaByte SQL (YSQL)
aliases:
  - /quick-start/test-postgresql/
  - /latest/quick-start/test-postgresql/
  - /latest/quick-start/test-ysql/
  - /quick-start/test-cassandra/
  - /latest/quick-start/test-cassandra/
  - /latest/quick-start/test-ycql/
  - /latest/explore/postgresql/joins/
  - /latest/explore/postgresql/aggregations/
  - /latest/explore/postgresql/expressions/
  - /latest/explore/postgresql/views/
menu:
  latest:
    parent: quick-start
    weight: 130
type: page
isTocNested: false
showAsideToc: true
---

After [creating a local cluster](../create-local-cluster/), follow the instructions here to explore YugaByte DB's PostgreSQL-compatible [YSQL](../../api/ysql/) API.

[**psql**](https://www.postgresql.org/docs/9.3/static/app-psql.html) is a command line shell for interacting with PostgreSQL. For ease of use, YugaByte DB ships with a version of psql in its bin directory.


## 1. Load Data


- Download the sample schema.

```sh
$ wget https://raw.githubusercontent.com/YugaByte/yb-sql-workshop/master/query-using-bi-tools/schema.sql
```

-  Download the sample data

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

-  Connect using psql

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="#macos" class="nav-link active" id="macos-tab" data-toggle="tab" role="tab" aria-controls="macos" aria-selected="true">
      <i class="fab fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a href="#linux" class="nav-link" id="linux-tab" data-toggle="tab" role="tab" aria-controls="linux" aria-selected="false">
      <i class="fab fa-linux" aria-hidden="true"></i>
      Linux
    </a>
  </li>
  <li>
    <a href="#docker" class="nav-link" id="docker-tab" data-toggle="tab" role="tab" aria-controls="docker" aria-selected="false">
      <i class="fab fa-docker" aria-hidden="true"></i>
      Docker
    </a>
  </li>
  <li >
    <a href="#kubernetes" class="nav-link" id="kubernetes-tab" data-toggle="tab" role="tab" aria-controls="kubernetes" aria-selected="false">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="macos" class="tab-pane fade show active" role="tabpanel" aria-labelledby="macos-tab">
    {{% includeMarkdown "binary/explore-ysql.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade" role="tabpanel" aria-labelledby="linux-tab">
    {{% includeMarkdown "binary/explore-ysql.md" /%}}
  </div>
  <div id="docker" class="tab-pane fade" role="tabpanel" aria-labelledby="docker-tab">
    {{% includeMarkdown "docker/explore-ysql.md" /%}}
  </div>
  <div id="kubernetes" class="tab-pane fade" role="tabpanel" aria-labelledby="kubernetes-tab">
    {{% includeMarkdown "kubernetes/explore-ysql.md" /%}}
  </div>
</div>

-  Create a database.

```sql
postgres=> CREATE DATABASE yb_demo;
```

```sql
postgres=> GRANT ALL ON DATABASE yb_demo to postgres;
```

```sql
postgres=> \c yb_demo;
```
-  Insert sample data

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

## 2. Run Queries

-  How are users signing up for my site?

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

-  What is the most effective channel for user signups?

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

-  What are the most effective channels for product sales by revenue?

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

-  What is the min, max and average price of products in the store?

```sql
yb_demo=> SELECT MIN(price), MAX(price), AVG(price) FROM products;
```

```
min               |       max        |       avg
------------------+------------------+------------------
 15.6919436739704 | 98.8193368436819 | 55.7463996679207
(1 row)
```

-  What percentage of the total sales is from the Facebook channel?

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

## 3. Test JOINs

## 4. Test Distributed Transactions

## 5. Test Secondary Indexes

## 6. Test JSONB Column Type


