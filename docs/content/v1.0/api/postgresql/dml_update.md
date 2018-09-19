---
title: UPDATE
summary: Change values of a row in a table
description: UPDATE
menu:
  v1.0:
    identifier: api-postgresql-update
    parent: api-postgresql-dml
aliases:
  - api/postgresql/dml/update
  - api/pgsql/dml/update
---

## Synopsis

The `UPDATE` statement updates one or more column values for a row in table.

## Syntax

### Diagram

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="584" height="65" viewbox="0 0 584 65"><path class="connector" d="M0 52h5m68 0h10m91 0h10m43 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h37m24 0h38q5 0 5 5v20q0 5-5 5m-5 0h30m65 0h10m128 0h5"/><rect class="literal" x="5" y="35" width="68" height="25" rx="7"/><text class="text" x="15" y="52">UPDATE</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="83" y="35" width="91" height="25"/><text class="text" x="93" y="52">table_name</text></a><rect class="literal" x="184" y="35" width="43" height="25" rx="7"/><text class="text" x="194" y="52">SET</text><rect class="literal" x="289" y="5" width="24" height="25" rx="7"/><text class="text" x="299" y="22">,</text><a xlink:href="../grammar_diagrams#assignment"><rect class="rule" x="257" y="35" width="89" height="25"/><text class="text" x="267" y="52">assignment</text></a><rect class="literal" x="376" y="35" width="65" height="25" rx="7"/><text class="text" x="386" y="52">WHERE</text><a xlink:href="../grammar_diagrams#where-expression"><rect class="rule" x="451" y="35" width="128" height="25"/><text class="text" x="461" y="52">where_expression</text></a></svg>

#### assignment
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="492" height="65" viewbox="0 0 492 65"><path class="connector" d="M0 22h25m106 0h223m-344 0q5 0 5 5v20q0 5 5 5h5m106 0h10m25 0h10m123 0h10m25 0h5q5 0 5-5v-20q0-5 5-5m5 0h10m30 0h10m83 0h5"/><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="25" y="5" width="106" height="25"/><text class="text" x="35" y="22">column_name</text></a><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="25" y="35" width="106" height="25"/><text class="text" x="35" y="52">column_name</text></a><rect class="literal" x="141" y="35" width="25" height="25" rx="7"/><text class="text" x="151" y="52">[</text><a xlink:href="../grammar_diagrams#index-expression"><rect class="rule" x="176" y="35" width="123" height="25"/><text class="text" x="186" y="52">index_expression</text></a><rect class="literal" x="309" y="35" width="25" height="25" rx="7"/><text class="text" x="319" y="52">]</text><rect class="literal" x="364" y="5" width="30" height="25" rx="7"/><text class="text" x="374" y="22">=</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="404" y="5" width="83" height="25"/><text class="text" x="414" y="22">expression</text></a></svg>

### Grammar
```
update ::= UPDATE table_name SET assignment [',' assignment ...] WHERE where_expression;

assignment ::= { column_name | column_name'['index_expression']' } '=' expression
```

Where

- `table_name` is an identifier.

## Semantics
- An error is raised if the specified `table_name` does not exist.
- The `where_expression` must evaluate to boolean values.

## See Also

[`CREATE TABLE`](../ddl_create_table)
[`DELETE`](../dml_delete)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[Other PostgreSQL Statements](..)
