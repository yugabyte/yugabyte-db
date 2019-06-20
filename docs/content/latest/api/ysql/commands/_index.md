---
title: Commands
description: Commands
summary: Commands
image: /images/section_icons/api/ysql.png
menu:
  latest:
    identifier: api-ysql-commands
    parent: api-ysql
    weight: 4100
aliases:
  - /latest/api/ysql/commands/
  - /latest/api/ysql/commands/ddl_drop_database/
isTocNested: true
showAsideToc: true
---

The following table lists the commands in YSQL.

| Statement | Description |
|-----------|-------------|
| [`ABORT`](txn_abort) | Rollback a transaction |
| [`ALTER DOMAIN`](ddl_alter_domain) | Alter a domain |
| [`ALTER TABLE`](ddl_alter_table) | Change table definition |
| [`BEGIN TRANSACTION`](txn_begin) | Start a transaction |
| [`COMMIT`](txn_commit) | Commit a transaction |
| [`COPY`](cmd_copy) | Copy data between tables and files |
| [`CREATE DATABASE`](ddl_create_database) | Create a new database |
| [`CREATE DOMAIN`](ddl_create_domain) | Create a new domain |
| [`CREATE INDEX`](ddl_create_index) | Create a new index |
| [`CREATE SCHEMA`](ddl_create_schema) | Create a new schema (namespace) |
| [`CREATE SEQUENCE`](ddl_create_sequence) | Create a new sequence generator |
| [`CREATE TABLE`](ddl_create_table) | Create a new table |
| [`CREATE TABLE AS`](ddl_create_table_as) | Create a new table |
| [`CREATE USER`](dcl_create_user) | Create a new user (role) |
| [`CREATE VIEW`](ddl_create_view) | Create a new view |
| [`DEALLOCATE`](perf_deallocate) | Deallocate a prepared statement |
| [`DELETE`](dml_delete) | Delete rows from a table |
| [`DROP DOMAIN`](ddl_drop_domain) | Delete a domain |
| [`DROP SEQUENCE`](ddl_drop_sequence) | Delete a sequence generator |
| [`DROP TABLE`](ddl_drop_table) | Delete a table from a database |
| [`END TRANSACTION`](txn_end) | Commit a transaction |
| [`EXECUTE`](perf_execute) | Execute a prepared statement |
| [`EXPLAIN`](perf_explain) | Display execution plan for a statement |
| [`INSERT`](dml_insert) | Insert rows into a table |
| [`PREPARE`](perf_prepare) | Prepare a statement |
| [`RESET`](cmd_reset) | Reset a parameter to factory settings |
| [`ROLLBACK`](txn_rollback) | Rollback a transaction |
| [`SELECT`](dml_select) | Select rows from a table |
| [`SET`](cmd_set) | Set a system, session, or transactional parameter |
| [`SET CONSTRAINTS`](txn_set_constraints) | Set constraints on current transaction |
| [`SET TRANSACTION`](txn_set) | Set transaction behaviors |
| [`SHOW`](cmd_show) | Show value of a system, session, or transactional parameter |
| [`SHOW TRANSACTION`](txn_show) | Show properties of a transaction |
| [`TRUNCATE`](ddl_truncate) | Clear all rows from a table |
| [`UPDATE`](dml_update) | Update rows in a table |
