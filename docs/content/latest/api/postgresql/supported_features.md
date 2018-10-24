---
title: Supported Features
summary: Listing supported PosgreSQL features.
description: Supported PostgreSQL Features by YugaByte
menu:
  latest:
    identifier: api-postgresql-supported-features
    parent: api-postgresql
aliases:
  - api/postgresql/supported_features
  - api/pgsql/supported_features
---

## Synopsis
This document lists PostgreSQL 10.4 features that are supported in the latest release of YugaByte DB.

## DDL Statements

### DATABASE

<b>CREATE DATABASE</b> [<i>Spec</i>](https://www.postgresql.org/docs/current/static/sql-createdatabase.html)

-- Optional parameters are not supported.

<b>DROP DATABASE</b> Not supported.

### SCHEMA

<b>CREATE SCHEMA</b> [<i>Spec</i>](https://www.postgresql.org/docs/current/static/sql-createschema.html)

-- AUTHORIZATION clause and all optional elements are not supported.

<b>DROP SCHEMA</b> Not supported.

### TABLE

<b>CREATE TABLE</b> [<i>Spec</i>](https://www.postgresql.org/docs/current/static/sql-createtable.html)

-- The table must have primary key.

-- TABLE optional parameters such as TEMPORARY are not supported.

-- TABLE optional clauses such as LIKE and INHERITS are not supported.


<b>DROP TABLE</b> [<i>Spec</i>](https://www.postgresql.org/docs/current/static/sql-droptable.html)

-- Optional parameters are not supported.

### USER

<b>CREATE USER</b> [<i>Spec</i>](https://www.postgresql.org/docs/current/static/sql-createuser.html)

-- USER creating optional parameters are not supported.

<b>DROP USER</b> Not supported.

### VIEW

<b>CREATE VIEW</b> [<i>Spec</i>](https://www.postgresql.org/docs/current/static/sql-createview.html)

-- VIEW creating optional parameters are not supported.

<b>DROP VIEW</b> Not supported.

## DML Statements

### INSERT Statement

-- [<i>Specifications</i>](https://www.postgresql.org/docs/current/static/sql-insert.html)

-- Optional clauses, OVERRIDING, DEFAULT, ON CONFLICT, and RETURNING, are not supported.

### SELECT Statement

-- [<i>Specifications</i>](https://www.postgresql.org/docs/current/static/sql-select.html)

### Transaction Control Statements

The following statements are supported, but transaction mode options such as ISOLATION LEVEL, READ, and WRITE, are not supported.

ABORT [<i>Spec</i>](https://www.postgresql.org/docs/current/static/sql-abort.html)

BEGIN [<i>Spec</i>](https://www.postgresql.org/docs/current/static/sql-begin.html)

COMMIT [<i>Spec</i>](https://www.postgresql.org/docs/current/static/sql-commit.html)

END TRANSACTION [<i>Spec</i>](https://www.postgresql.org/docs/current/static/sql-end.html)

ROLLBACK [<i>Spec</i>](https://www.postgresql.org/docs/current/static/sql-rollback.html)

START TRANSACTION [<i>Spec</i>](https://www.postgresql.org/docs/current/static/sql-start-transaction.html)

## Datatypes

-- [<i>Specifications</i>](https://www.postgresql.org/docs/current/static/datatype.html)

-- Character datatypes without modifiers are supported.

-- BIGINT, INT, SMALLINT, FLOAT, and DOUBLE are supported.

## Expressions

-- [<i>Specifications</i>](https://www.postgresql.org/docs/current/static/functions.html)

-- All operators and builtin functions are supported.

-- User-defined functions are not.

## See Also
[PostgreSQL Statements](..)
