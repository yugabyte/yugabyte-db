---
title: YSQL
linkTitle: YSQL
description: YugaByte Structured Query Language (YSQL) [Beta]
summary: Reference for the YSQL API
image: /images/section_icons/api/ysql.png
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
menu:
  latest:
    identifier: api-ysql
    parent: api
    weight: 2900
aliases:
  - /latest/api/ysql/
isTocNested: true
showAsideToc: true
---

## Introduction
YSQL is a distributed SQL API that is compatible with the SQL dialect of PostgreSQL. Currently, the compatibility is with 11.2 version of PostgreSQL.

The main components of YSQL are Data definition language (DDL), Data manipulation language (DML), and Data control language (DCL). Several other components are also provided for different purposes such as system control, transaction control, and performance tuning.

A number of elements are used to construct the SQL language in YSQL such as datatypes, database objects, names and qualifiers, expressions, and comments.

## Example
The following example illustrates how to use `psql` to connect to the YSQL API. It assumes you have [installed YugaByte DB](../../quick-start/install/) and started a [YSQL-enabled cluster](../../quick-start/explore-ysql/).

```sh
$ bin/psql -p 5433 -U postgres
```

```
psql (11.2)
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
