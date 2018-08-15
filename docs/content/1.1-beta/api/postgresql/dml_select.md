---
title: SELECT
summary: Retrieves rows from a table
description: SELECT
menu:
  1.1-beta:
    identifier: api-postgresql-select
    parent: api-postgresql-dml
aliases:
  - api/postgresql/dml/select
  - api/pgsql/dml/select
---

## Synopsis
The `SELECT` statement retrieves (part of) rows of specified columns that meet a given condition from a table. It specifies the columns to be retrieved, the name of the table, and the condition each selected row must satisfy.

## Syntax

### Diagram

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="818" height="95" viewbox="0 0 818 95"><path class="connector" d="M0 22h5m66 0h30m78 0h20m-113 0q5 0 5 5v8q0 5 5 5h88q5 0 5-5v-8q0-5 5-5m5 0h30m28 0h138m-181 0q5 0 5 5v50q0 5 5 5h25m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h25q5 0 5-5v-50q0-5 5-5m5 0h10m54 0h10m91 0h30m65 0h10m128 0h20m-238 0q5 0 5 5v8q0 5 5 5h213q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="66" height="25" rx="7"/><text class="text" x="15" y="22">SELECT</text><rect class="literal" x="101" y="5" width="78" height="25" rx="7"/><text class="text" x="111" y="22">DISTINCT</text><rect class="literal" x="229" y="5" width="28" height="25" rx="7"/><text class="text" x="239" y="22">*</text><rect class="literal" x="290" y="35" width="24" height="25" rx="7"/><text class="text" x="300" y="52">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="249" y="65" width="106" height="25"/><text class="text" x="259" y="82">column_name</text></a><rect class="literal" x="405" y="5" width="54" height="25" rx="7"/><text class="text" x="415" y="22">FROM</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="469" y="5" width="91" height="25"/><text class="text" x="479" y="22">table_name</text></a><rect class="literal" x="590" y="5" width="65" height="25" rx="7"/><text class="text" x="600" y="22">WHERE</text><a xlink:href="../grammar_diagrams#where-expression"><rect class="rule" x="665" y="5" width="128" height="25"/><text class="text" x="675" y="22">where_expression</text></a></svg>

### Grammar
```
select ::= SELECT [ DISTINCT ] { '*' | column_name [ ',' column_name ... ] } 
               FROM table_name [ WHERE where_expression ];
```

Where

- `table_name` and `column_name` are identifiers.

## Semantics
 - An error is raised if the specified `table_name` does not exist.
 - `*` represents all columns.

### `WHERE` Clause
 - The `where_expression` must evaluate to boolean values.
 - The `where_expression` can specify conditions for any column. 

While the where clause allows a wide range of operators, the exact conditions used in the where clause have significant performance considerations (especially for large datasets).

## See Also

[`CREATE TABLE`](../ddl_create_table)
[`INSERT`](../dml_insert)
[`UPDATE`](../dml_update)
[`DELETE`](../dml_delete)
[Other PostgreSQL Statements](..)
