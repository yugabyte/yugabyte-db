---
title: Northwind sample database
linkTitle: Northwind
description: Use the classic Northwind sample database to explore YugaByte DB.
menu:
  latest:
    identifier: northwind
    parent: datasets
    weight: 2710
isTocNested: true
showAsideToc: true
---

## Introduction

Learn how to download and install the PostgreSQL-compatible version of Northwind on the YugaByte DB distributed SQL database.

The Northwind database is a sample database that was originally created by Microsoft and used as the basis for their tutorials in a variety of database products for decades. The Northwind database contains the sales data for a fictitious company called “Northwind Traders,” which imports and exports specialty foods from around the world. The Northwind database is an excellent tutorial schema for a small-business ERP, with customers, orders, inventory, purchasing, suppliers, shipping, employees, and single-entry accounting. The Northwind database has since been ported to a variety of non-Microsoft databases, including PostgreSQL.

## Before you begin

To use the Northwind sample database, you must have installed and configured YugaByte DB. To get up and running quickly, see [Quick Start](/latest/quick-start/).

## Download and install the Northwind database

### 1. Download the files

You can download the Northwind database that is compatible with YugaByte DB from our GitHub repo. Here’s the two files you’ll need:

[northwind_ddl.sql](https://raw.githubusercontent.com/YugaByte/yugabyte-db/master/sample/northwind_ddl.sql) which creates tables and other database objects
[northwind_data.sql](https://raw.githubusercontent.com/YugaByte/yugabyte-db/master/sample/northwind_data.sql) which loads the sample data into Northwind

### 2. Open the YSQL shell

To open the YSQL shell, run the `ysqlsh` command.

```sh
ysqlsh (11.2)
Type "help" for help.
postgres=#
```

### 3. Create the Northwind database

To create the `northwind` database, run the following CREATE DATABASE command.

```sql
CREATE DATABASE northwind;
```

Confirm that you have the Northwind database by listing out the databases on your cluster.

```
postgres=# \l
```

Connect to the Northwind database.

```
postgres=# \c northwind
You are now connected to database "northwind" as user "postgres".
northwind=# 
```

### 4. Build the tables and objects

To build the tables and database objects, execute the `northwind_ddl.sql` SQL script.

```
northwind=# \i /Users/yugabyte/northwind_ddl.sql
```

You can verify that all 14 tables have been created by running the `\d` command.

```
northwind=# \d
```

[add image - list of relations]

### Load sample data

To load the `northwind` database with sample data, run the following command to execute commands in the `northwind_data.sql` file.

```
northwind=# \i /Users/yugabyte/northwind_data.sql
```

To verify that you have some data to work with, you can run a simple SELECT statement to pull data from the `customers` table.

```
northwind=# SELECT * FROM customers LIMIT 2;
```

[Add image]

## Explore the Northwind dataset

The `northwind` dataset consists of 14 tables and the table relationships are showcased in the entity relationship diagram below:

[add e-r diagram]

The dataset contains the following:

- **Suppliers**: Suppliers and vendors of Northwind
- **Customers**: Customers who buy products from Northwind
- **Employees**: Employee details of Northwind traders
- **Products**: Product information
- **Shippers**: The details of the shippers who ship the products from the traders to the end-customers
- **Orders and Order_Details**: Sales Order transactions taking place between the customers & the company

That’s it! You are now ready to start exploring the Northwind database and YugaByte DB features using the command line or your favorite PostgreSQL development or administration tool.

## What's next

- Compare YugaByte DB in depth to databases like CockroachDB, Google Cloud Spanner and MongoDB.
- Get started with YugaByte DB on macOS, Linux, Docker, and Kubernetes.
- Contact us to learn more about licensing, pricing or to schedule a technical overview.