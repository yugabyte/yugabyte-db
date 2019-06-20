---
title: Metabase
linkTitle: Metabase
description: Metabase
menu:
  v1.1:
    identifier: metabase
    parent: ecosystem-integrations
    weight: 576
isTocNested: true
showAsideToc: true
---

[Metabase](https://www.metabase.com/) is an extremly easy-to-use Business Intelligence (BI) tool. It bills itself as `the easy, open source way for everyone in your company to ask questions and learn from data`. This page shows how Metabase can be setup to integrate with YugaByte DB's PostgreSQL compatible API.

## 1. Start Local Cluster with YSQL API Enabled

Follow [Quick Start](../../../quick-start/) instructions to run a local YugaByte DB cluster. Test YugaByte DB's PostgreSQL compatible YSQL API as [documented](../../../quick-start/test-postgresql/) so that you can confirm that you have a PostgresSQL compatible service running on `localhost:5433`. 

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
orders.sql	products.sql	reviews.sql	users.sql
```

### Connect to YugaByte DB using psql

You can do this as shown below.

```sh
$ ./bin/psql -p 5433 -U postgres
```

```
psql (10.3, server 10.4)
Type "help" for help.

postgres=#
```

### Create a Database

```sql
postgres=> CREATE DATABASE yb_demo;
```

```sql
postgres=> GRANT ALL ON DATABASE yb_demo to postgres;
```

```sql
postgres=> \c yb_demo;
```

### Create Schema and Load Data

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

## 3. Download and Configure Metabase

Detailed steps for setting up Metabase are available [here](https://www.metabase.com/docs/latest/setting-up-metabase.html). The following are the minimal setup steps for getting started.

```sh
$ wget http://downloads.metabase.com/v0.30.4/metabase.jar
```

```sh
$ java -jar metabase.jar
```

Go to http://localhost:3000 to configure your Metabase server and point it to the YugaByte DB PostgreSQL API endpoint at `localhost:5433`.

## 4. Run Complex Queries with Metabase

Detailed steps on how to use Metabase are available [here](https://www.metabase.com/docs/latest/getting-started.html). For this doc, we will specifically focus on asking questions that require RDBMS capabilities.

- Filter data using WHERE clauses
- Join data between tables
- Perform data aggregation using GROUP BY
- Use built-in functions such as SUM, MIN, MAX, etc.

You can click on Ask a Question -> Custom Query. Choose the database we just setup, and enter the SQL queries noted in the [Retail Analytics](../../realworld-apps/retail-analytics/) section.
