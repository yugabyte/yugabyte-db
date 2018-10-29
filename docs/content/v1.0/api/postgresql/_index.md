---
title: PostgreSQL (Beta)
linkTitle: PostgreSQL (Beta)
description: PostgreSQL (Beta)
summary: PostgreSQL overview and features
image: /images/section_icons/api/pgsql.png
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
menu:
  v1.0:
    identifier: api-postgresql
    parent: api
    weight: 3000
---

## Introduction
PostgreSQL support is under active development in YugaByte DB. We will keep updating this page as features are added.

- Data definition language (DDL) statements.
- Data manipulation language (DML) statements.
- Builtin functions and Expression operators.
- Primitive user-defined datatypes.

## Examples
The following examples show a limited set of PostgreSQL statements that can be used with YugaByte DB.

```
22:15 $ psql mytest --host=127.0.0.1 --port=5433
Database 'mytest' does not exist
message type 0x5a arrived from server while idle
psql (10.1, server 0.0.0)
Type "help" for help.

mytest=> create database mytest;
DATABASE CREATED
mytest=> \c mytest;
psql (10.1, server 0.0.0)
You are now connected to database "mytest" as user "yuga_dev".
mytest=> create table tab(id int, name varchar, salary float);
TABLE CREATED
mytest=> insert into tab values(1, 'one', 50000.99);
INSERT 0 1
mytest=> insert into tab values(2, 'two', 10000.00);
INSERT 0 1
mytest=> select * from tab;
 id | name |    salary    
----+------+--------------
  1 | one  | 50000.988281
  2 | two  | 10000.000000
(2 rows)

mytest=> \q
```

## DDL Statements
Data definition language (DDL) statements are instructions for the following database operations.

- Create and drop database objects.

Statement | Description |
----------|-------------|
[`CREATE DATABASE`](../ddl_create_database) | Create a new database |
[`CREATE TABLE`](../ddl_create_table) | Create a new table |
[`DROP DATABASE`](../ddl_drop_database) | Delete a database and associated objects |
[`DROP TABLE`](../ddl_drop_table) | Delete a table from a database |

## DML Statements
Data manipulation language (DML) statements read from and write to the existing database objects. Currently, YugaByte DB implicitly commits any updates by DML statements.

Statement | Description |
----------|-------------|
[`INSERT`](../dml_insert) | Insert rows into a table |
[`SELECT`](../dml_select) | Select rows from a table |
[`UPDATE`](../dml_update) | Will be added. Update rows in a table |
[`DELETE`](../dml_delete) | Will be added. Delete rows from a table |

## Expressions
Under development.

## Data Types
The following table lists all supported primitive types.

Primitive Type | Allowed in Key | Type Parameters | Description |
---------------|----------------|-----------------|-------------|
[`BIGINT`](../type_int) | Yes | - | 64-bit signed integer |
[`BOOLEAN`](../type_bool) | Yes | - | Boolean |
[`DECIMAL`](../type_number) | Yes | - | Exact, fixed-point number |
[`DOUBLE PRECISION`](../type_number) | Yes | - | 64-bit, inexact, floating-point number |
[`FLOAT`](../type_number) | Yes | - | 64-bit, inexact, floating-point number |
[`REAL`](../type_number) | Yes | - | 32-bit, inexact, floating-point number |
[`INT` &#124; `INTEGER`](../type_int) | Yes | - | 32-bit signed integer |
[`SMALLINT`](../type_int) | Yes | - | 16-bit signed integer |
[`TEXT` &#124; `VARCHAR`](../type_text) | Yes | - | Variable-size string of Unicode characters |
