---
title: DROP DATABASE
summary: Removes a database and all of its database objects
description: DROP DATABASE
menu:
  latest:
    identifier: api-postgresql-drop-db
    parent: api-postgresql-ddl
aliases:
  - /latest/api/postgresql/ddl_drop_database
  - /latest/api/ysql/ddl_drop_database
---

## Synopsis
The `DROP DATABASE` statement removes a database and all its database objects (such as [tables](../ddl_create_table) or [types](../ddl_create_type)) from the system.

## Syntax

### Diagram

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="221" height="35" viewbox="0 0 221 35"><path class="connector" d="M0 22h5m53 0h10m84 0h10m54 0h5"/><rect class="literal" x="5" y="5" width="53" height="25" rx="7"/><text class="text" x="15" y="22">DROP</text><rect class="literal" x="68" y="5" width="84" height="25" rx="7"/><text class="text" x="78" y="22">DATABASE</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="162" y="5" width="54" height="25"/><text class="text" x="172" y="22">name</text></a></svg>

### Grammar

```
drop_database ::= DROP DATABASE name;
```
Where

- `name` is an identifier.

## Semantics

- An error is raised if the specified `name` does not exist.
- Currently, an error is raised if the specified database is non-empty (contains tables or types). This restriction will be removed.

## See Also
[`CREATE DATABASE`](../ddl_create_database)
[Other PostgreSQL Statements](..)
