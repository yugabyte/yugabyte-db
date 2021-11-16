---
title: Retail analytics sample database
headerTitle: Retail analytics sample database
linkTitle: Retail Analytics
description: Explore this retail analytics sample database on YugabyteDB using YSQL.
menu:
  stable:
    identifier: retail-analytics
    parent: sample-data
    weight: 500
isTocNested: true
showAsideToc: true
---

Install the PostgreSQL-compatible Retail Analytics dataset on the YugabyteDB distributed SQL database.

You can install and use the Retail Analytics sample database using:

- A local installation of YugabyteDB. To install YugabyteDB, refer to [Quick Start](../../quick-start/).
- A local installation of the YugabyteDB client shells that you use to connect to a cluster in Yugabyte Cloud. To connect to your Yugabyte Cloud cluster, refer to [Connect via Client Shell](../../yugabyte-cloud/cloud-basics/connect-to-clusters/#connect-via-client-shell). To get started with Yugabyte Cloud, refer to [Get Started](../../yugabyte-cloud/cloud-basics/).

In either case, you use the YugabyteDB SQL shell ([ysqlsh](../../admin/ysqlsh/)) CLI to interact with YugabyteDB using [YSQL](../../api/ysql/).

## About the Retail Analytics database

The Retail Analytics dataset includes sample data in the following tables:

- **Products**: Product information
- **Users**: Customers who have bought products
- **Orders**: Orders made by customers
- **Reviews**: Product reviews

## Install the Retail Analytics sample database

The Retail Analytics SQL scripts reside in the `share` folder of your YugabyteDB or client shell installation. They can also be found in the [`sample` directory of the YugabyteDB GitHub repository](https://github.com/yugabyte/yugabyte-db/tree/master/sample). The following files will be used for this exercise:

- [`schema.sql`](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/schema.sql) — Creates the tables and constraints
- [`orders.sql`](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/orders.sql) — Loads product orders
- [`products.sql`](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/products.sql) — Loads products
- [`reviews.sql`](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/reviews.sql) — Loads product reviews
- [`users.sql`](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/users.sql) — Loads customer information

Follow the steps here to install the Retail Analytics sample database.

### Open the YSQL shell

If you are using a local installation of YugabyteDB, run the `ysqlsh` command from the `yugabyte` root directory.

```sh
$ ./bin/ysqlsh
```

If you are connecting to Yugabyte Cloud, run the connection string for your cluster from the the `yugabyte-client` root directory. Refer to [Connect via Client Shell](../../yugabyte-cloud/cloud-basics/connect-to-clusters/#connect-via-client-shell).

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
yb_demo=# \i share/schema.sql;
```

Now load the data into the tables.

```plpgsql
\i share/products.sql;
\i share/users.sql;
\i share/orders.sql;
\i share/reviews.sql;
```

## Explore the Retail Analytics database

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
