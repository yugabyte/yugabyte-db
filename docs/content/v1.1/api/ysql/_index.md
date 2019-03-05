---
title: YSQL (Beta)
linkTitle: YSQL (Beta)
description: YugaByte Structured Query Language (YSQL) [Beta]
summary: Reference for PostgreSQL-compatible YSQL API
image: /images/section_icons/api/pgsql.png
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
aliases:
  - /latest/api/postgresql/
  - /latest/api/ysql/
menu:
  v1.1:
    identifier: api-postgresql
    parent: api
    weight: 3000
isTocNested: true
showAsideToc: true
---

## Introduction
YSQL is a distributed SQL API compatible with PostgreSQL. It supports the following features.

- Data definition language (DDL) statements.
- Data manipulation language (DML) statements.
- Builtin functions and Expression operators.
- Primitive user-defined datatypes.

## Example
The following example illustrates how to use `psql` to connect to YugaByte DB's PostgreSQL API.
It assumes you have [installed YugaByte](../../quick-start/install/) and started a [PostgreSQL-enabled cluster](../../quick-start/test-postgresql/).

```sh
$ bin/psql -p 5433 -U postgres
```

```
psql (10.3, server 10.4)
Type "help" for help.

postgres=#
```

```sql
postgres=# create table sample(id int primary key, name varchar, salary float);
CREATE TABLE
postgres=# insert into sample values(1, 'one', 50000.99);
INSERT 0 1
postgres=# insert into sample values(2, 'two', 10000.00);
INSERT 0 1
postgres=# select * from sample ORDER BY id DESC;
```

```
 id | name |  salary
----+------+----------
  2 | two  |    10000
  1 | one  | 50000.99
(2 rows)
```
The examples given in the rest of this section assume the cluster is running and `psql` is connected to it as described above.

## DDL Statements
Data definition language (DDL) statements are instructions for the following database operations.

- Create and drop database objects.

Statement | Description |
----------|-------------|
[`CREATE DATABASE`](ddl_create_database) | Create a new database |
[`CREATE TABLE`](ddl_create_table) | Create a new table |
[`DROP DATABASE`](ddl_drop_database) | Delete a database and associated objects |
[`DROP TABLE`](ddl_drop_table) | Delete a table from a database |
[`CREATE VIEW`](ddl_create_view) | Create a new view |
[`CREATE USER`](permissions) | Create a new user/role |
[`GRANT`](permissions) | Grant permissions|
[`REVOKE`](permissions) | Revoke permissions |

## DML Statements
Data manipulation language (DML) statements read from and write to the existing database objects. Currently, YugaByte DB implicitly commits any updates by DML statements.

Statement | Description |
----------|-------------|
[`INSERT`](dml_insert) | Insert rows into a table |
[`SELECT`](dml_select) | Select rows from a table |
[`UPDATE`] | In progress. Update rows in a table |
[`DELETE`] | In progress. Delete rows from a table |

## Transactions
Statement | Description |
----------|-------------|
[`ABORT` &#124; `ROLLBACK`](transactions) | Rollback a transaction |
[`BEGIN`](transactions) | Start a transaction |
[`END` &#124; `COMMIT`](transactions) | Commit a transaction |

## Other SQL Statements

Statement | Description |
----------|-------------|
[`EXECUTE`](prepare_execute) | Insert rows into a table |
[`EXPLAIN`](explain) | Insert rows into a table |
[`PREPARE`](prepare_execute) | Select rows from a table |
[`SET`](transactions) | Select rows from a table |
[`SHOW`](transactions) | Select rows from a table |

## Expressions
PostgreSQL builtin functions and operators are supported. 
User-defined functions are currently in progress.

## Data Types
The following table lists all supported primitive types.

Primitive Type | Allowed in Key | Type Parameters | Description |
---------------|----------------|-----------------|-------------|
[`BIGINT`](type_int) | Yes | - | 64-bit signed integer |
[`DOUBLE PRECISION`](type_number) | Yes | - | 64-bit, inexact, floating-point number |
[`FLOAT`](type_number) | Yes | - | 64-bit, inexact, floating-point number |
[`REAL`](type_number) | Yes | - | 32-bit, inexact, floating-point number |
[`INT` &#124; `INTEGER`](type_int) | Yes | - | 32-bit signed integer |
[`SMALLINT`](type_int) | Yes | - | 16-bit signed integer |
[`TEXT` &#124; `VARCHAR`](type_text) | Yes | - | Variable-size string of Unicode characters |
