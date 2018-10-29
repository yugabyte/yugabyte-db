---
title: DELETE
summary: Deletes rows from a table.
description: DELETE
menu:
  v1.0:
    identifier: api-postgresql-delete
    parent: api-postgresql-dml
---

## Synopsis
The `DELETE` statement removes rows from a specified table that meet a given condition. Deleting multiple rows is not yet supported.

## Syntax
### Diagram

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="455" height="35" viewbox="0 0 455 35"><path class="connector" d="M0 22h5m67 0h10m54 0h10m91 0h10m65 0h10m128 0h5"/><rect class="literal" x="5" y="5" width="67" height="25" rx="7"/><text class="text" x="15" y="22">DELETE</text><rect class="literal" x="82" y="5" width="54" height="25" rx="7"/><text class="text" x="92" y="22">FROM</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="146" y="5" width="91" height="25"/><text class="text" x="156" y="22">table_name</text></a><rect class="literal" x="247" y="5" width="65" height="25" rx="7"/><text class="text" x="257" y="22">WHERE</text><a xlink:href="../grammar_diagrams#where-expression"><rect class="rule" x="322" y="5" width="128" height="25"/><text class="text" x="332" y="22">where_expression</text></a></svg>

### Grammar
```
delete ::= DELETE FROM table_name WHERE where_expression;
```
Where

- `table_name` is an identifier.

## Semantics

 - An error is raised if the specified `table_name` does not exist.
 - The `where_expression` must evaluate to [boolean](../type_bool) values.

### WHERE Clause

 - The `where_expression` should specify conditions for all primary-key columns for fast read.
 
## See Also

[`CREATE TABLE`](../ddl_create_table)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[`UPDATE`](../dml_update)
[Other PostgreSQL Statements](..)
