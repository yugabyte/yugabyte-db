---
title: Statements
description: Statements
summary: Statements
image: /images/section_icons/api/ysql.png
block_indexing: true
menu:
  v1.3:
    identifier: api-ysql-commands
    parent: api-ysql
    weight: 4100
isTocNested: true
showAsideToc: true
---

The following SQL statements are supported by the Yugabyte Structured Query Language (YSQL).

| Statement | Description |
|-----------|-------------|
| [`ABORT`](txn_abort) | Rolls back a transaction |
| [`ALTER DATABASE`](ddl_alter_db) | Changes database definition |
| [`ALTER DOMAIN`](ddl_alter_domain) | Alters a domain |
| [`ALTER TABLE`](ddl_alter_table) | Changes table definition |
| [`BEGIN`](txn_begin) | Starts a transaction |
| [`COMMENT`](ddl_comment) | Adds a comment on a database object |
| [`COMMIT`](txn_commit) | Commits a transaction |
| [`COPY`](cmd_copy) | Copy data between tables and files |
| [`CREATE DATABASE`](ddl_create_database) | Create a new database |
| [`CREATE DOMAIN`](ddl_create_domain) | Create a new domain |
| [`CREATE INDEX`](ddl_create_index) | Create a new index |
| [`CREATE SCHEMA`](ddl_create_schema) | Create a new schema (namespace) |
| [`CREATE SEQUENCE`](ddl_create_sequence) | Create a new sequence generator |
| [`CREATE TABLE`](ddl_create_table) | Create a new table |
| [`CREATE TABLE AS`](ddl_create_table_as) | Create a new table |
| [`CREATE TYPE`](ddl_create_type) | Create a new type |
| [`CREATE USER`](dcl_create_user) | Create a new user (role) |
| [`CREATE VIEW`](ddl_create_view) | Create a new view |
| [`DEALLOCATE`](perf_deallocate) | Deallocate a prepared statement |
| [`DELETE`](dml_delete) | Delete rows from a table |
| [`DROP DATABASE`](ddl_drop_database) | Delete a database from the system |
| [`DROP DOMAIN`](ddl_drop_domain) | Delete a domain |
| [`DROP SEQUENCE`](ddl_drop_sequence) | Delete a sequence generator |
| [`DROP TABLE`](ddl_drop_table) | Deletes a table from a database |
| [`DROP TYPE`](ddl_drop_type) | Delete a user-defined type |
| [`END`](txn_end) | Commit a transaction |
| [`EXECUTE`](perf_execute) | Execute a prepared statement |
| [`EXPLAIN`](perf_explain) | Display execution plan for a statement |
| [`INSERT`](dml_insert) | Insert rows into a table |
| [`LOCK`](txn_lock) | Locks a table |
| [`PREPARE`](perf_prepare) | Prepare a statement |
| [`RESET`](cmd_reset) | Reset a parameter to factory settings |
| [`REVOKE`](dcl_revoke) | Remove access privileges |
| [`ROLLBACK`](txn_rollback) | Rollback a transaction |
| [`SELECT`](dml_select) | Select rows from a table |
| [`SET`](cmd_set) | Set a system, session, or transactional parameter |
| [`SET CONSTRAINTS`](txn_set_constraints) | Set constraints on current transaction |
| [`SET TRANSACTION`](txn_set) | Set transaction behaviors |
| [`SHOW`](cmd_show) | Show value of a system, session, or transactional parameter |
| [`SHOW TRANSACTION`](txn_show) | Show properties of a transaction |
| [`TRUNCATE`](ddl_truncate) | Clear all rows from a table |
| [`UPDATE`](dml_update) | Update rows in a table |
