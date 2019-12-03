In this post we are going to walk you through how to download and install the PostgreSQL compatible version of the Chinook sample db onto the YugabyteDB distributed SQL database with a replication factor of 3.

The Chinook data model represents a digital media store, including tables for artists, albums, media tracks, invoices and customers. Media related data was created using real data from an iTunes Library. Customer and employee information was created using fictitious names and addresses that can be located on Google maps, and other well formatted data (phone, fax, email, etc.). Sales information was auto generated using random data for a four year period. The basic characteristics of Chinook include:

* 11 tables
* A variety of indexes, primary and foreign key constraints
* Over 15,000 rows of data

Here’s an ER diagram of the Chinook data model:

![](https://3lr6t13cowm230cj0q42yphj-wpengine.netdna-ssl.com/wp-content/uploads/2019/07/distributed-sql-scale-out-postgresql-chinook-01.png)

## Download and Install YugabyteDB

The latest instructions on how to get up and running are on our Quickstart page here:

https://docs.yugabyte.com/latest/quick-start/

## Download and Install the Chinook Database

### Download the Chinook Scripts

You can download the Chinook database that is compatible with YugabyteDB from our GitHub repo. The three files are:

* [chinook_ddl.sql](https://github.com/Yugabyte/yugabyte-db/blob/master/sample/chinook_ddl.sql) which creates tables and constraints
* [chinook_genres_artists_albums.sql](https://github.com/Yugabyte/yugabyte-db/blob/master/sample/chinook_genres_artists_albums.sql) which loads artist and album information
* [chinook_songs.sql](https://github.com/Yugabyte/yugabyte-db/blob/master/sample/chinook_songs.sql) which loads individual song information

We’ve purposely broken up what would otherwise be a very large script. By breaking it up into building blocks, it’ll be easier to see what YugabyteDB is doing and spot any problems (if you encounter them) a lot easier.

### Create the Chinook Database

```
CREATE DATABASE chinook;
```

Let’s confirm we have the exercises database by listing out the databases on our cluster.
```
postgres=# \l
```
Switch to the chinook database.
```
postgres=# \c chinook

You are now connected to database "chinook" as user "postgres".
chinook=#
```

### Build the Chinook Tables and Objects
```
chinook=# \i /Users/yugabyte/chinook_ddl.sql
```

We can verify that all 11 of our tables have been created by executing:

```
chinook=# \d
```

### Load Sample Data into Chinook
Next, let’s load our database with sample data.
```
chinook=# \i /Users/yugabyte/chinook_genres_artists_albums.sql
```

and
```
chinook=# \i /Users/yugabyte/chinook_songs.sql
```

Do a simple SELECT to pull data from the “Track” table to verify we now have some data to play with.
```
chinook=# SELECT "Name", "Composer" FROM "Track" LIMIT 10;

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
## Explore the Chinook Sample DB
That’s it! You are ready to start exploring the Chinook sample database.