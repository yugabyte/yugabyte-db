---
title: Metabase
linkTitle: Metabase
description: Metabase
menu:
  preview_integrations:
    identifier: metabase
    parent: tools
    weight: 70
aliases:
  - /integrations/metabase/
  - /preview/tools/metabase/
type: docs
---

[Metabase](https://www.metabase.com/) is a Business Intelligence (BI) tool.

This document shows how to set up Metabase to integrate with YugabyteDB's PostgreSQL-compatible API.

## 1. Start local cluster

Follow [Quick Start](../../quick-start/) instructions to run a local YugabyteDB cluster.

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
orders.sql  products.sql  reviews.sql  users.sql
```

### Connect to YugabyteDB using ysqlsh

Run the following command to connect to YugabyteDB using the YSQL shell:

```sh
$ ./bin/ysqlsh
```

```output
ysqlsh (11.2)
Type "help" for help.

yugabyte=#
```

### Create a database

```sql
yugabyte=# CREATE DATABASE yb-demo;
yugabyte=# GRANT ALL ON DATABASE yb-demo to yugabyte;
yugabyte=# \c yb-demo;
```

### Create schema and load data

First create the 4 tables necessary to store the data:

```sql
yugabyte=# \i 'schema.sql';
```

Now load the data into the tables:

```sql
yugabyte=# \i 'data/products.sql'
yugabyte=# \i 'data/users.sql'
yugabyte=# \i 'data/orders.sql'
yugabyte=# \i 'data/reviews.sql'
```

## 3. Download and configure Metabase

Detailed steps for setting up Metabase are available in the [Metabase documentation](https://www.metabase.com/docs/latest/setting-up-metabase.html). The following are the minimal steps for getting started:

```sh
$ wget http://downloads.metabase.com/v0.30.4/metabase.jar
```

```sh
$ java -jar metabase.jar
```

Go to <http://localhost:3000> to configure your Metabase server and point it to the YSQL API endpoint at `localhost:5433`.

## 4. Run complex queries with Metabase

Detailed steps on how to use Metabase are available in the [Metabase documentation](https://www.metabase.com/docs/latest/getting-started.html). For this doc, you will specifically focus on asking questions that require RDBMS capabilities.

- Filter data using WHERE clauses
- Join data between tables
- Perform data aggregation using GROUP BY
- Use built-in functions such as SUM, MIN, MAX, and so on

Click **Ask a Question > Custom Query**. Choose the database you just set up, and enter the SQL queries noted in the [Retail Analytics](../../sample-data/retail-analytics/) section.
