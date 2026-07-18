---
title: Databases, schemas, and tables
linkTitle: Schemas and tables
description: Create and operate on databases, schemas, and tables
menu:
  v2025.1:
    identifier: explore-ysql-language-features-databases-schemas-tables
    parent: explore-ysql-language-features
    weight: 100
rightNav:
  hideH3: true
type: docs
---

To manage data in a database efficiently, you need to follow a structured process that involves creating databases, tables, and schemas. The following detailed guide can help you understand and implement each step.

{{<tip>}}
For the list of supported and unsupported schema relations operations, see [Schema operations](../../../api/ysql/sql-feature-support#schema-operations).
{{</tip>}}

## Setup

{{% explore-setup-single-new %}}

## Databases

A database is the highest level of data organization and serves as a container for all objects such as tables, views, indexes, functions, and schemas. A YugabyteDB cluster can manage multiple databases and each database is isolated from the others, ensuring data integrity and security.

### Create a database

By default, a database named `yugabyte` is already created. To create a new database, `testdb`, run the following statement:

```sql
CREATE DATABASE testdb;
```

This creates an empty database where you can create tables.

### Switch to the new database

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
testdb=# \l
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

To drop or delete the database, connect to another database and then use the DROP command.

{{<note>}}
You cannot drop the database you are connected to.
{{</note>}}

Connect to another database as follows:

```sql
testdb=# \c yugabyte
```

```bash{.nocopy}
You are now connected to database "yugabyte" as user "yugabyte".
```

Use the DROP command as follows:

```sql
yugabyte=# DROP DATABASE testdb;
```

```sql{.nocopy}
DROP DATABASE
```

## Tables

A table is the fundamental database object that stores the actual data in a structured format, consisting of rows and columns. Tables are created in a specific schema (by default the `public` schema) and contain the data that applications and users interact with. Each table has a defined structure, with columns representing the different attributes or fields of the data, and rows representing individual records or entries.

### Create a table

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

To list more information about the tables you created, use the `\d+` meta-command.

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

The `users_id_seq` sequence is the result of the `serial` datatype that has been used in the definition of the `id` column.

### Insert data

After the tables are set up, you can add data to them. To add a record to the table, you can use the INSERT command.

```sql
INSERT INTO users VALUES(1, 'Yoda');
```

As the statement does not have an explicit value for the column `email`, the default value of `NULL` is set for that column.

### Query data

You can retrieve data from tables using the SELECT statement. For example:

```sql
SELECT * FROM users;
```

To retrieve only certain columns, you can specify the column name as follows:

```sql
SELECT username FROM users;
```

### Alter a table

After a table is created, you might need to alter it by adding, removing, or modifying columns. You can use the [ALTER TABLE](../../../api/ysql/the-sql-language/statements/ddl_alter_table) command to perform these actions.

#### Add a column

To add a new column `address`, run the following command:

```sql
ALTER TABLE users ADD COLUMN address TEXT;
```

#### Drop a column

To drop an existing column, say `enabled`, you can run the following command:

```sql
ALTER TABLE users DROP COLUMN enabled ;
```

#### Modify a column name

To modify the name of a column, say to change the name of the `id` column to `user_id`, do the following:

```sql
ALTER TABLE users RENAME COLUMN id to user_id;
```

## Schemas

A schema is a logical container in a database that holds database objects such as tables, views, functions, and indexes. Schemas provide a way to organize objects into logical groups, making it easier to manage large databases with many objects, and avoiding name conflicts. By default, YugabyteDB creates a schema named `public` in each database.

### Create a schema

To create the schema with name `myschema`, run the following command:

```sql
testdb=# CREATE SCHEMA myschema;
```

### List schemas

List the schemas as follows:

```sql
yugabyte=# \dn
```

```caddyfile{.nocopy}
   Name   |  Owner
----------+----------
 myschema | yugabyte
 public   | postgres
(2 rows)
```

### Current schema

To see which schema is currently the default, run the following:

```sql
yugabyte=# SHOW search_path;
```

```caddyfile{.nocopy}
   search_path
-----------------
 "$user", public
(1 row)
```

### Create tables in a schema

To create a table in a specific schema, prefix the table name with the schema name. For example, create the table `users` in the schema `myschema`:

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

At this point, the `public` schema is still the selected schema, and running the `\d` meta-command would not list the table you just created.

### Switch schemas

To set `myschema` as the current schema in this session, do the following:

```sql
SET search_path=myschema;
```

```sql
yugabyte=# SHOW search_path;
```

```caddyfile{.nocopy}
 search_path
-------------
 myschema
```

List the table you created.

```sh
yugabyte=# \d
```

```caddyfile{.nocopy}
           List of relations
  Schema  | Name  | Type  |  Owner
----------+---------+-------+----------
 myschema | users | table | yugabyte
```

### Drop schemas

To drop the schema `myschema` and all the objects inside it, first change the current schema.

```sql
yugabyte=# SET search_path=public;
```

Next, run the DROP statement as follows:

```sql
yugabyte=# DROP SCHEMA myschema CASCADE;
```

You should see the following output.

```sql{.nocopy}
NOTICE:  drop cascades to table myschema.users
DROP SCHEMA
```

## Users

Managing users (also called roles) involves creating, altering, and deleting users, and managing their permissions.

By default, YugabyteDB has two admin users already created: `yugabyte` (the recommended user) and `postgres` (mainly for backward compatibility with PostgreSQL).

### Current user

You can display the current user information as follows:

```sql
yugabyte=# \conninfo
```

This should output the following:

```sql{.nocopy}
You are connected to database "yugabyte" as user "yugabyte" on host "127.0.0.1" at port "5433".
```

### List users

To check all the users provisioned, run the following meta-command:

```sql
yugabyte=# \du
```

```caddyfile{.nocopy}
                                     List of roles
  Role name   |                         Attributes                         | Member of
--------------+------------------------------------------------------------+-----------
 postgres     | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
 yb_db_admin  | No inheritance, Cannot login                               | {}
 yb_extension | Cannot login                                               | {}
 yb_fdw       | Cannot login                                               | {}
 yugabyte     | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
```

### Create a user

You can create a user with the [CREATE USER](../../../api/ysql/the-sql-language/statements/dcl_create_user) command. For example, to create a user `yoda` with password `feeltheforce` as follows.

```sql
CREATE USER yoda WITH PASSWORD 'feeltheforce';
```

### Change a user password

You can change passwords of existing users using the [ALTER USER](../../../api/ysql/the-sql-language/statements/dcl_alter_user) command. For example:

```sql
ALTER USER yoda WITH PASSWORD 'thereisnotry';
```

### User privileges

Users can be granted privileges to perform certain operations on databases and tables. Privileges include actions like SELECT, INSERT, UPDATE, DELETE, and more. You can provide privileges to users using the [GRANT](../../../api/ysql/the-sql-language/statements/dcl_grant) command:

```sql
GRANT SELECT, INSERT ON TABLE users TO yoda;
```

### Superuser privileges

To give a user superuser privileges (full administrative rights), you can use the [ALTER USER](../../../api/ysql/the-sql-language/statements/dcl_alter_user) command. For example:

```sql
ALTER USER yoda WITH SUPERUSER;
```

To revoke superuser privileges, do the following:

```sql
ALTER USER yoda WITH NOSUPERUSER;
```

### Drop a user

To delete a user, use the [DROP USER](../../../api/ysql/the-sql-language/statements/dcl_drop_user) command:

```sql
DROP USER yoda;
```
