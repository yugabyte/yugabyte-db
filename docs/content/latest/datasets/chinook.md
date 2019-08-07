---
title: Chinook sample database
linkTitle: Chinook
description: Use the Chinook sample database, for a digital media store, to begin exploring YugaByte DB.
menu:
  latest:
    identifier: chinook
    parent: datasets
    weight: 2710
isTocNested: true
showAsideToc: true
---

The Chinook sample database is a sample database for a digital media store that you can use to explore and learn YugaByte DB.

## Before you begin

To install and use the Chinook sample database, you need to have installed and configured YugaByte DB. To get up and running quickly, see [Quick Start](/latest/quick-start/).

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

### 3. Create the Chinook database

To create the Chinook database, run the following command.

```sql
CREATE DATABASE chinook;
```

Confirm that you have the chinook database by listing out the databases on your cluster.

```
postgres=# \l
```

![Chinook list of databases](/images/datasets/chinook/chinook-list-of-dbs.png)

Connect to the `chinook` database.

```
postgres=# \c chinook
You are now connected to database "chinook" as user "postgres".
chinook=#
```

### Build the tables and objects

To build the tables and database objects, run the following `\i` command.

```
chinook=# \i /Users/yugabyte/chinook_ddl.sql
```

You can verify that all 14 tables have been created by running the `\d` command.

```
chinook=# \d
```

![Chinook list of relations](/images/datasets/chinook/chinook-list-of-relations.png)

### Load sample data

To load the `chinook` database with sample data, run the following command to execute commands in the file.

```
chinook=# \i /Users/yugabyte/chinook_data.sql
```

To verify that you have some data to work with, you can run a simple SELECT statement to pull data from the `Track` table.

```sql
chinook=# SELECT "Name", "Composer" FROM "Track" LIMIT 10;
```

```
              Name               |                          Composer
---------------------------------+------------------------------------------------------------
 Boa Noite                       |
 The Memory Remains              | Hetfield, Ulrich
 Plush                           | R. DeLeo/Weiland
 The Trooper                     | Steve Harris
 Surprise! You're Dead!          | Faith No More
 School                          | Kurt Cobain
 Sometimes I Feel Like Screaming | Ian Gillan, Roger Glover, Jon Lord, Steve Morse, Ian Paice
 Sad But True                    | Apocalyptica
 Tailgunner                      |
 Tempus Fugit                    | Miles Davis
(10 rows)
```

## Explore the Chinook sample database

That’s it! Using the command line or your favorite PostgreSQL development or administration tool, you are now ready to start exploring the chinook database and YugaByte DB features.

The Chinook data model represents a digital media store, including tables for artists, albums, media tracks, invoices and customers. Media related data was created using real data from an iTunes Library. Customer and employee information was created using fictitious names and addresses that can be located on Google maps, and other well formatted data (phone, fax, email, etc.). Sales information was auto generated using random data for a four year period. The basic characteristics of Chinook include:

- 11 tables
- A variety of indexes, primary and foreign key constraints
- Over 15,000 rows of data

Here’s an entity relationship diagram of the Chinook data model.

![Chinook E-R diagram](/images/datasets/chinook/chinook-er-diagram.png)

## What's next

- [Compare YugaByte DB to other databases](../comparisons) like [CockroachDB](https://www.yugabyte.com/yugabyte-db-vs-cockroachdb/), Google Cloud Spanner and MongoDB.
- Get started with YugaByte DB using the [Quick Start tutorial](../quick-start) on macOS, Linux, Docker, and Kubernetes.
- [Contact YugaByte](https://www.yugabyte.com/contact-sales/) to learn more about licensing, pricing, or to schedule a technical overview.
