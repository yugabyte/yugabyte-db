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
YSQL - YugaByte Structured Query Language - is a distributed SQL API compatible with PostgreSQL. Similar to PostgreSQL, YSQL has several components being constructed by a number of elements.

The main components of YSQL are Data definition language (DDL), Data manipulation language (DML), and Data control language (DCL). Several other components are also provided for different purposes such as system control, transaction control, and performance tuning.

A number of elements are used to construct the languages in YSQL such as datatypes, database objects, names and qualifiers, expressions, and comments.

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

## References

### Commands
All suppoted commands are listed in the [Commands](commands/) section.

### Data Types
All PostgresSQL-compatible types are supported although not all of them can be used for columns in PRIMARY KEY yet. All suppoted types are listed in the [Data Types](datatypes/) section.

### Expressions
All PostgreSQL-compatible builtin functions and operators are supported. User-defined functions are currently in progress. All suppoted expressions are listed in the [Expressions](exprs/) section.
