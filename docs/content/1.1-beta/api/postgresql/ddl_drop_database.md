---
title: DROP DATABASE
summary: Removes a database and all of its database objects
description: DROP DATABASE
menu:
  1.1-beta:
    identifier: api-postgresql-drop-db
    parent: api-postgresql-ddl
aliases:
  - api/postgresql/ddl_drop_database
  - api/pgsql/ddl_drop_database
---

## Synopsis
The `DROP DATABASE` statement removes a database and all its database objects (such as [tables](../ddl_create_table) or [types](../ddl_create_type)) from the system.

## Syntax

### Diagram

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="438" height="50" viewbox="0 0 438 50"><path class="connector" d="M0 22h5m53 0h10m84 0h30m32 0h10m64 0h20m-141 0q5 0 5 5v8q0 5 5 5h116q5 0 5-5v-8q0-5 5-5m5 0h10m115 0h5"/><rect class="literal" x="5" y="5" width="53" height="25" rx="7"/><text class="text" x="15" y="22">DROP</text><rect class="literal" x="68" y="5" width="84" height="25" rx="7"/><text class="text" x="78" y="22">DATABASE</text><rect class="literal" x="182" y="5" width="32" height="25" rx="7"/><text class="text" x="192" y="22">IF</text><rect class="literal" x="224" y="5" width="64" height="25" rx="7"/><text class="text" x="234" y="22">EXISTS</text><a xlink:href="../grammar_diagrams#database-name"><rect class="rule" x="318" y="5" width="115" height="25"/><text class="text" x="328" y="22">database_name</text></a></svg>

### Grammar

```
drop_database ::= DROP DATABASE [ IF EXISTS ] database_name;
```
Where

- `database_name` is an identifier.

## Semantics

- An error is raised if the specified `database_name` does not exist unless `IF EXISTS` option is present.
- Currently, an error is raised if the specified database is non-empty (contains tables or types). This restriction will be removed.

## See Also
[`CREATE DATABASE`](../ddl_create_database)
[Other PostgreSQL Statements](..)
