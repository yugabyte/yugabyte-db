---
title: INSERT
summary: Add a new row to a table
description: INSERT
menu:
  v1.0:
    identifier: api-postgresql-insert
    parent: api-postgresql-dml
---

## Synopsis

The `INSERT` statement adds a row to a specified table.

## Syntax
### Diagram

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="710" height="80" viewbox="0 0 710 80"><path class="connector" d="M0 52h5m65 0h10m50 0h10m91 0h30m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h25m24 0h25q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h20m-209 0q5 0 5 5v8q0 5 5 5h184q5 0 5-5v-8q0-5 5-5m5 0h10m68 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h19m24 0h19q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h5"/><rect class="literal" x="5" y="35" width="65" height="25" rx="7"/><text class="text" x="15" y="52">INSERT</text><rect class="literal" x="80" y="35" width="50" height="25" rx="7"/><text class="text" x="90" y="52">INTO</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="140" y="35" width="91" height="25"/><text class="text" x="150" y="52">table_name</text></a><rect class="literal" x="261" y="35" width="25" height="25" rx="7"/><text class="text" x="271" y="52">(</text><rect class="literal" x="336" y="5" width="24" height="25" rx="7"/><text class="text" x="346" y="22">,</text><a xlink:href="../grammar_diagrams#column"><rect class="rule" x="316" y="35" width="64" height="25"/><text class="text" x="326" y="52">column</text></a><rect class="literal" x="410" y="35" width="25" height="25" rx="7"/><text class="text" x="420" y="52">)</text><rect class="literal" x="465" y="35" width="68" height="25" rx="7"/><text class="text" x="475" y="52">VALUES</text><rect class="literal" x="543" y="35" width="25" height="25" rx="7"/><text class="text" x="553" y="52">(</text><rect class="literal" x="612" y="5" width="24" height="25" rx="7"/><text class="text" x="622" y="22">,</text><a xlink:href="../grammar_diagrams#value"><rect class="rule" x="598" y="35" width="52" height="25"/><text class="text" x="608" y="52">value</text></a><rect class="literal" x="680" y="35" width="25" height="25" rx="7"/><text class="text" x="690" y="52">)</text></svg>

### Grammar

```
insert ::= INSERT INTO table_name ['(' column [ ',' column ... ] ')']
               VALUES '(' value [ ',' value ... ] ')';
```

Where

- `table_name` and `column` are identifiers.
- `value` can be any expression.

## Semantics
 - An error is raised if the specified `table_name` does not exist. 
 - Each of primary key columns must have a non-null value.

### `VALUES` Clause
 - The values list must have the same length as the columns list.
 - Each value must be convertible to its corresponding (by position) column type.
 - Each value literal can be an expression.

## See Also

[`CREATE TABLE`](../ddl_create_table)
[`DELETE`](../dml_delete)
[`SELECT`](../dml_select)
[`UPDATE`](../dml_update)
[Other PostgreSQL Statements](..)
