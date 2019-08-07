---
title: SportsDB sample database 
linkTitle: SportsDB
description: Use the classic Northwind sample database to begin exploring YugaByte DB.
menu:
  latest:
    identifier: sportsdb
    parent: datasets
    weight: 2710
isTocNested: true
showAsideToc: true
---

## Introduction

In this post we are going to walk you through how to download and install the PostgreSQL-compatible version of Northwind on the YugaByte DB distributed SQL database.

[SportsDB](http://www.sportsdb.org/sd) is a sample dataset compiled from multiple sources, encompassing a variety of sports, including football, baseball, ice hockey, and more. It also cross-references many different types of content media. It is capable of supporting queries for the most intense of sports data applications, yet is simple enough for use by those with minimal database experience. The database itself is comprised of over 100 tables and just as many sequences, unique constraints, foreign keys, and indexes. The dataset also includes almost 80k rows of data. It has been ported to MySQL, SQL Server and PostgreSQL. If you like details, check out the [detailed entity relationship (ER) diagram](http://www.sportsdb.org/modules/sd/assets/downloads/sportsdb-27.jpg).

## Before you begin

To use the SportsDB sample database, you must have installed and configured YugaByte DB. To get up and running quickly, see [Quick Start](/latest/quick-start/).

## Install the SportsDB sample database

The following sections guide you through downloading and installing the SportsDB sample database.

### 1. Download the SportsDB scripts

The SQL scripts you need to create the SportsDB sample database (YugaByte DB=compatible) are available in the [`sample` directory of the YugaByte DB GitHub repository](https://github.com/YugaByte/yugabyte-db/tree/master/sample). Download the following five files.

- [`sportsdb_tables.sql`](https://raw.githubusercontent.com/YugaByte/yugabyte-db/master/sample/sportsdb_tables.sql) — Creates the tables and sequences
- [`sportsdb_inserts.sql`](https://raw.githubusercontent.com/YugaByte/yugabyte-db/master/sample/sportsdb_inserts.sql) — Loads the sample data into the `sportsdb` database
- [`sportsdb_constraints.sql`](https://raw.githubusercontent.com/YugaByte/yugabyte-db/master/sample/sportsdb_constraints.sql) — Creates the unique constraints
- [`sportsdb_fks.sql`](https://raw.githubusercontent.com/YugaByte/yugabyte-db/master/sample/sportsdb_fks.sql) — Creates the foreign key constraints
- [`sportsdb_indexes.sql`](https://raw.githubusercontent.com/YugaByte/yugabyte-db/master/sample/sportsdb_indexes.sql) — Creates the indexes

### 2. Open the YugaByte SQL (YSQL) shell

To open the YSQL shell, run the `ysqlsh` command.

```sh
ysqlsh (11.2)
Type "help" for help.
postgres=#
```

### 3. Create the SportsDB database

To create the `northwind` database, run the following YSQL command

```sql
CREATE DATABASE sportsdb;
```

Confirm that you have the SportsDB database by listing out the databases on your cluster.

```
postgres=# \l
```

[Add screenshot.]

Connect to the SportsDB database.

```
postgres=# \c sportsdb
You are now connected to database "sportsdb" as user "postgres".
sportsdb=# 
```

### 4. Build the SportsDB tables and sequences

To build the tables and database objects, run the following command.

```
sportsdb=# \i /Users/yugabyte/sportsdb_tables.sql
```

You can verify that all 203 tables and sequences have been created.

```
sportsdb=# \d
```

[Add screenshot]

### 5. Load sample data into the SportsDB database

To load the `sportsdb` database with sample data (~80k rows), run the following command to execute commands in the file.

```
sportsdb=# \i /Users/yugabyte/sportsdb_data.sql
```

To verify that you have some data to work with, you can run the following simple SELECT statement to pull data from the  basketball_defensive_stats` table.

```
sportsdb=# SELECT * FROM basketball_defensive_stats WHERE steals_total = '5';
```

[Add image]

### 6. Create unique constraints and foreign key

To create the unique constraints and foreign keys, run the following commands.

```
sportsdb=# \i /Users/yugabyte/sportsdb_constraints.sql
```

and

```
sportsdb=# \i /Users/yugabyte/sportsdb_fks.sql
```

### 7. Create the indexes

To create the indexes, run the following command.

```
sportsdb=# \i /Users/yugabyte/sportsdb_indexes.sql
```



## Explore the Northwind dataset

The `northwind` dataset consists of 14 tables and the table relationships are showcased in the entity relationship diagram below:

[add e-r diagram]

The dataset contains the following:

- Suppliers: Suppliers and vendors of Northwind
- Customers: Customers who buy products from Northwind
- Employees: Employee details of Northwind traders
- Products: Product information
- Shippers: The details of the shippers who ship the products from the traders to the end-customers
- Orders and Order_Details: Sales Order transactions taking place between the customers & the company

That’s it! Using the command line or your favorite PostgreSQL development or administration tool, you are now ready to start exploring the Northwind database and YugaByte DB features.

## What to do next

- Compare YugaByte DB in depth to databases like CockroachDB, Google Cloud Spanner and MongoDB.
- Get started with YugaByte DB on macOS, Linux, Docker, and Kubernetes.
- Contact us to learn more about licensing, pricing or to schedule a technical overview.