---
title: SportsDB sample database
headerTitle: SportsDB sample database
linkTitle: SportsDB
description: Use the SportsDB to query sports statistics while learning YugabyteDB.
menu:
  v2.20:
    identifier: sportsdb
    parent: sample-data
    weight: 400
type: docs
---

If you like sports statistics, you can install the PostgreSQL-compatible version of SportsDB on the YugabyteDB distributed SQL database and explore statistics for your favorite sport.

You can install and use the SportsDB sample database using:

- A local installation of YugabyteDB. To install YugabyteDB, refer to [Quick Start](/preview/quick-start/).
- Using cloud shell or a client shell to connect to a cluster in YugabyteDB Aeon. Refer to [Connect to clusters in YugabyteDB Aeon](/preview/yugabyte-cloud/cloud-connect/). To get started with YugabyteDB Aeon, refer to [Quick Start](/preview/yugabyte-cloud/cloud-quickstart/).

In either case, you use the YugabyteDB SQL shell ([ysqlsh](../../admin/ysqlsh/)) CLI to interact with YugabyteDB using [YSQL](../../api/ysql/).

## About the SportsDB sample database

[SportsDB](http://www.sportsdb.org/sd) is a sample sports statistics dataset compiled from multiple sources and encompassing a variety of sports, including football, baseball, basketball, ice hockey, and soccer. It also cross-references many different types of content media. It is capable of supporting queries for the most intense of sports data applications, yet is simple enough for use by those with minimal database experience. The database includes over 100 tables and just as many sequences, unique constraints, foreign keys, and indexes. The dataset also includes almost 80,000 rows of data. It has been ported to MySQL, SQL Server, and PostgreSQL.

If you like details, check out this detailed entity relationship (ER) diagram.

![SportsDB ER diagram](/images/sample-data/sportsdb/sportsdb-er-diagram.jpg)

## Install the SportsDB sample database

The SportsDB SQL scripts reside in the `share` folder of your YugabyteDB or client shell installation. They can also be found in the [`sample` directory of the YugabyteDB GitHub repository](https://github.com/yugabyte/yugabyte-db/tree/master/sample). The following files will be used for this exercise:

- [`sportsdb_tables.sql`](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/sportsdb_tables.sql) — Creates the tables and sequences
- [`sportsdb_inserts.sql`](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/sportsdb_inserts.sql) — Loads the sample data into the `sportsdb` database
- [`sportsdb_constraints.sql`](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/sportsdb_constraints.sql) — Creates the unique constraints
- [`sportsdb_fks.sql`](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/sportsdb_fks.sql) — Creates the foreign key constraints
- [`sportsdb_indexes.sql`](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/sportsdb_indexes.sql) — Creates the indexes

Follow the steps here to install the SportsDB sample database.

### Open the YSQL shell

If you are using a local installation of YugabyteDB, run the `ysqlsh` command from the `yugabyte` root directory.

```sh
$ ./bin/ysqlsh
```

If you are connecting to YugabyteDB Aeon, open the [ysqlsh cloud shell](/preview/yugabyte-cloud/cloud-connect/connect-cloud-shell/), or [run the YSQL connection string](/preview/yugabyte-cloud/cloud-connect/connect-client-shell/#ysqlsh) for your cluster from the `yugabyte-client` bin directory.

### Create the SportsDB database

To create the `sportsdb` database, run the following YSQL command.

```plpgsql
CREATE DATABASE sportsdb;
```

Confirm that you have the `sportsdb` database by listing out the databases on your cluster.

```plpgsql
yugabyte=# \l
```

Connect to the `sportsdb` database.

```plpgsql
yugabyte=# \c sportsdb
```

```output
You are now connected to database "sportsdb" as user "yugabyte".
sportsdb=#
```

### Build the SportsDB tables and sequences

To build the tables and database objects, run the following command.

```plpgsql
sportsdb=# \i share/sportsdb_tables.sql
```

You can verify that all 203 tables and sequences have been created by running the `\d` command.

```plpgsql
sportsdb=# \d
```

### Load sample data into the SportsDB database

To load the `sportsdb` database with sample data (~80k rows), run the following command to execute commands in the file.

```plpgsql
sportsdb=# \i share/sportsdb_inserts.sql
```

To verify that you have some data to work with, you can run the following simple SELECT statement to pull data from the  basketball_defensive_stats` table.

```plpgsql
sportsdb=# SELECT * FROM basketball_defensive_stats WHERE steals_total = '5';
```

### Create unique constraints and foreign key

To create the unique constraints and foreign keys, run the following commands.

```plpgsql
sportsdb=# \i share/sportsdb_constraints.sql
sportsdb=# \i share/sportsdb_fks.sql
```

### Create the indexes

To create the indexes, run the following command.

```plpgsql
sportsdb=# \i share/sportsdb_indexes.sql
```

## Explore the SportsDB database

That’s it! Using the command line or your favorite PostgreSQL development or administration tool, you are now ready to start exploring the SportsDB database and YugabyteDB features.
