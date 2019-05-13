---
title: DROP TABLE
summary: Remove a table
description: DROP TABLE
menu:
  latest:
    identifier: api-ysql-commands-drop-table
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/ddl_drop_table/
isTocNested: true
showAsideToc: true
---

## Synopsis
The `DROP TABLE` command removes a table and all of its data from the database.

## Syntax

### Diagrams

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="385" height="49" viewbox="0 0 385 49"><path class="connector" d="M0 21h5m54 0h10m57 0h30m30 0h10m61 0h20m-136 0q5 0 5 5v8q0 5 5 5h111q5 0 5-5v-8q0-5 5-5m5 0h10m93 0h5"/><rect class="literal" x="5" y="5" width="54" height="24" rx="7"/><text class="text" x="15" y="21">DROP</text><rect class="literal" x="69" y="5" width="57" height="24" rx="7"/><text class="text" x="79" y="21">TABLE</text><rect class="literal" x="156" y="5" width="30" height="24" rx="7"/><text class="text" x="166" y="21">IF</text><rect class="literal" x="196" y="5" width="61" height="24" rx="7"/><text class="text" x="206" y="21">EXISTS</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="287" y="5" width="93" height="24"/><text class="text" x="297" y="21">table_name</text></a></svg>

### Grammar
```
drop_table ::= DROP TABLE [ IF EXISTS ] qualified_name;
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
[Other YSQL Statements](..)
