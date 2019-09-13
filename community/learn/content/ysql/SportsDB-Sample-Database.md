<a href="http://www.sportsdb.org/sd">SportsDB</a> is a sample dataset compiled from multiple sources, encompassing a variety of sports including football, baseball, ice hockey and more. It also cross-references many different types of content media. It is capable of supporting queries for the most intense of sports data applications, yet is simple enough for use by those with minimal database experience. The database itself is comprised of over 100 tables and just as many sequences, unique constraints, foreign keys and indexes. The dataset also includes almost 80k rows of data. It has been ported to MySQL, SQL Server and PostgreSQL. You can check out a detailed ER diagram <a href="http://www.sportsdb.org/modules/sd/assets/downloads/sportsdb-27.jpg">here.</a>

![SportsDB ER Diagram](https://blog.yugabyte.com/wp-content/uploads/2019/07/sportsdb-distrbutedsql-postgresql-01.png)

In this post we are going to walk you through how to download and install the PostgreSQL compatible version of SportsDB onto the YugaByte DB distributed SQL database with a replication factor of 3.

## Download and Install YugaByte DB
The latest instructions on how to get up and running are on our Quickstart page here:

https://docs.yugabyte.com/latest/quick-start/

<strong>Note:</strong> Due to the hardware limitations of my laptop and size of the scripts, I temporarily bumped up the available memory accessible to YugaByte by adding the above <code>tserver_flags</code> argument. Depending on your setup, you might need to bump it up as well if you see errors like the one below while executing the database scripts. For example:

```
Service unavailable (yb/tserver/tablet_service.cc:239): Soft memory limit exceeded (at 96.13% of capacity)
```

If you see the above error, try:

```
$ ./bin/yb-ctl --rf 3 create --tserver_flags "memory_limit_hard_bytes=6442450944”
```

## Download and Install the SportsDB Database

### Download the SportsDB Scripts

You can download the SportsDB database that is compatible with YugaByte DB from our GitHub repo. The five files you’ll need are:


* _[sportsdb_tables.sql](https://github.com/YugaByte/yugabyte-db/blob/master/sample/sportsdb_tables.sql)_ which creates tables and sequences
* _[sportsdb_inserts.sql](https://github.com/YugaByte/yugabyte-db/blob/master/sample/sportsdb_inserts.sql)_ which loads the sample data into the sportsdb database
* _[sportsdb_constraints.sql](https://github.com/YugaByte/yugabyte-db/blob/master/sample/sportsdb_constraints.sql)_ which creates unique constraints
* _[sportsdb_fks.sql](https://github.com/YugaByte/yugabyte-db/blob/master/sample/sportsdb_fks.sql)_ which creates foreign key constraints
* _[sportsdb_indexes.sql](https://github.com/YugaByte/yugabyte-db/blob/master/sample/sportsdb_indexes.sql)_ which creates indexes

We’ve purposely broken up what would otherwise be a very large script. By breaking it up into building blocks, it’ll be easier to see what YugaByteDB is doing and spot any problems (if you encounter them) a lot easier.

### Create the SportsDB Database

```
CREATE DATABASE sportsdb;
```


Let’s confirm we have the sportsdb database by listing out the databases on our cluster.

```
postgres=# \l
```

Switch to the <em>sportsdb</em> database.

```
postgres=# \c sportsdb
You are now connected to database "sportsdb" as user "postgres".
sportsdb=#
```
### Build the SportsDB Tables and Sequences

```
sportsdb=# \i /Users/yugabyte/sportsdb_tables.sql
```

We can verify that all 203 tables and sequences have been created by executing:

```
sportsdb=# \d
```

### Load Sample Data into SportsDB
Next, let’s load our database with sample data, ~80k rows.

```
sportsdb=# \i /Users/yugabyte/sportsdb_inserts.sql
```

Let’s do a simple SELECT to pull data from the <em>basketball_defensive_stats</em> table to verify we now have some data to play with.

```
sportsdb=# SELECT * FROM basketball_defensive_stats WHERE steals_total = '5';
```

### Create Unique Constraints and Foreign Keys

Next, let’s create our unique constraints and foreign keys by executing:

```
sportsdb=# \i /Users/yugabyte/sportsdb_constraints.sql
```

and

```
sportsdb=# \i /Users/yugabyte/sportsdb_fks.sql
```

### Create Indexes

Finally, let’s create our indexes by executing:

```
sportsdb=# \i /Users/yugabyte/sportsdb_indexes.sql
```

<strong>Note:</strong> If you have worked with the SportDB sample database in the past, you know that the index creation section specifies the use of a BTREE index. YugaByte DB makes use of LSM trees, so we’ve modified the script as such. You can read more about LSM vs BTREE in our post, <a href="https://blog.yugabyte.com/a-busy-developers-guide-to-database-storage-engines-the-basics/">“A Busy Developer’s Guide to Database Storage Engines  -  The Basics.”</a> Even if we had not specified LSM, you would have seen an informational message that advised you that YugaByte DB had made the switch behind the scenes.

## Explore SportsDB
That’s it! You are ready to start exploring SportsDB running on YugaByte DB using your favorite PostgreSQL admin or development tool. You can learn more about the SportsDB project, libraries, samples, web services and more by visiting the project page <a href="http://www.sportsdb.org/sd">here.</a>
