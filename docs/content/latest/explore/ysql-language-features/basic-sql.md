---
title: Basic SQL Features
linkTitle: Basic SQL Features
description: Basic SQL Features in YSQL
headcontent: Basic SQL Features
image: /images/section_icons/secure/create-roles.png
menu:
  latest:
    identifier: explore-ysql-language-features-basic-sql
    parent: explore-ysql-language-features
    weight: 100
isTocNested: true
showAsideToc: true
---

This section covers the basics of the YSQL shell `ysqlsh`, databases and schemas as well as the data types supported in YSQL - including the `SERIAL` type (for implementing a primary key which is an auto incrementing id in a table) and composite types.

## `ysqlsh` SQL shell

The recommended command line shell to interact with a Yugabyte SQL database cluster is using the `ysqlsh` shell utility.

{{< tip title="Tip" >}}
All the `psql` commands just work as is in `ysqlsh`. This is because the `ysqlsh` shell is in turn derived from PostgreSQL shell `psql` code base - the default settings such as the database default port and the output format of some of the schema commands have been modified for YugabyteDB.
{{< /tip >}}

Connect to any node of the database cluster as shown below:

```sh
$ ./bin/ysqlsh -h 127.0.0.1
```

This should bring up the following prompt, which prints the version of `ysqlsh` being used.

```
ysqlsh (11.2-YB-2.5.0.0-b0)
Type "help" for help.

yugabyte=#
```

You can check the version of the database server by running the following.

```sql
yugabyte=# SELECT version();
```

The output shows that the YugabyteDB server version is 2.5.0.0-b0, and is a fork of PostgreSQL v11.2:
```
                                              version
----------------------------------------------------------------------------------------------------------
 PostgreSQL 11.2-YB-2.5.0.0-b0 on x86_64-<os, compiler version, etc>, 64-bit
(1 row)
```

### Query timing

By default the timing of query results will be displayed in milliseconds in `ysqlsh`. You can toggle this off (and on again) by using the `\timing` command.

```
yugabyte=# \timing
Timing is off.
```

### List all databases

List all databases using the following statements.
```sql
yugabyte=# \l
```
Output:
```
                                   List of databases
      Name       |  Owner   | Encoding | Collate |    Ctype    |   Access privileges
-----------------+----------+----------+---------+-------------+-----------------------
 postgres        | postgres | UTF8     | C       | en_US.UTF-8 |
 system_platform | postgres | UTF8     | C       | en_US.UTF-8 |
 template0       | postgres | UTF8     | C       | en_US.UTF-8 | =c/postgres          +
                 |          |          |         |             | postgres=CTc/postgres
 template1       | postgres | UTF8     | C       | en_US.UTF-8 | =c/postgres          +
                 |          |          |         |             | postgres=CTc/postgres
 yugabyte        | postgres | UTF8     | C       | en_US.UTF-8 |
(5 rows)
```

### List all schemas

You can list all schemas using `\dn` as shown here.
```sql
yugabyte=# \dn
```
Example:
```
# Create an example schema.
yugabyte=# create schema example;
CREATE SCHEMA

# List all schemas.
yugabyte=# \dn
  List of schemas
  Name   |  Owner
---------+----------
 example | yugabyte
 public  | postgres
```

### List tables

You can list all the tables in a database by using the `\d` command.
```sql
yugabyte=# \d
```
Output:
```
                 List of relations
 Schema |        Name         |   Type   |  Owner
--------+---------------------+----------+----------
 public | users               | table    | yugabyte
 public | users_id_seq        | sequence | yugabyte
 ```

### Describe a table

Describe a table using the `\d` command.
```sql
yugabyte=# \d users
```
Output:
```
                                Table "public.users"
  Column  |     Type      | Collation | Nullable |              Default
----------+---------------+-----------+----------+-----------------------------------
 id       | integer       |           | not null | nextval('users_id_seq'::regclass)
 username | character(25) |           | not null |
 enabled  | boolean       |           |          | true
Indexes:
    "users_pkey" PRIMARY KEY, lsm (id HASH)
```

## Users

YugabyteDB has two admin users already created - `yugabyte` (the recommended user) and `postgres` (mainly for backward compatibility with PostgreSQL). You can check this as shown below.

```sql
yugabyte=# \conninfo
```
This should print an output that looks as follows: `You are connected to database "yugabyte" as user "yugabyte" on host "127.0.0.1" at port "5433"`.

To check all the users provisioned, run:
```sql
yugabyte=# \du
```
```
                                   List of roles
 Role name |                         Attributes                         | Member of
-----------+------------------------------------------------------------+-----------
 postgres  | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
 yugabyte  | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
```

## Databases

YSQL supports databases and schemas, much like PostgreSQL.

To create a new database `testdb`, run the following statement:
```sql
CREATE DATABASE testdb;
```

To list all databases, use the `\l` command.

```
yugabyte=# \l
                                   List of databases
      Name       |  Owner   | Encoding | Collate |    Ctype    |   Access privileges
-----------------+----------+----------+---------+-------------+-----------------------
 postgres        | postgres | UTF8     | C       | en_US.UTF-8 |
 system_platform | postgres | UTF8     | C       | en_US.UTF-8 |
 template0       | postgres | UTF8     | C       | en_US.UTF-8 | =c/postgres          +
                 |          |          |         |             | postgres=CTc/postgres
 template1       | postgres | UTF8     | C       | en_US.UTF-8 | =c/postgres          +
                 |          |          |         |             | postgres=CTc/postgres
 testdb          | yugabyte | UTF8     | C       | en_US.UTF-8 |
 yugabyte        | postgres | UTF8     | C       | en_US.UTF-8 |
(6 rows)
```

To connect to this database, use the `\c` command as shown below.

```sql
\c testdb
```

You should see the following output.
```
You are now connected to database "testdb" as user "yugabyte".
```

To drop the database we just created, simply connect to another database and use the `DROP` command.

```
# First connect to another database.
testdb=# \c yugabyte
You are now connected to database "yugabyte" as user "yugabyte".

# Next use the DROP command.
yugabyte=# drop database testdb;
DROP DATABASE

# Verify the database is no longer present.
yugabyte=# \l
                                   List of databases
      Name       |  Owner   | Encoding | Collate |    Ctype    |   Access privileges
-----------------+----------+----------+---------+-------------+-----------------------
 postgres        | postgres | UTF8     | C       | en_US.UTF-8 |
 system_platform | postgres | UTF8     | C       | en_US.UTF-8 |
 template0       | postgres | UTF8     | C       | en_US.UTF-8 | =c/postgres          +
                 |          |          |         |             | postgres=CTc/postgres
 template1       | postgres | UTF8     | C       | en_US.UTF-8 | =c/postgres          +
                 |          |          |         |             | postgres=CTc/postgres
 yugabyte        | postgres | UTF8     | C       | en_US.UTF-8 |
```

## Tables

Let's create a simple table as shown below.

```sql
CREATE TABLE users (
  id serial,
  username CHAR(25) NOT NULL,
  enabled boolean DEFAULT TRUE,
  PRIMARY KEY (id)
  );
```

To list all tables, use the `\dt` command.
```sql
\dt
```
Output:
```
yugabyte=# \dt
                List of relations
 Schema |        Name         | Type  |  Owner
--------+---------------------+-------+----------
 public | users               | table | yugabyte
```

To list the table and the sequence we created, use the `\d` command.
```
 Schema |        Name         |   Type   |  Owner
--------+---------------------+----------+----------
 public | users               | table    | yugabyte
 public | users_id_seq        | sequence | yugabyte
```

To describe the table we just created, do the following.
```sql
\d users
```
Output:
```
yugabyte=# \d users
                                Table "public.users"
  Column  |     Type      | Collation | Nullable |              Default
----------+---------------+-----------+----------+-----------------------------------
 id       | integer       |           | not null | nextval('users_id_seq'::regclass)
 username | character(25) |           | not null |
 enabled  | boolean       |           |          | true
Indexes:
    "users_pkey" PRIMARY KEY, lsm (id HASH)
```


## Schemas

A schema is a named collection of tables, views, indexes, sequences, data types, operators, and functions. 

To create the schema with name `myschema`, run the following command.
```sql
testdb=# create schema myschema;
```

To create a table in this schema, run the following:

```sql
testdb=# create table myschema.company(
   ID   INT              NOT NULL,
   NAME VARCHAR (20)     NOT NULL,
   AGE  INT              NOT NULL,
   ADDRESS  CHAR (25),
   SALARY   DECIMAL (18, 2),
   PRIMARY KEY (ID)
);
```

Note that at this point, the `default` schema is still the selected schema, and running the `\d` command would not list the table we just created.

To see which schema is currently the default, run the following.

```sql
SHOW search_path;
```
You should see the following output.
```
testdb=# SHOW search_path;
   search_path
-----------------
 "$user", public
(1 row)
```

To set `myschema` as the default schema in this session, do the following.

```sql
SET search_path=myschema;
```
Now, we should be able to list the table we created.

```
testdb=# SHOW search_path;
 search_path
-------------
 myschema
(1 row)

testdb=# \d
           List of relations
  Schema  |  Name   | Type  |  Owner
----------+---------+-------+----------
 myschema | company | table | yugabyte
(1 row)
```

To drop the schema `myschema` and all the objects inside it, first change the current default schema.

```sql
SET search_path=default;
```

Next, run the `DROP` statement as shown below.

```sql
DROP SCHEMA myschema CASCADE;
```

You should see the following output.

```
testdb=# DROP SCHEMA myschema CASCADE;
NOTICE:  00000: drop cascades to table myschema.company
LOCATION:  reportDependentObjects, dependency.c:1004
DROP SCHEMA
```

## Data types

### Strings

The following character types are supported.

* `varchar(n)`: variable-length string
* `char(n)`: fixed-length, blank padded
* `text`, `varchar`: variable unlimited length

To test YugabyteDB’s support for character types, let’s create a table that has columns with these types specified:

```sql
CREATE TABLE char_types (
    id serial PRIMARY KEY,
    a CHAR (4),
    b VARCHAR (16),
    c TEXT
);
```

Insert some rows:

```sql
INSERT INTO char_types (a, b, c) VALUES (
  'foo', 'bar', 'Data for the text column'
);
```

### Numeric Types

The following numeric types are supported

* `SMALLINT`: a 2-byte signed integer that has a range from -32,768 to 32,767.
* `INT`: a 4-byte integer that has a range from -2,147,483,648 to 2,147,483,647.
* `float(n)`: is a floating-point number whose precision is at least, n, up to a maximum of 8 bytes
* `real`: is a 4-byte floating-point number
* `numeric` or `numeric(p,s)`: is a real number with p digits with s number after the decimal point. The numeric(p,s) is the exact number

Below is an example of creating a table with integer type columns and inserting rows into it:

```sql
CREATE TABLE albums (
    album_id SERIAL PRIMARY KEY,
    title VARCHAR (255) NOT NULL,
    play_time SMALLINT NOT NULL,
    library_record INT NOT NULL
);

INSERT INTO albums
values (default,'Funhouse', 3600,2146483645 ),
(default,'Darkside of the Moon', 4200, 214648348);
```

Similarly, the example below shows how to create a table with floating-point typed columns and how to insert a row into that table.

```sql
CREATE TABLE floating_point_test (
    floatn_test float8 not NULL,
    real_test real NOT NULL,
    numeric_test NUMERIC (3, 2)
);

INSERT INTO floating_point_test (floatn_test, real_test, numeric_test)
VALUES
    (9223372036854775807, 2147483647, 5.36), 
    (9223372036854775800, 2147483640, 9.99);
```

### The `SERIAL` pseudo-type

In YugabyteDB, just as with PostgreSQL, a sequence is a special kind of database object that generates a sequence of integers. A sequence is often used as the primary key column in a table.

By assigning the SERIAL pseudo-type to a column, the following occurs behind the scenes:

1. The database creates a sequence object and sets the next value generated by the sequence as the default value for the column.
2. The database adds a NOT NULL constraint to that column because a sequence always generates an integer, which is a non-null value.
3. The `SERIAL` column is assigned as the owner of the sequence. This results in the sequence object being deleted when the `SERIAL` column or table is dropped.

YSQL supports the following pseudo-types:

* `SMALLSERIAL`: 2 bytes (1 to 32,767)
* `SERIAL`:  4 bytes (1 to 2,147,483,647)
* `BIGSERIAL`: 8 bytes (1 to 9,223,372,036,854,775,807)


### Date and time

Temporal data types allow us to store date and/or time data. There are five main types in PostgreSQL, all of which are supported in YugabyteDB.

* `DATE`: stores the dates only
* `TIME`: stores the time of day values
* `TIMESTAMP`: stores both date and time values
* `TIMESTAMPTZ`: is a timezone-aware timestamp data type
* `INTERVAL`: stores intervals of time

Let's create a table with these temporal types as shown below.

```sql
CREATE TABLE temporal_types (
  date_type DATE,
  time_type TIME,
  timestamp_type TIMESTAMP,
  timestampz_type TIMESTAMPTZ,
  interval_type INTERVAL
);
```

Next, let's insert a row into this table.
```sql
INSERT INTO temporal_types (
  date_type, time_type, timestamp_type, timestampz_type, interval_type)
VALUES
  ('2000-06-28', '06:23:00', '2016-06-22 19:10:25-07', 
   '2016-06-22 19:10:25-07', '1 year'),
  ('2010-06-28', '12:32:12','2016-06-22 19:10:25-07', 
   '2016-06-22 19:10:25-07', '10 years 3 months 5 days');
```

You can check the data inserted as shown below.

```
yugabyte=# select * from temporal_types;
 date_type  | time_type |   timestamp_type    |    timestampz_type     |     interval_type
------------+-----------+---------------------+------------------------+------------------------
 2010-06-28 | 12:32:12  | 2016-06-22 19:10:25 | 2016-06-22 19:10:25-07 | 10 years 3 mons 5 days
 2000-06-28 | 06:23:00  | 2016-06-22 19:10:25 | 2016-06-22 19:10:25-07 | 1 year
(2 rows)
```

### Arrays

YSQL supports arrays to hold data of variable length. The type of the data stored in an array can be an inbuilt type, a user-defined type or an enumerated type. The examples below are adapted from [here](http://postgresguide.com/cool/arrays.html).

##### 1. Create a table with an array type

```sql
CREATE TABLE rock_band (
   name text,
   members text[]
);
```

##### 2. Insert rows

You can insert a row into this table as shown below. Note that the array literals must be double-quoted.

```sql
INSERT INTO rock_band VALUES (
  'Led Zeppelin', '{"Page", "Plant", "Jones", "Bonham"}'
);
```

An alternate syntax using the array constructor is shown below. When using the `ARRAY` constructor, the values must be single-quoted.

```sql
INSERT INTO rock_band VALUES (
  'Pink Floyd', ARRAY['Barrett', 'Gilmour']
);
```

##### 3. Accessing arrays

You can query the table as shown below.

```sql
select * from rock_band;
```
Output:
```
     name     |          members
--------------+---------------------------
 Pink Floyd   | {Barrett,Gilmour}
 Led Zeppelin | {Page,Plant,Jones,Bonham}
(2 rows)
```

Array values can be accessed using subscripts.

```sql
select name from rock_band where members[2] = 'Plant';
```
Output:
```
     name
--------------
 Led Zeppelin
(1 row)
```

They can also be accessed using slices as shown below.

```sql
select members[1:2] from rock_band;
```
Output:
```
      members
-------------------
 {Barrett,Gilmour}
 {Page,Plant}
(2 rows)
```

##### 4. Updating a single element

```sql
UPDATE rock_band set members[2] = 'Waters' where name = 'Pink Floyd';
```
Output:
```
yugabyte=# select * from rock_band where name = 'Pink Floyd';
    name    |     members
------------+------------------
 Pink Floyd | {Barrett,Waters}
(1 row)
```

##### 5. Updating entire array

```sql
UPDATE rock_band set members = '{"Mason", "Wright", "Gilmour"}' where name = 'Pink Floyd';
```
Output:
```
yugabyte=# select * from rock_band where name = 'Pink Floyd';
    name    |        members
------------+------------------------
 Pink Floyd | {Mason,Wright,Gilmour}
(1 row)
```

##### 6. Searching in arrays

Use the `ANY` keyword to search for a particular value in an array as shown below.
```sql
select name from rock_band where 'Mason' = ANY(members);
```
Output:
```
    name
------------
 Pink Floyd
(1 row)
```


### Enumerations with `ENUM` type

YugabyteDB supports the `ENUM` type in PostgreSQL. Below is an example (adapted from [here](http://postgresguide.com/sexy/enums.html)).

##### 1. Create `ENUM`
```sql
CREATE TYPE e_contact_method AS ENUM (
  'Email', 
  'Sms', 
  'Phone');
```

##### 2. Viewing the `ENUM`

To view the list of values across all `ENUM` types, run:
```sql
select t.typname, e.enumlabel 
  from pg_type t, pg_enum e 
  where t.oid = e.enumtypid;
```
The output should be as follows:
```
     typname      | enumlabel
------------------+-----------
 e_contact_method | Email
 e_contact_method | Sms
 e_contact_method | Phone
```

##### 2. Create table with `ENUM` column

```sql
CREATE TABLE contact_method_info (
   contact_name text,
   contact_method e_contact_method,
   value text
);
```

##### 3. Insert row with `ENUM`

Inserting a row with a valid value for the `ENUM` should succeed.

```sql
INSERT INTO contact_method_info VALUES ('Jeff', 'Email', 'jeff@mail.com')
```

You can verify as shown below:
```
yugabyte=# select * from contact_method_info;
 contact_name | contact_method |     value
--------------+----------------+---------------
 Jeff         | Email          | jeff@mail.com
(1 row)
```

Inserting an invalid `ENUM` value would fail.

```sql
yugabyte=# INSERT INTO contact_method_info VALUES ('Jeff', 'Fax', '4563456');
```
You should see the following error (note error message is compatible with that of PostgreSQL).
```
ERROR:  22P02: invalid input value for enum e_contact_method: "Fax"
LINE 1: INSERT INTO contact_method_info VALUES ('Jeff', 'Fax', '4563...
```

### Composite types

A composite type (also known as a *user defined type*) is a collection of data types similar to a `struct` in a programming language. The examples in this section are adapted from [here](https://www.tutorialspoint.com/postgresql/postgresql_data_types.htm).

##### 1. Create a composite type

```sql
CREATE TYPE inventory_item AS (
   name text,
   supplier_id integer,
   price numeric
);
```

##### 2. Create table with composite type

This can be done as shown below.
```sql
CREATE TABLE on_hand (
   item inventory_item,
   count integer
);
```

##### 3. Insert a row

To insert a row, use the `RUN` keyword, as shown below.
```sql
INSERT INTO on_hand VALUES (ROW('fuzzy dice', 42, 1.99), 1000);
```

##### 4. Selecting data

To select some subfields from our `on_hand` example table, run the query shown below.
```sql
SELECT (item).name FROM on_hand WHERE (item).price > 0.99;
```
You can also use the table names as shown below.
```sql
SELECT (on_hand.item).name FROM on_hand WHERE (on_hand.item).price > 0.99;
```
Output:
```
yugabyte=# SELECT (item).name FROM on_hand WHERE (item).price > 0.99;
    name
------------
 fuzzy dice
(1 row)
```



