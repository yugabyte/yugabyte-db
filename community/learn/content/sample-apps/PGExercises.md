PgExercises is a sample dataset used to power the [PostgreSQL Exercises tutorial website](https://pgexercises.com/). The site is comprised of over 80 exercises designed to be used as a companion to the official PostgreSQL documentation. The exercises on the PGExercises site range from simple SELECT statements and WHERE clauses, through JOINs and CASE statements, then on to aggregations, window functions, and recursive queries.

The dataset consists of 3 tables (members, bookings, and facilities) and table relationships as shown in the ER diagram below:

![](https://3lr6t13cowm230cj0q42yphj-wpengine.netdna-ssl.com/wp-content/uploads/2019/07/distributed-sql-postgresql-01.png)

## Download and Install YugabyteDB

The latest instructions on how to get up and running are on our Quickstart page here:

https://docs.yugabyte.com/latest/quick-start/

## Download and Install the PGExercises Database

### Download the PGExercises Scripts
You can download the PGExercises database that is compatible with YugabyteDB from our GitHub repo. The two files are:

* [clubdata_ddl.sql](https://raw.githubusercontent.com/Yugabyte/yugabyte-db/master/sample/clubdata_ddl.sql) which creates tables and other database objects
* [clubdata_data.sql](https://raw.githubusercontent.com/Yugabyte/yugabyte-db/master/sample/clubdata_data.sql) which loads the sample data into the exercises database

### Create the Exercises Database
```
CREATE DATABASE exercises;
```

Let’s confirm we have the exercises database by listing out the databases on our cluster.

```
postgres=# \l
```

Switch to the exercises database.

```
postgres=# \c exercises
You are now connected to database "exercises" as user "postgres".
exercises=#
```

### Build the Exercises Tables and Objects

```
exercises=# \i /Users/yugabyte/clubdata_ddl.sql
```

We can verify that all 3 of our tables have been created by executing:

```
exercises=# \d
```

### Load Sample Data into Exercises
Next, let’s load our database with sample data.

```
exercises=# \i /Users/yugabyte/clubdata_data.sql
```

Let’s do a simple SELECT to pull data from the bookings table to verify we now have some data to play with.

```
exercises=# SELECT * FROM bookings LIMIT 5;
```

## Try the PGExercises Tutorial
That’s it! You are ready to start working through the PGExercises tutorial with YugabyteDB as the backend. PGExercises is made up of 81 exercises and broken into the following major sections:

* [Simple SQL Queries](https://pgexercises.com/questions/basic/)
* [JOINs and Subqueries](https://pgexercises.com/questions/joins/)
* [Modifying Data](https://pgexercises.com/questions/updates/)
* [Aggregation](https://pgexercises.com/questions/aggregates/)
* [Working with Timestamps](https://pgexercises.com/questions/date/)
* [String Operations](https://pgexercises.com/questions/string/)
* [Recursive Queries](https://pgexercises.com/questions/recursive/)

Note that all of the exercises on the site will work with YugabyteDB with the exception of the following:

* [This JOIN example](https://pgexercises.com/questions/joins/simplejoin2.html) has a bug and won’t return the correct row numbers. The GitHub issue can be tracked [here.](https://github.com/Yugabyte/yugabyte-db/issues/1827)
* In this [string operation exercise](https://pgexercises.com/questions/string/reg.html) you might notice that our sort order will be different. This is because we hash partition our data. As a result, row ordering is expected to be different.
* In the [calculated UPDATE example](https://pgexercises.com/questions/updates/updatecalculated.html), you will get an error because “FROM clause in UPDATE” is not yet supported. You can track the issue on GitHub [here.](https://github.com/Yugabyte/yugabyte-db/issues/738)
* The exercise which demonstrates a [DELETE based on a subquery](https://pgexercises.com/questions/updates/deletewh2.html) will return an error. You can track the resolution of this issue on GitHub [here.](https://github.com/Yugabyte/yugabyte-db/issues/1828)