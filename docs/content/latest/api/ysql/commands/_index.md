---
title: SQL Commands
description: Overview on PostgreSQL-Compatible Commands
summary: Overview on SQL Commands
image: /images/section_icons/api/pgsql.png
menu:
  latest:
    identifier: api-ysql-commands
    parent: api-ysql
    weight: 3100
aliases:
  - /latest/api/ysql/commands/
isTocNested: true
showAsideToc: true
---

The following table lists all SQL commands that are supported by YugaByte Database.

| Statement | Description |
|-----------|-------------|
| [`ABORT`&#124;`ROLLBACK`](transactions) | Rollback a transaction |
| [`BEGIN`](transactions) | Start a transaction |
| [`CREATE DATABASE`](ddl_create_database) | Create a new database |
| [`CREATE INDEX`](ddl_create_index) | Create a new index |
| [`CREATE TABLE`](ddl_create_table) | Create a new table |
| [`CREATE USER`](ddl_create_user) | Create a new user/role |
| [`CREATE VIEW`](ddl_create_view) | Create a new view |
| [`DELETE`](dml_delete) | Delete rows from a table |
| [`DROP TABLE`](ddl_drop_table) | Delete a table from a database |
| [`END` &#124; `COMMIT`](transactions) | Commit a transaction |
| [`EXECUTE`](prepare_execute) | Insert rows into a table |
| [`EXPLAIN`](explain) | Insert rows into a table |
| [`INSERT`](dml_insert) | Insert rows into a table |
| [`PREPARE`](prepare_execute) | Select rows from a table |
| [`SELECT`](dml_select) | Select rows from a table |
| [`SET`](transactions) | Select rows from a table |
| [`SHOW`](transactions) | Select rows from a table |
| [`TRUNCATE`](ddl_truncate_table) | Clear all rows from a table |
| [`UPDATE`](dml_update) | Update rows in a table |

<!---
These are commented out as they are not yet fully supported.
[`DROP DATABASE`](ddl_drop_database) | Delete a database and associated objects |
[`GRANT`](permissions) | Grant permissions|
[`REVOKE`](permissions) | Revoke permissions |
--->
