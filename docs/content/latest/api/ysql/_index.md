---
title: YSQL
linkTitle: YSQL
description: YugaByte Structured Query Language (YSQL) [Beta]
summary: Reference for the YSQL API
image: /images/section_icons/api/pgsql.png
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
menu:
  latest:
    identifier: api-ysql
    parent: api
    weight: 3000
aliases:
  - /latest/api/ysql/
isTocNested: true
showAsideToc: true
---

## Introduction
YSQL is a distributed SQL API compatible with PostgreSQL. It supports the following features.

- Data definition language (DDL) statements are provided to define a database structure, modify it, and delete it by using CREATE, ALTER, and DROP commands respectively.
- Data manipulation language (DML) statements are provided to modify the contents of a database by using INSERT, UPDATE, DELETE, and SELECT commands.
- Data control language (DCL) statements are provided to protect and prevent it from corruptions by using GRANT and REVOKE commands.
- There are also a host of other commands for different purposes such as system control, transaction control, and performance tuning.
- Builtin datatypes are provided to specify a database object.
- Builtin functions and expression operators are provided for performance purpose as selected data are computed and filtered on server side before being sent to clients.

## Example
The following example illustrates how to use `psql` to connect to YugaByte DB's PostgreSQL-compatible API. It assumes you have [installed YugaByte](../../quick-start/install/) and started a [PostgreSQL-enabled cluster](../../quick-start/test-postgresql/).

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

## SQL Commands

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
