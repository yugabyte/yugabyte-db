---
title: PgExercises sample database
headerTitle: PgExercises
linkTitle: PgExercises
description: Use the PgExercises sample database on YugabyteDB to learn SQL or test your knowledge.
menu:
  stable:
    identifier: pgexercises
    parent: sample-data
    weight: 300
type: docs
---

Install the PostgreSQL-compatible version of PgExercises on the YugabyteDB distributed SQL database. Work through 81 exercises to learn SQL or test your knowledge.

You can install and use the PgExercises database using:

- A local installation of YugabyteDB. To install YugabyteDB, refer to [Quick Start](/preview/quick-start/).
- Using cloud shell or a client shell to connect to a cluster in YugabyteDB Aeon. Refer to [Connect to clusters in YugabyteDB Aeon](/preview/yugabyte-cloud/cloud-connect/). To get started with YugabyteDB Aeon, refer to [Quick Start](/preview/yugabyte-cloud/cloud-quickstart/).

In either case, you use the YugabyteDB SQL shell ([ysqlsh](../../admin/ysqlsh/)) CLI to interact with YugabyteDB using [YSQL](../../api/ysql/).

## About the PgExercises sample database

The PgExercises sample database is based on the sample dataset used for the [PostgreSQL Exercises](https://pgexercises.com/) tutorial website. The dataset is for a new country club, with a set of members, facilities, and booking history.

The PostgreSQL Exercises website includes 81 exercises designed to be used as a companion to the official PostgreSQL documentation. The exercises on the PgExercises site range from simple SELECT statements and WHERE clauses, through JOINs and CASE statements, then on to aggregations, window functions, and recursive queries.

For further details about the data, see the [PostgresSQL Exercises' Getting Started page](https://pgexercises.com/gettingstarted.html).

The `exercises` database consists of three tables (for members, bookings, and facilities) and the table relationships as shown in the entity relationship diagram.

![PgExercises ER diagram](/images/sample-data/pgexercises/pgexercises-er-diagram.png)

## Install the PgExercises sample database

The PGExercise SQL scripts reside in the `share` folder of your YugabyteDB or client shell installation. They can also be found in the [`sample` directory of the YugabyteDB GitHub repository](https://github.com/yugabyte/yugabyte-db/tree/master/sample). The following files will be used for this exercise:

- [clubdata_ddl.sql](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/clubdata_ddl.sql) — Creates the tables and other database objects
- [clubdata_data.sql](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/clubdata_data.sql) — Loads the sample data

Follow the steps here to install the PgExercises sample database.

### Open the YSQL shell

If you are using a local installation of YugabyteDB, run the `ysqlsh` command from the `yugabyte` root directory.

```sh
$ ./bin/ysqlsh
```

If you are connecting to YugabyteDB Aeon, open the [ysqlsh cloud shell](../../yugabyte-cloud/cloud-connect/connect-cloud-shell/), or [run the YSQL connection string](../../yugabyte-cloud/cloud-connect/connect-client-shell/#ysqlsh) for your cluster from the `yugabyte-client` bin directory.

### Create the PgExercises database

To create the `exercises` database, run the following SQL `CREATE DATABASE` command.

```plpgsql
CREATE DATABASE exercises;
```

Confirm that you have the `exercises` database by listing the databases on your cluster using the `\l` command.

```plpgsql
yugabyte=# \l
```

Connect to the `exercises` database.

```plpgsql
yugabyte=# \c exercises
```

```output
You are now connected to database "exercises" as user "yugabyte".
exercises=#
```

### Build the PgExercises tables and objects

To build the tables and database objects, run the `\i` command.

```plpgsql
exercises=# \i share/clubdata_ddl.sql
```

You can verify that all three tables have been created by running the `\d` command.

```plpgsql
exercises=# \d
```

### Load the sample data

To load the `exercises` database with sample data, run the following command to execute commands in the file.

```plpgsql
exercises=# \i share/clubdata_data.sql
```

You can verify that you have data to work with by running the following `SELECT` statement to pull data from the `bookings` table.

```plpgsql
exercises=# SELECT * FROM bookings LIMIT 5;
```

## Explore the PgExercises database

You are now ready to start working through the [PostgreSQL Exercises](https://pgexercises.com/) exercises using YugabyteDB as the backend. The 81 exercises at the PostgreSQL Exercises website are broken into the following major sections.

- [Simple SQL Queries](https://pgexercises.com/questions/basic/)
- [JOINs and Subqueries](https://pgexercises.com/questions/joins/)
- [Modifying Data](https://pgexercises.com/questions/updates/)
- [Aggregation](https://pgexercises.com/questions/aggregates/)
- [Working with Timestamps](https://pgexercises.com/questions/date/)
- [String Operations](https://pgexercises.com/questions/string/)
- [Recursive Queries](https://pgexercises.com/questions/recursive/)

YugabyteDB returns the same results as expected based on the solutions on the PostgreSQL Exercises website, with one exception. In the ["Find telephone numbers with parentheses"](https://pgexercises.com/questions/string/reg.html) exercise, YugabyteDB returns results with a sort order of strings different than in PostgreSQL due to [hash sharding in YugabyteDB](../../architecture/docdb-sharding/).
