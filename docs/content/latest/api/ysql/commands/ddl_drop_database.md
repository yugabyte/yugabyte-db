---
title: DROP DATABASE
summary: Removes a database and all of its database objects
description: DROP DATABASE
menu:
  latest:
    identifier: api-ysql-commands-drop-db
    parent: api-ysql-commands-drop-db
aliases:
  - /latest/api/ysql/commands/ddl_drop_database
isTocNested: true
showAsideToc: true
---

## Synopsis
The `DROP DATABASE` statement removes a database and all its database objects (such as [tables](ddl_create_table) or [types](ddl_create_type)) from the system.

## Syntax

### Diagrams
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="440" height="49" viewbox="0 0 440 49"><path class="connector" d="M0 21h5m54 0h10m84 0h30m30 0h10m61 0h20m-136 0q5 0 5 5v8q0 5 5 5h111q5 0 5-5v-8q0-5 5-5m5 0h10m121 0h5"/><rect class="literal" x="5" y="5" width="54" height="24" rx="7"/><text class="text" x="15" y="21">DROP</text><rect class="literal" x="69" y="5" width="84" height="24" rx="7"/><text class="text" x="79" y="21">DATABASE</text><rect class="literal" x="183" y="5" width="30" height="24" rx="7"/><text class="text" x="193" y="21">IF</text><rect class="literal" x="223" y="5" width="61" height="24" rx="7"/><text class="text" x="233" y="21">EXISTS</text><a xlink:href="../grammar_diagrams#database-name"><rect class="rule" x="314" y="5" width="121" height="24"/><text class="text" x="324" y="21">database_name</text></a></svg>

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
[`CREATE DATABASE`](ddl_create_database)
[Other YSQL Statements](..)
