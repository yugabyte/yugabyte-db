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
    weight: 3000
aliases:
  - /latest/api/ysql/
isTocNested: true
showAsideToc: true
---

## Introduction
YSQL - YugaByte Structured Query Language - is a distributed SQL API that is compatible with PostgreSQL. YSQL has several different components to serve different purposes. The main components of YSQL are Data definition language (DDL), Data manipulation language (DML), and Data control language (DCL), which offer commands to create, modify, and secure the database, respectively. There are also several other components for system control, transaction control, performance tuning, etc.

## Example
The following example illustrates how to use `psql` to connect to the YSQL API. It assumes you have [installed YugaByte DB](../../quick-start/install/) and started a [YSQL-enabled cluster](../../quick-start/explore-ysql/).

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
