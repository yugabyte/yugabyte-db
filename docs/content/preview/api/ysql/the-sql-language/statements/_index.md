---
title: SQL statements [YSQL]
headerTitle: Categorized list of SQL statements
linkTitle: SQL statements
description: List of PostgreSQL-compatible SQL statements supported by Yugabyte SQL (YSQL)
image: /images/section_icons/api/subsection.png
menu:
  preview:
    identifier: statements
    parent: the-sql-language
    weight: 100
aliases:
  - /preview/api/ysql/commands/
type: indexpage
showRightNav: true
---

The YSQL statements are compatible with the SQL dialect that PostgreSQL supports. The sidebar lists all of the YSQL statements in alphabetical order. The following tables list them by category.

## Data definition language (DDL)

| Statement | Description |
| :-------- | :---------- |
| [`ALTER DATABASE`](ddl_alter_db) | Change database definition |
| [`ALTER DOMAIN`](ddl_alter_domain) | Change domain definition |
| [`ALTER FOREIGN DATA WRAPPER`](ddl_alter_foreign_data_wrapper) | Change foreign data wrapper definition |
| [`ALTER FOREIGN TABLE`](ddl_alter_foreign_table) | Change foreign table definition |
| [`ALTER INDEX`](ddl_alter_index) | Change index definition |
| [`ALTER MATERIALIZED VIEW`](ddl_alter_matview) | Change materialized view definition |
| [`ALTER SEQUENCE`](ddl_alter_sequence) | Change sequence definition |
| [`ALTER SERVER`](ddl_alter_server) | Change foreign server definition |
| [`ALTER SCHEMA`](ddl_alter_schema) | Change schema definition |
| [`ALTER TABLE`](ddl_alter_table) | Change table definition |
| [`COMMENT`](ddl_comment) | Set, update, or remove a comment on a database object |
| [`CREATE AGGREGATE`](ddl_create_aggregate) | Create an aggregate |
| [`CREATE CAST`](ddl_create_cast) | Create a cast |
| [`CREATE DATABASE`](ddl_create_database) | Create a database |
| [`CREATE DOMAIN`](ddl_create_domain) | Create a user-defined data type with optional constraints |
| [`CREATE EXTENSION`](ddl_create_extension) | Load an extension |
| [`CREATE FOREIGN DATA WRAPPER`](ddl_create_foreign_data_wrapper) | Create a foreign-data wrapper |
| [`CREATE FOREIGN TABLE`](ddl_create_foreign_table) | Create a foreign table |
| [`CREATE FUNCTION`](ddl_create_function) | Create a function |
| [`CREATE INDEX`](ddl_create_index/) | Create an index |
| [`CREATE MATERIALIZED VIEW`](ddl_create_matview) | Create a materialized view |
| [`CREATE OPERATOR`](ddl_create_operator) | Create an operator |
| [`CREATE OPERATOR CLASS`](ddl_create_operator_class) | Create an operator class |
| [`CREATE PROCEDURE`](ddl_create_procedure) | Create a procedure |
| [`CREATE RULE`](ddl_create_rule) | Create a rule |
| [`CREATE SCHEMA`](ddl_create_schema) | Create a schema (namespace) |
| [`CREATE SEQUENCE`](ddl_create_sequence) | Create a sequence generator |
| [`CREATE SERVER`](ddl_create_server) | Create a foreign server |
| [`CREATE TABLE`](ddl_create_table) | Create an empty table |
| [`CREATE TABLE AS`](ddl_create_table_as) | Create a table from the results of a executing a `SELECT` |
| [`CREATE TABLESPACE`](ddl_create_tablespace)                     | Create a tablespace                                       |
| [`CREATE TRIGGER`](ddl_create_trigger) | Create a trigger |
| [`CREATE TYPE`](ddl_create_type) | Create a type |
| [`CREATE USER MAPPING`](ddl_create_user_mapping) | Create a user mapping |
| [`CREATE VIEW`](ddl_create_view) | Create a view |
| [`DROP AGGREGATE`](ddl_drop_aggregate) | Delete an aggregate |
| [`DROP CAST`](ddl_drop_cast) | Delete a cast |
| [`DROP DATABASE`](ddl_drop_database) | Delete a database from the system |
| [`DROP DOMAIN`](ddl_drop_domain) | Delete a domain |
| [`DROP EXTENSION`](ddl_drop_extension) | Delete an extension |
| [`DROP FOREIGN DATA WRAPPER`](ddl_drop_foreign_data_wrapper) | Drop a foreign-data wrapper |
| [`DROP FOREIGN TABLE`](ddl_drop_foreign_table) | Drop a foreign table |
| [`DROP FUNCTION`](ddl_drop_function) | Delete a function |
| [`DROP INDEX`](ddl_drop_index) | Delete an index from a database |
| [`DROP MATERIALIZED VIEW`](ddl_drop_matview) | Drop a materialized view |
| [`DROP OPERATOR`](ddl_drop_operator) | Delete an operator |
| [`DROP OPERATOR CLASS`](ddl_drop_operator_class) | Delete an operator class |
| [`DROP PROCEDURE`](ddl_drop_procedure) | Delete a procedure |
| [`DROP RULE`](ddl_drop_rule) | Delete a rule |
| [`DROP SCHEMA`](ddl_drop_schema) | Delete a schema from the system |
| [`DROP SEQUENCE`](ddl_drop_sequence) | Delete a sequence generator |
| [`DROP SERVER`](ddl_drop_server) | Drop a foreign server |
| [`DROP TABLE`](ddl_drop_table) | Delete a table from a database |
| [`DROP TABLESPACE`](ddl_drop_tablespace) | Delete a tablespace from the cluster |
| [`DROP TYPE`](ddl_drop_type) | Delete a user-defined type |
| [`DROP TRIGGER`](ddl_drop_trigger) | Delete a trigger |
| [`IMPORT FOREIGN SCHEMA`](ddl_import_foreign_schema) | Import a foreign schema |
| [`REFRESH MATERIALIZED VIEW`](ddl_refresh_matview) | Refresh a materialized view |
| [`TRUNCATE`](ddl_truncate) | Clear all rows from a table |

## Data manipulation language (DML)

| Statement                 | Description                               |
| :------------------------ | :---------------------------------------- |
| [`CLOSE`](dml_close/)     | Remove a cursor                           |
| [`DECLARE`](dml_declare/) | Create a cursor                           |
| [`DELETE`](dml_delete/)   | Delete rows from a table                  |
| [`FETCH`](dml_fetch/)     | Fetch rows from a cursor                  |
| [`INSERT`](dml_insert/)   | Insert rows into a table                  |
| [`MOVE`](dml_move/)       | Move the current position within a cursor |
| [`SELECT`](dml_select/)   | Select rows from a table                  |
| [`UPDATE`](dml_update/)   | Update rows in a table                    |

## Data control language (DCL)

| Statement | Description |
| :-------- | :---------- |
| [`ALTER DEFAULT PRIVILEGES`](dcl_alter_default_privileges) | Define default privileges |
| [`ALTER GROUP`](dcl_alter_group) | Alter a group |
| [`ALTER POLICY`](dcl_alter_policy) | Alter a row level security policy |
| [`ALTER ROLE`](dcl_alter_role) | Alter a role (user or group) |
| [`ALTER USER`](dcl_alter_user) | Alter a user |
| [`CREATE GROUP`](dcl_create_group) | Create a group (role) |
| [`CREATE POLICY`](dcl_create_policy) | Create a row level security policy |
| [`CREATE ROLE`](dcl_create_role) | Create a role (user or group) |
| [`CREATE USER`](dcl_create_user) | Create a user (role) |
| [`DROP GROUP`](dcl_drop_group) | Drop a group |
| [`DROP POLICY`](dcl_drop_policy) | Drop a row level security policy |
| [`DROP ROLE`](dcl_drop_role) | Drop a role (user or group) |
| [`DROP OWNED`](dcl_drop_owned) | Drop owned objects |
| [`DROP USER`](dcl_drop_user) | Drop a user |
| [`GRANT`](dcl_grant) | Grant permissions |
| [`REASSIGN OWNED`](dcl_reassign_owned) | Reassign owned objects |
| [`REVOKE`](dcl_revoke) | Revoke permissions |
| [`SET ROLE`](dcl_set_role) | Set a role |
| [`SET SESSION AUTHORIZATION`](dcl_set_session_authorization) | Set session authorization |

## Transaction control language (TCL)

| Statement | Description |
| :-------- | :---------- |
| [`ABORT`](txn_abort) | Roll back a transaction |
| [`BEGIN`](txn_begin/) | Start a transaction |
| [`COMMIT`](txn_commit) | Commit a transaction |
| [`END`](txn_end) | Commit a transaction |
| [`LOCK`](txn_lock) | Lock a table |
| [`ROLLBACK`](txn_rollback) | Roll back a transaction |
| [`SET CONSTRAINTS`](txn_set_constraints) | Set constraints on current transaction |
| [`SET TRANSACTION`](txn_set) | Set transaction behaviors |
| [`SHOW TRANSACTION`](txn_show) | Show properties of a transaction |
| [`START TRANSACTION`](txn_start) | Start a transaction |
| [`SAVEPOINT`](savepoint_create) | Create a new savepoint |
| [`ROLLBACK TO`](savepoint_rollback) | Rollback to a savepoint |
| [`RELEASE`](savepoint_release) | Release a savepoint |

## Session and system control

| Statement | Description |
| :-------- | :---------- |
| [`RESET`](cmd_reset) | Reset a run-time parameter to its default value |
| [`SET`](cmd_set) | Set the value of a run-time parameter |
| [`SHOW`](cmd_show) | Show the value of a run-time parameter |

## Performance control

| Statement | Description |
| :-------- | :---------- |
| [`DEALLOCATE`](perf_deallocate) | Deallocate a prepared statement |
| [`EXECUTE`](perf_execute) | Execute a prepared statement |
| [`EXPLAIN`](perf_explain) | Explain an execution plan for a statement |
| [`PREPARE`](perf_prepare) | Prepare a statement |

## Other statements

| Statement | Description |
| :-------- | :---------- |
| [`ANALYZE`](cmd_analyze) | Collect statistics about a database |
| [`COPY`](cmd_copy) | Copy data between tables and files |
| [`DO`](cmd_do) | Execute an anonymous PL/pgSQL code block |
