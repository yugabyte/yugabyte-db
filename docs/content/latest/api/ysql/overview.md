---
title: Overview
description: YugaByte Structured Query Language
summary: Overview on YSQL
image: /images/section_icons/api/ysql.png
menu:
  latest:
    identifier: api-ysql-overview
    parent: api-ysql
    weight: 3100
aliases:
  - /latest/api/ysql/overview
isTocNested: true
showAsideToc: true
---

This section provides a summary of what YSQL is offering.

## Data Definition Language (DDL)
DDL commands are provided to define structures in YSQL Database, change their definition, and remove them by using CREATE, ALTER, and DROP commands respectively.

| Statement | Description |
|-----------|-------------|
| [`ALTER DATABASE`](ddl_alter_db) | Change database definition |
| [`ALTER TABLE`](ddl_alter_table) | Change table definition |
| [`CREATE DATABASE`](ddl_create_database) | Create a new database |
| [`CREATE INDEX`](ddl_create_index) | Create a new index |
| [`CREATE SCHEMA`](ddl_create_schema) | Create a new schema (namespace) |
| [`CREATE SEQUENCE`](ddl_create_sequence) | Create a new sequence generator |
| [`CREATE TABLE`](ddl_create_table) | Create a new table |
| [`CREATE TABLE AS`](ddl_create_table_as) | Create a new table |
| [`CREATE VIEW`](ddl_create_view) | Create a new view |
| [`DROP DATABASE`](ddl_drop_database) | Delete a database and associated objects |
| [`DROP SEQUENCE`](ddl_drop_sequence) | Delete a sequence generator |
| [`DROP TABLE`](ddl_drop_table) | Delete a table from a database |
| [`TRUNCATE`](ddl_truncate) | Clear all rows from a table |

## Data Manipulation Language (DML)
DML commands are provided to modify the contents of a YSQL Database.

| Statement | Description |
|-----------|-------------|
| [`DELETE`](dml_delete) | Delete rows from a table |
| [`INSERT`](dml_insert) | Insert rows into a table |
| [`SELECT`](dml_select) | Select rows from a table |
| [`UPDATE`](dml_update) | Update rows in a table |

## Data Control Language (DCL)
DCL commands are provided to protect and prevent YSQL Database from corruptions.

| Statement | Description |
|-----------|-------------|
| [`CREATE USER`](dcl_create_user) | Create a new user (role) |
| [`GRANT`](dcl_grant) | Grant permissions |
| [`REVOKE`](dcl_revoke) | Revoke permissions |

## Transaction Control Language (TCL)
TCL commands are provided to manage transactions of operations on YSQL database.

| Statement | Description |
|-----------|-------------|
| [`ABORT`](txn_abort) | Rollback a transaction |
| [`BEGIN TRANSACTION`](txn_begin) | Start a transaction |
| [`COMMIT`](txn_commit) | Commit a transaction |
| [`END TRANSACTION`](txn_end) | Commit a transaction |
| [`LOCK`](txn_lock) | Lock a table |
| [`ROLLBACK`](txn_rollback) | Rollback a transaction |
| [`SET CONSTRAINTS`](txn_set_constraints) | Set constraints on current transaction|
| [`SET TRANSACTION`](txn_set) | Set transaction behaviors |
| [`SHOW TRANSACTION`](txn_show) | Show properties of a transaction |

## Session and system Control

| Statement | Description |
|-----------|-------------|
| [`RESET`](cmd_reset) | Reset a variable to factory settings |
| [`SET`](cmd_set) | Set a system, session, or transactional parameter |
| [`SHOW`](cmd_show) | Show value of a system, session, or transactional parameter |

## Perfomance Control

| Statement | Description |
|-----------|-------------|
| [`DEALLOCATE`](perf_deallocate) | Deallocate a prepared statement |
| [`EXECUTE`](perf_execute) | Insert rows into a table |
| [`EXPLAIN`](perf_explain) | Insert rows into a table |
| [`PREPARE`](perf_prepare) | Select rows from a table |

## Other commands
| Statement | Description |
|-----------|-------------|
| [`COPY`](cmd_copy) | Copy data between tables and files |

## Language Elements
This section lists the main elements of YSQL.

- Keywords: List or [keywords](keywords).
- Names and Qualifiers: Some names are reserved for the system. List of [reserved names](reserved_names).
- Datatypes: All PostgreSQL compatible datatypes are provided. List of [datatypes](datatypes).
- Expressions
- Database Objects
- Comments
