---
title: Schemas and tables
linkTitle: Schemas and tables
description: Schemas and tables in YSQL
menu:
  preview:
    identifier: explore-ysql-language-features-databases-schemas-tables
    parent: explore-ysql-language-features
    weight: 100
type: docs
---

This section covers basic topics including how to connect to your cluster using the YSQL shell, and use the shell to manage databases, schemas, and tables.

{{% explore-setup-single %}}

## YSQL shell

Use the [ysqlsh shell](../../../admin/ysqlsh/) to interact with a Yugabyte database cluster using the [YSQL API](../../../api/ysql/). Because `ysqlsh` is derived from the PostgreSQL shell `psql` code base, all `psql` commands work as is in `ysqlsh`. Some default settings such as the database default port and the output format of some of the schema commands have been modified for YugabyteDB.

Using `ysqlsh`, you can:

- interactively enter SQL queries and see the query results
- input from a file or the command line
- use [meta-commands](../../../admin/ysqlsh-meta-commands/) for scripting and administration

`ysqlsh` is installed with YugabyteDB and is located in the `bin` directory of the YugabyteDB home directory.

### Connect to a node

From the YugabyteDB home directory, connect to any node of the database cluster as shown below:

```sh
$ ./bin/ysqlsh -h 127.0.0.1
```

This should bring up the following prompt, which prints the version of `ysqlsh` being used.

```output
ysqlsh (11.2-YB-2.5.1.0-b0)
Type "help" for help.

yugabyte=#
```

You can check the version of the database server by running the following query:

```sql
yugabyte=# SELECT version();
```

The output shows the YugabyteDB server version, and is a fork of PostgreSQL v11.2:

```output
                                              version
----------------------------------------------------------------------------------------------------------
 PostgreSQL 11.2-YB-2.5.1.0-b0 on x86_64-<os, compiler version, etc>, 64-bit
(1 row)
```

### Query timing

You can turn the display of how long each SQL statement takes (in milliseconds) on and off by using the `\timing` meta-command, as follows:

```sql
yugabyte=# \timing
```

```output
Timing is on.
```

## Users

By default, YugabyteDB has two admin users already created: `yugabyte` (the recommended user) and `postgres` (mainly for backward compatibility with PostgreSQL). You can check this as follows:

```sql
yugabyte=# \conninfo
```

This should output the following:

```output
You are connected to database "yugabyte" as user "yugabyte" on host "127.0.0.1" at port "5433".
```

To check all the users provisioned, run the following meta-command:

```sql
yugabyte=# \du
```

```output
                                     List of roles
  Role name   |                         Attributes                         | Member of
--------------+------------------------------------------------------------+-----------
 postgres     | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
 yb_db_admin  | No inheritance, Cannot login                               | {}
 yb_extension | Cannot login                                               | {}
 yb_fdw       | Cannot login                                               | {}
 yugabyte     | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
```

## Databases

A database is the highest level of data organization and serves as a container for all objects such as tables, views, indexes, functions, and schemas. A YugabyteDB cluster can manage multiple databases and each database is isolated from the others, ensuring data integrity and security.

### Default databases

When a YugabyteDB cluster is deployed, YugabyteDB creates a set of default databases as described in the following table.

| Database | Source | Description |
| :--- | :--- | :--- |
| postgres | PostgreSQL | PostgreSQL default database meant for use by users, utilities, and third party applications. |
| system_platform | YugabyteDB | Used by [YugabyteDB Anywhere](../../../yugabyte-platform/) to run periodic read and write tests to check the health of the node's YSQL endpoint. |
| template0 | PostgreSQL | [PostgreSQL template database](https://www.postgresql.org/docs/11/manage-ag-templatedbs.html), to be copied when using CREATE DATABASE commands. template0 should never be modified. |
| template1 | PostgreSQL | [PostgreSQL template database](https://www.postgresql.org/docs/11/manage-ag-templatedbs.html), copied when using CREATE DATABASE commands. You can add objects to template1; these are copied into databases created later. |
| yugabyte | YugabyteDB | The default database for YSQL API connections. See [Default user](../../../secure/enable-authentication/authentication-ysql/#default-user-and-password). |

For more information on the default PostgreSQL databases, refer to [Managing Databases](https://www.postgresql.org/docs/11/managing-databases.html) on the PostgreSQL documentation.

### Create a database

To create a new database `testdb`, run the following statement:

```sql
CREATE DATABASE testdb;
```

To list all databases, use the `\l` meta-command.

```sql
yugabyte=# \l
```

```output
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

To connect to the database you created, use the `\c` meta-command.

```sql
yugabyte=# \c testdb
```

You should see the following output:

```output
You are now connected to database "testdb" as user "yugabyte".
testdb=#
```

To drop the database we just created, connect to another database and then use the `DROP` command.

Connect to another database as follows:

```sql
testdb=# \c yugabyte
```

```output
You are now connected to database "yugabyte" as user "yugabyte".
```

Use the `DROP` command as follows:

```sql
yugabyte=# DROP DATABASE testdb;
```

```output
DROP DATABASE
```

Verify the database is no longer present as follows:

```sql
yugabyte=# \l
```

```output
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

## Tables

A table is the fundamental database object that stores the actual data in a structured format, consisting of rows and columns. Tables are created in a specific schema (by default public) and contain the data that applications and users interact with. Each table has a defined structure, with columns representing the different attributes or fields of the data, and rows representing individual records or entries.

Create a table using the CREATE TABLE statement.

```sql
CREATE TABLE users (
  id serial,
  username CHAR(25) NOT NULL,
  enabled boolean DEFAULT TRUE,
  PRIMARY KEY (id)
  );
```

```output
CREATE TABLE
```

To list all tables, use the `\dt` meta-command.

```sql
yugabyte=# \dt
```

```output
yugabyte=# \dt
                List of relations
 Schema |        Name         | Type  |  Owner
--------+---------------------+-------+----------
 public | users               | table | yugabyte
```

To list the table and the sequence you created, use the `\d` meta-command.

```sql
yugabyte=# \d
```

```output
 Schema |        Name         |   Type   |  Owner
--------+---------------------+----------+----------
 public | users               | table    | yugabyte
 public | users_id_seq        | sequence | yugabyte
```

To describe the table you created, enter the following:

```sql
\d users
```

```output
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

A schema is a logical container in a database that holds database objects such as tables, views, functions, and indexes. Schemas provide a way to organize objects into logical groups, making it easier to manage large databases with many objects and avoiding name conflicts.

To create the schema with name `myschema`, run the following command:

```sql
testdb=# CREATE SCHEMA myschema;
```

```output
CREATE SCHEMA
```

List the schemas as follows:

```sql
yugabyte=# \dn
```

```output
   List of schemas
   Name   |  Owner
----------+----------
 myschema | yugabyte
 public   | postgres
(2 rows)
```

To create a table in this schema, run the following:

```sql
yugabyte=# CREATE TABLE myschema.company(
   ID   INT              NOT NULL,
   NAME VARCHAR (20)     NOT NULL,
   AGE  INT              NOT NULL,
   ADDRESS  CHAR (25),
   SALARY   DECIMAL (18, 2),
   PRIMARY KEY (ID)
);
```

At this point, the `default` schema is still the selected schema, and running the `\d` meta-command would not list the table you just created.

To see which schema is currently the default, run the following.

```sql
yugabyte=# SHOW search_path;
```

You should see the following output.

```output
   search_path
-----------------
 "$user", public
(1 row)
```

To set `myschema` as the default schema in this session, do the following.

```sql
SET search_path=myschema;
```

Now list the table you created.

```sql
yugabyte=# SHOW search_path;
```

```output
 search_path
-------------
 myschema
(1 row)
```

List the table you created.

```sql
yugabyte=# \d
```

```output
           List of relations
  Schema  |  Name   | Type  |  Owner
----------+---------+-------+----------
 myschema | company | table | yugabyte
(1 row)
```

To drop the schema `myschema` and all the objects inside it, first change the current default schema.

```sql
yugabyte=# SET search_path=default;
```

Next, run the `DROP` statement as follows:

```sql
yugabyte=# DROP SCHEMA myschema CASCADE;
```

You should see the following output.

```output
NOTICE:  drop cascades to table myschema.company
DROP SCHEMA
```

## Quit ysqlsh

To quit the shell, enter the following meta-command:

```sql
yugabyte=# \q
```
