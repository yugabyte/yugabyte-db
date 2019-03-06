---
title: Commands
description: Overview on YSQL Commands
summary: Overview on YSQL Commands
image: /images/section_icons/api/pgsql.png
menu:
  latest:
    identifier: api-ysql-commands
    parent: api-ysql
    weight: 4100
aliases:
  - /latest/api/ysql/commands/
isTocNested: true
showAsideToc: true
---

The following table lists all SQL commands that are supported by YugaByte Database.

| Statement | Description |
|-----------|-------------|
| [`ABORT`](txn_abort) | Rollback a transaction |
| [`ALTER DATABASE`](ddl_alter_db) |  |
| [`ALTER TABLE`](ddl_alter_table) |  |
| [`BEGIN TRANSACTION`](txn_begin) | Start a transaction |
| [`COMMIT`](txn_commit) | Commit a transaction |
| [`COPY`](cmd_copy) |  |
| [`CREATE DATABASE`](ddl_create_database) | Create a new database |
| [`CREATE INDEX`](ddl_create_index) | Create a new index |
| [`CREATE SCHEMA`](ddl_create_schema) | Create a new schema (namespace) |
| [`CREATE SEQUENCE`](ddl_create_seq) |  |
| [`CREATE TABLE`](ddl_create_table) | Create a new table |
| [`CREATE TABLE AS`](ddl_create_table_as) | Create a new table |
| [`CREATE USER`](dcl_create_user) | Create a new user (role) |
| [`CREATE VIEW`](ddl_create_view) | Create a new view |
| [`DEALLOCATE`](txn_deallocate) |  |
| [`DELETE`](dml_delete) | Delete rows from a table |
| [`DROP DATABASE`](ddl_drop_database) | Delete a database and associated objects |
| [`DROP TABLE`](ddl_drop_table) | Delete a table from a database |
| [`END TRANSACTION`](txn_end) | Commit a transaction |
| [`EXECUTE`](perf_execute) | Insert rows into a table |
| [`EXPLAIN`](perf_explain) | Insert rows into a table |
| [`GRANT`](dcl_grant) | Grant permissions |
| [`INSERT`](dml_insert) | Insert rows into a table |
| [`LOCK`](txn_lock) |  |
| [`PREPARE`](perf_prepare) | Select rows from a table |
| [`RESET`](cmd_reset) |  |
| [`REVOKE`](dcl_revoke) | Revoke permissions |
| [`ROLLBACK`](txn_rollback) | Rollback a transaction |
| [`SELECT`](dml_select) | Select rows from a table |
| [`SET`](cmd_set) |  |
| [`SET CONSTRAINTS`](cmd_set_constraints) |  |
| [`SET TRANSACTION`](txn_set) |  |
| [`SHOW`](cmd_show) |  |
| [`SHOW TRANSACTION`](txn_show) |  |
| [`TRUNCATE`](ddl_truncate) | Clear all rows from a table |
| [`UPDATE`](dml_update) | Update rows in a table |
