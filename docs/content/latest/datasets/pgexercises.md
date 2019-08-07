---
title: PGExercises sample database 
linkTitle: PGExercises
description: Use the classic PGExercises sample database to begin exploring YugaByte DB.
menu:
  latest:
    identifier: pgexercises
    parent: datasets
    weight: 2710
isTocNested: true
showAsideToc: true
---

## Introduction



## Before you begin

To use the PGExercises sample database, you must have installed and configured YugaByte DB. To get up and running quickly, see [Quick Start](/latest/quick-start/).

## Download and install the PGExercises sample database

### Download the SQL scripts

You can download the PGExercise SQL scripts that is compatible with YugaByte DB from our GitHub repo. Here’s the two files you’ll need:

[`clubdata_ddl.sql`](https://raw.githubusercontent.com/YugaByte/yugabyte-db/master/sample/northwind_ddl.sql) which creates tables and other database objects
[`clubdata_data.sql`](https://raw.githubusercontent.com/YugaByte/yugabyte-db/master/sample/northwind_data.sql) which loads the sample data into the exercises

### Open the YSQL shell

To open the YSQL shell, run the `ysqlsh` command.

```sh
ysqlsh (11.2)
Type "help" for help.
postgres=#
```

### Create the 'exercises' database

To create the `exercises` database, run the following command.

```sql
CREATE DATABASE exercises;
```

Confirm that you have the `exercises` database by listing out the databases on your cluster.

```
postgres=# \l
```

Switch to the `exercises` database.

```
postgres=# \c exercises
You are now connected to database "exercises" as user "postgres".
northwind=# 
```

### Build the PGExercises tables and objects

To build the tables and database objects, run the following command.

```
exercises=# \i /Users/yugabyte/clubdata_ddl.sql
```
You can verify that all 3 tables have been created by executing:

```
exercises=# \d
```

[add image - list of relations]

### Load sample data

To load the `exercises` database with sample data, run the following command to execute commands in the file.

```
exercises=# \i /Users/yugabyte/clubdata_data.sql
```

To verify that you have some data to work with, you can run a simple SELECT statement to pull data from the `bookings` table.

```
exercises=# SELECT * FROM bookings LIMIT 5;
```

[Add image]

## Explore the PGExercises dataset

The `exercises` database consists of 14 tables and the table relationships are showcased in the entity relationship diagram below:

[add e-r diagram]

The dataset contains the following:


That’s it! Using the command line or your favorite PostgreSQL development or administration tool, you are now ready to start exploring the PGExercises database and YugaByte DB features.

## What to do next

- Compare YugaByte DB in depth to databases like CockroachDB, Google Cloud Spanner and MongoDB.
- Get started with YugaByte DB on macOS, Linux, Docker, and Kubernetes.
- Contact us to learn more about licensing, pricing or to schedule a technical overview.