---
title: DROP TABLE
summary: Remove a table
description: DROP TABLE
menu:
  v1.1:
    identifier: api-postgresql-drop-table
    parent: api-postgresql-ddl
isTocNested: true
showAsideToc: true
---

## Synopsis
The `DROP TABLE` statement removes a table and all of its data from the database.

## Syntax

### Diagram

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="252" height="35" viewbox="0 0 252 35"><path class="connector" d="M0 22h5m53 0h10m58 0h10m111 0h5"/><rect class="literal" x="5" y="5" width="53" height="25" rx="7"/><text class="text" x="15" y="22">DROP</text><rect class="literal" x="68" y="5" width="58" height="25" rx="7"/><text class="text" x="78" y="22">TABLE</text><a xlink:href="../grammar_diagrams#qualified-name"><rect class="rule" x="136" y="5" width="111" height="25"/><text class="text" x="146" y="22">qualified_name</text></a></svg>

### Grammar
```
drop_table ::= DROP TABLE qualified_name;
```
Where

- `qualified_name` is a (possibly qualified) identifier.

## Semantics

 - An error is raised if the specified `table_name` does not exist.
 - Associated objects to `table_name` such as prepared statements will be eventually invalidated after the drop statement is completed.

## See Also

[`CREATE TABLE`](../ddl_create_table)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[Other PostgreSQL Statements](..)
