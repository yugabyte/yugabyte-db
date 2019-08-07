---
title: Chinook sample database
linkTitle: Chinook
description: Use the Chinook sample database to begin exploring YugaByte DB.
menu:
  latest:
    identifier: chinook
    parent: datasets
    weight: 2710
isTocNested: true
showAsideToc: true
---

## Introduction

In this post we are going to walk you through how to download and install the PostgreSQL compatible version of the Chinook sample db onto the YugaByte DB distributed SQL database with a replication factor of 3.

The Chinook data model represents a digital media store, including tables for artists, albums, media tracks, invoices and customers. Media related data was created using real data from an iTunes Library. Customer and employee information was created using fictitious names and addresses that can be located on Google maps, and other well formatted data (phone, fax, email, etc.). Sales information was auto generated using random data for a four year period. The basic characteristics of Chinook include:

- 11 tables
- A variety of indexes, primary and foreign key constraints
- Over 15,000 rows of data

Here’s an ER diagram of the Chinook data model:

[add ER diagram]

## Before you begin

To use the Chinook sample database, you must have installed and configured YugaByte DB. To get up and running quickly, see [Quick Start](/latest/quick-start/).

## Install the Chinook database

### 1. Download the SQL scripts

You can download the Chinook database that is compatible with YugaByte DB from the sample directory of the [YugaByte DB GitHub repository](https://github.com/YugaByte/yugabyte-db). Download the following three files.

- [`chinook_ddl.sql`](https://raw.githubusercontent.com/YugaByte/yugabyte-db/42799a519726c75f502f463795ac6cd3ebda40c2/sample/chinook_ddl.sql) — Creates the tables and constraints
- [`chinook_genres_artists_albums.sql`](https://raw.githubusercontent.com/YugaByte/yugabyte-db/42799a519726c75f502f463795ac6cd3ebda40c2/sample/chinook_genres_artists_albums.sql) — Loads artist and album information
- [`chinook_songs.sql`](https://raw.githubusercontent.com/YugaByte/yugabyte-db/42799a519726c75f502f463795ac6cd3ebda40c2/sample/chinook_songs.sql) — Loads individual song information

### 2. Open the YSQL shell

To open the YSQL shell, run the `ysqlsh` command.

```sh
ysqlsh (11.2)
Type "help" for help.
postgres=#
```

### 3. Create the database

To create the `chinook` database:

```sql
CREATE DATABASE chinook;
```

Confirm that you have the chinook database by listing out the databases on your cluster.

```
postgres=# \l
```

Switch to the chinook database.

```
postgres=# \c chinook
You are now connected to database "chinook" as user "postgres".
chinook=# 
```

### Build the tables and objects

To build the tables and database objects, run the following command.

```
chinook=# \i /Users/yugabyte/chinook_ddl.sql
```
You can verify that all 14 tables have been created by executing:

```
chinook=# \d
```

[add image - list of relations]

### Load sample data

To load the `chinook` database with sample data, run the following command to execute commands in the file.

```
chinook=# \i /Users/yugabyte/chinook_data.sql
```

To verify that you have some data to work with, you can run a simple SELECT statement to pull data from the `customers` table.

```
chinook=# SELECT * FROM customers LIMIT 2;
```

[Add image]

## Explore the chinook dataset

The `chinook` dataset consists of 14 tables and the table relationships are showcased in the entity relationship diagram below:

[add e-r diagram]

The dataset contains the following:

- Suppliers: Suppliers and vendors of chinook
- Customers: Customers who buy products from chinook
- Employees: Employee details of chinook traders
- Products: Product information
- Shippers: The details of the shippers who ship the products from the traders to the end-customers
- Orders and Order_Details: Sales Order transactions taking place between the customers & the company

That’s it! Using the command line or your favorite PostgreSQL development or administration tool, you are now ready to start exploring the chinook database and YugaByte DB features.

## What to do next

- Compare YugaByte DB in depth to databases like CockroachDB, Google Cloud Spanner and MongoDB.
- Get started with YugaByte DB on macOS, Linux, Docker, and Kubernetes.
- Contact us to learn more about licensing, pricing or to schedule a technical overview.