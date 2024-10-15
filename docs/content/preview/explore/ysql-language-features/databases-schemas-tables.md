---
title: Databases, schemas and tables
linkTitle: Schemas and tables
description: Create and operate on databases, schemas and tables
menu:
  preview:
    identifier: explore-ysql-language-features-databases-schemas-tables
    parent: explore-ysql-language-features
    weight: 100
rightNav:
  hideH3: true
type: docs
---

Creating databases, tables, and schemas involves a structured process that enables efficient data management. Below is a detailed guide to help you understand and implement each step.

## Setup

The examples will run on any YugabyteDB universe. To create a universe follow the instructions below.

<!-- begin: nav tabs -->
{{<nav/tabs list="local,anywhere,cloud" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
<!-- local cluster setup instructions -->
{{<setup/local numnodes="1" rf="1" >}}

{{</nav/panel>}}

{{<nav/panel name="anywhere">}} {{<setup/anywhere>}} {{</nav/panel>}}
{{<nav/panel name="cloud">}}{{<setup/cloud>}}{{</nav/panel>}}
{{</nav/panels>}}
<!-- end: nav tabs -->

## Databases

A database is the highest level of data organization and serves as a container for all objects such as tables, views, indexes, functions, and schemas. A YugabyteDB cluster can manage multiple databases and each database is isolated from the others, ensuring data integrity and security.

### Create a database

By default, a database named `yugabyte` is already created. To create a new database say, `testdb`, run the following statement:

```sql
CREATE DATABASE testdb;
```

This creates an empty database where you can create tables.

### Switching to the new database

To switch or connect to the new database, use the `\c` meta-command as follows:

```sql
\c testdb
```

You should see the following output:

```bash{.nocopy}
You are now connected to database "testdb" as user "yugabyte".
testdb=#
```

### List databases

To list all databases, use the `\l` or `\list` meta-commands.

```bash
yugabyte=# \l
```

```caddyfile{.nocopy}
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
```

### Drop database

To drop or delete the database we just created, connect to another database and then use the `DROP` command.

{{<warning>}}
You cannot drop the database you are connected to.
{{</warning>}}

Connect to another database as follows:

```sql
testdb=# \c yugabyte
```

```bash{.nocopy}
You are now connected to database "yugabyte" as user "yugabyte".
```

Use the `DROP` command as follows:

```sql
yugabyte=# DROP DATABASE testdb;
```

```sql{.nocopy}
DROP DATABASE
```

## Tables

A table is the fundamental database object that stores the actual data in a structured format, consisting of rows and columns. Tables are created in a specific schema (by default public) and contain the data that applications and users interact with. Each table has a defined structure, with columns representing the different attributes or fields of the data, and rows representing individual records or entries.

### Creating a table

Create a table using the [CREATE TABLE](../../../api/ysql/the-sql-language/statements/ddl_create_table) statement.

```sql
CREATE TABLE users (
  id serial,
  username CHAR(25) NOT NULL,
  email TEXT DEFAULT NULL,
  PRIMARY KEY (id)
);
```

```sql{.nocopy}
CREATE TABLE
```

To list all tables, use the `\dt` meta-command.

```sql
yugabyte=# \dt
```

```caddyfile{.nocopy}
                List of relations
 Schema |        Name         | Type  |  Owner
--------+---------------------+-------+----------
 public | users               | table | yugabyte
```

To list more ionformation about the tables you created, use the `\d+` meta-command.

```sql
yugabyte=# \d+
```

```caddyfile{.nocopy}
                          List of relations
 Schema |     Name     |   Type   |  Owner   |  Size   | Description
--------+--------------+----------+----------+---------+-------------
 public | users        | table    | yugabyte | 3072 kB |
 public | users_id_seq | sequence | yugabyte | 0 bytes |
```

{{<note>}}
The `users_id_seq` sequence you see above is the result of the `serial` datatype that has been used in the definition of the `id` column.
{{</note>}}

### Inserting Data

Once the tables are set up, you can insert data into them. To insert a record into the above table, you can use the INSERT command.

```sql
INSERT INTO users VALUES(1, 'Yoda');
```

{{<note>}}
The above statement does not have an explicit value for the column `email` as a default value of `NULL` has been set for that column.
{{</note>}}

### Querying Data

You can retrieve data from tables using the SELECT statement. For eg,

```sql
SELECT * FROM users;
```

To retrieve only certain columns, you can specify the column name as,

```sql
SELECT username FROM users;
```

### Altering a table

Once a table is created, you might need to alter it by adding, removing, or modifying columns. You can use the [ALTER TABLE](../../../api/ysql/the-sql-language/statements/ddl_alter_table) command to perform such actions.

#### Add a column

To add a new column say `address`, you can run the following command:

```sql
ALTER TABLE users ADD COLUMN address TEXT;
```

#### Drop a column

To drop an existing column, say `enabled`, you can run the following command:

```sql
ALTER TABLE users DROP COLUMN  enabled ;
```

#### Modify a column's name

To modify the name of a column, say to change the name of `id` column to `user_id` you can do:

```sql
ALTER TABLE users RENAME COLUMN id to user_id;
```

## Schemas

A schema is a logical container in a database that holds database objects such as tables, views, functions, and indexes. Schemas provide a way to organize objects into logical groups, making it easier to manage large databases with many objects and avoiding name conflicts. By default, YugabyteDB creates a schema named `public` in each database.

### Creating a schema

To create the schema with name `myschema`, run the following command:

```sql
testdb=# CREATE SCHEMA myschema;
```

```output
CREATE SCHEMA
```

### Listing schemas

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

### Current schema

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

### Creating tables in a schema

To create a table in a specific schema, prefix the table name with the schema name. For example, to create the table `users` in the schema `myschema`.

```sql
yugabyte=# CREATE TABLE myschema.users(
   ID   INT              NOT NULL,
   NAME VARCHAR (20)     NOT NULL,
   AGE  INT              NOT NULL,
   ADDRESS  CHAR (25),
   SALARY   DECIMAL (18, 2),
   PRIMARY KEY (ID)
);
```

{{<note>}}
At this point, the `default` schema is still the selected schema, and running the `\d` meta-command would not list the table you just created.
{{</note>}}

### Switching schemas

To set `myschema` as the default schema in this session, do the following.

```sql
SET search_path=myschema;
```

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

### Dropping schemas

To drop the schema `myschema` and all the objects inside it, first change the current default schema.

```sql
yugabyte=# SET search_path=default;
```

Next, run the `DROP` statement as follows:

```sql
yugabyte=# DROP SCHEMA myschema CASCADE;
```

You should see the following output.

```sql{.nocopy}
NOTICE:  drop cascades to table myschema.company
DROP SCHEMA
```

## Users

Managing users (also called roles) involves creating, altering, deleting users, and managing their permissions.

{{<note>}}
By default, YugabyteDB has two admin users already created: `yugabyte` (the recommended user) and `postgres` (mainly for backward compatibility with PostgreSQL).
{{</note>}}

### Current user

You can display the current user information as follows:

```sql
yugabyte=# \conninfo
```

This should output the following:

```sql{.nocopy}
You are connected to database "yugabyte" as user "yugabyte" on host "127.0.0.1" at port "5433".
```

### Listing users

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

### Creating a User

You can create a user with the [CREATE USER](../../../api/ysql/the-sql-language/statements/dcl_create_user) command. For example, to create a user `yoda` with password `feeltheforce` as follows.

```sql
CREATE USER yoda WITH PASSWORD 'feeltheforce';
```

### Changing a user's password

You can change passwords of existing users using the [ALTER USER](../../../api/ysql/the-sql-language/statements/dcl_alter_user) command. For example,

```sql
ALTER USER yoda WITH PASSWORD 'thereisnotry';
```

### User privileges

Users can be granted privileges to perform certain operations on databases and tables. Privileges include actions like `SELECT`, `INSERT`, `UPDATE`, `DELETE`, and more. You can provide privileges to users using the [GRANT](../../../api/ysql/the-sql-language/statements/dcl_grant) command like:

```sql
GRANT SELECT, INSERT ON TABLE users TO yoda;
```

### Superuser privileges

To give a user superuser privileges (full administrative rights), you can use the [ALTER USER](../../../api/ysql/the-sql-language/statements/dcl_alter_user) command. For example,

```sql
ALTER USER yoda WITH SUPERUSER;
```

To revoke superuser privileges, you can do,

```sql
ALTER USER yoda WITH NOSUPERUSER;
```

### Dropping a user

You can drop/delete a user with the [DROP USER](../../../api/ysql/the-sql-language/statements/dcl_drop_user) command like:

```sql
DROP USER yoda ;
```
