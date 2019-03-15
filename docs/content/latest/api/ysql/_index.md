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
  - /latest/api/postgresql/
isTocNested: true
showAsideToc: true
---

## Introduction
YSQL is a distributed SQL API that is compatible with the SQL dialect of PostgreSQL. Currently, the compatibility is with 11.2 version of PostgreSQL. It is best fit for RDBMS workloads that need horizontal write scalability and global data distribution while also using relational modeling features such as JOINs, distributed transactions and referential integrity (such as foreign keys). 

The main components of YSQL are data definition language (DDL), data manipulation language (DML), and data control language (DCL). A number of elements are used to construct these components including datatypes, database objects, names and qualifiers, expressions, and comments. Several other components are also provided for different purposes such as system control, transaction control, and performance tuning.

## Quick Start

You can explore the basics of the YSQL API using the [Quick Start](../../quick-start/explore-ysql) steps.

## Data Definition Language (DDL)
DDL commands define structures in the database, change their definitions, as well as remove them by using CREATE, ALTER, and DROP commands respectively.

| Statement | Description |
|-----------|-------------|
| [`ALTER DATABASE`](commands/ddl_alter_db) | Change database definition |
| [`ALTER TABLE`](commands/ddl_alter_table) | Change table definition |
| [`CREATE DATABASE`](commands/ddl_create_database) | Create a new database |
| [`CREATE INDEX`](commands/ddl_create_index) | Create a new index |
| [`CREATE SCHEMA`](commands/ddl_create_schema) | Create a new schema (namespace) |
| [`CREATE SEQUENCE`](commands/ddl_create_sequence) | Create a new sequence generator |
| [`CREATE TABLE`](commands/ddl_create_table) | Create a new table |
| [`CREATE TABLE AS`](commands/ddl_create_table_as) | Create a new table |
| [`CREATE VIEW`](commands/ddl_create_view) | Create a new view |
| [`DROP DATABASE`](commands/ddl_drop_database) | Delete a database and associated objects |
| [`DROP SEQUENCE`](commands/ddl_drop_sequence) | Delete a sequence generator |
| [`DROP TABLE`](commands/ddl_drop_table) | Delete a table from a database |
| [`TRUNCATE`](commands/ddl_truncate) | Clear all rows from a table |

## Data Manipulation Language (DML)
DML commands modify the contents of the database.

| Statement | Description |
|-----------|-------------|
| [`DELETE`](commands/dml_delete) | Delete rows from a table |
| [`INSERT`](commands/dml_insert) | Insert rows into a table |
| [`SELECT`](commands/dml_select) | Select rows from a table |
| [`UPDATE`](commands/dml_update) | Update rows in a table |

## Data Control Language (DCL)
DCL commands protect and prevent the database from corruptions.

| Statement | Description |
|-----------|-------------|
| [`CREATE USER`](commands/dcl_create_user) | Create a new user (role) |
| [`GRANT`](commands/dcl_grant) | Grant permissions (under development) |
| [`REVOKE`](commands/dcl_revoke) | Revoke permissions (under development) |

## Transaction Control Language (TCL)
TCL commands manage transactions of operations on the database.

| Statement | Description |
|-----------|-------------|
| [`ABORT`](commands/txn_abort) | Rollback a transaction |
| [`BEGIN TRANSACTION`](commands/txn_begin) | Start a transaction |
| [`COMMIT`](commands/txn_commit) | Commit a transaction |
| [`END TRANSACTION`](commands/txn_end) | Commit a transaction |
| [`ROLLBACK`](commands/txn_rollback) | Rollback a transaction |
| [`SET CONSTRAINTS`](commands/txn_set_constraints) | Set constraints on current transaction|
| [`SET TRANSACTION`](commands/txn_set) | Set transaction behaviors |
| [`SHOW TRANSACTION`](commands/txn_show) | Show properties of a transaction |

## Session and System Control

| Statement | Description |
|-----------|-------------|
| [`RESET`](commands/cmd_reset) | Reset a parameter to factory settings |
| [`SET`](commands/cmd_set) | Set a system, session, or transactional parameter |
| [`SHOW`](commands/cmd_show) | Show value of a system, session, or transactional parameter |

## Performance Control

| Statement | Description |
|-----------|-------------|
| [`DEALLOCATE`](commands/perf_deallocate) | Deallocate a prepared statement |
| [`EXECUTE`](commands/perf_execute) | Execute a prepared statement |
| [`EXPLAIN`](commands/perf_explain) | Explain an execution plan for a statement |
| [`PREPARE`](commands/perf_prepare) | Prepare a statement |

## Other commands
| Statement | Description |
|-----------|-------------|
| [`COPY`](commands/cmd_copy) | Copy data between tables and files |

## Language Elements
This section lists the main elements of YSQL.

- [Keywords](keywords).
- Names and Qualifiers: Some names are reserved for the system. List of [reserved names](reserved_names).
- Datatypes: Most PostgreSQL-compatible datatypes are supported. List of [datatypes](datatypes).
- [Expressions](exprs)
- Database Objects
- Comments
