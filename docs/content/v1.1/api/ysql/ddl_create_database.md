---
title: CREATE DATABASE
summary: Create a new database
description: CREATE DATABASE
menu:
  v1.1:
    identifier: api-postgresql-create-db
    parent: api-postgresql-ddl
aliases:
  - /latest/api/postgresql/ddl_create_database
  - /latest/api/ysql/ddl_create_database
isTocNested: true
showAsideToc: true
---

## Synopsis
The `CREATE DATABASE` statement creates a `database` that functions as a grouping mechanism for database objects such as [tables](../ddl_create_table).

## Syntax

### Diagram

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="235" height="35" viewbox="0 0 235 35"><path class="connector" d="M0 22h5m67 0h10m84 0h10m54 0h5"/><rect class="literal" x="5" y="5" width="67" height="25" rx="7"/><text class="text" x="15" y="22">CREATE</text><rect class="literal" x="82" y="5" width="84" height="25" rx="7"/><text class="text" x="92" y="22">DATABASE</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="176" y="5" width="54" height="25"/><text class="text" x="186" y="22">name</text></a></svg>

### Grammar
```
create_database ::= CREATE DATABASE name
```
Where

- `name` is an identifier.

## Semantics

- An error is raised if a database with the specified `name` already exists.

## See Also
[`DROP DATABASE`](../ddl_drop_database)
[Other PostgreSQL Statements](..)
