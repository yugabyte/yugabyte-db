---
title: INSERT
summary: Add a new row to a table
description: INSERT
menu:
  latest:
    identifier: api-ysql-commands-insert
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/dml/commands/insert
isTocNested: true
showAsideToc: true
---

## Synopsis

The `INSERT` statement adds a row to a specified table.

## Syntax

### Diagram

#### insert

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="1439" height="150" viewbox="0 0 1439 150"><path class="connector" d="M0 50h25m50 0h30m88 0h20m-123 0q5 0 5 5v8q0 5 5 5h98q5 0 5-5v-8q0-5 5-5m5 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h36m24 0h36q5 0 5 5v19q0 5-5 5m-5 0h40m-359 0q5 0 5 5v23q0 5 5 5h334q5 0 5-5v-23q0-5 5-5m5 0h10m63 0h10m48 0h10m93 0h30m36 0h10m49 0h20m-130 0q5 0 5 5v8q0 5 5 5h105q5 0 5-5v-8q0-5 5-5m5 0h30m25 0h10m113 0h10m25 0h20m-218 0q5 0 5 5v8q0 5 5 5h193q5 0 5-5v-8q0-5 5-5m5 0h30m74 0h10m67 0h97m-258 53q0 5 5 5h5m67 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h48m24 0h49q5 0 5 5v19q0 5-5 5m-5 0h25q5 0 5-5m-253-53q5 0 5 5v77q0 5 5 5h5m78 0h155q5 0 5-5v-77q0-5 5-5m5 0h30m125 0h20m-160 0q5 0 5 5v8q0 5 5 5h135q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="34" width="50" height="24" rx="7"/><text class="text" x="35" y="50">WITH</text><rect class="literal" x="105" y="34" width="88" height="24" rx="7"/><text class="text" x="115" y="50">RECURSIVE</text><rect class="literal" x="274" y="5" width="24" height="24" rx="7"/><text class="text" x="284" y="21">,</text><a xlink:href="../grammar_diagrams#with-query"><rect class="rule" x="243" y="34" width="86" height="24"/><text class="text" x="253" y="50">with_query</text></a><rect class="literal" x="379" y="34" width="63" height="24" rx="7"/><text class="text" x="389" y="50">INSERT</text><rect class="literal" x="452" y="34" width="48" height="24" rx="7"/><text class="text" x="462" y="50">INTO</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="510" y="34" width="93" height="24"/><text class="text" x="520" y="50">table_name</text></a><rect class="literal" x="633" y="34" width="36" height="24" rx="7"/><text class="text" x="643" y="50">AS</text><a xlink:href="../grammar_diagrams#alias"><rect class="rule" x="679" y="34" width="49" height="24"/><text class="text" x="689" y="50">alias</text></a><rect class="literal" x="778" y="34" width="25" height="24" rx="7"/><text class="text" x="788" y="50">(</text><a xlink:href="../grammar_diagrams#column-names"><rect class="rule" x="813" y="34" width="113" height="24"/><text class="text" x="823" y="50">column_names</text></a><rect class="literal" x="936" y="34" width="25" height="24" rx="7"/><text class="text" x="946" y="50">)</text><rect class="literal" x="1011" y="34" width="74" height="24" rx="7"/><text class="text" x="1021" y="50">DEFAULT</text><rect class="literal" x="1095" y="34" width="67" height="24" rx="7"/><text class="text" x="1105" y="50">VALUES</text><rect class="literal" x="1011" y="92" width="67" height="24" rx="7"/><text class="text" x="1021" y="108">VALUES</text><rect class="literal" x="1151" y="63" width="24" height="24" rx="7"/><text class="text" x="1161" y="79">,</text><a xlink:href="../grammar_diagrams#column-values"><rect class="rule" x="1108" y="92" width="111" height="24"/><text class="text" x="1118" y="108">column_values</text></a><a xlink:href="../grammar_diagrams#subquery"><rect class="rule" x="1011" y="121" width="78" height="24"/><text class="text" x="1021" y="137">subquery</text></a><a xlink:href="../grammar_diagrams#returning-clause"><rect class="rule" x="1289" y="34" width="125" height="24"/><text class="text" x="1299" y="50">returning_clause</text></a></svg>

#### column_values

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="224" height="63" viewbox="0 0 224 63"><path class="connector" d="M0 50h5m25 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h45m24 0h45q5 0 5 5v19q0 5-5 5m-5 0h30m25 0h5"/><rect class="literal" x="5" y="34" width="25" height="24" rx="7"/><text class="text" x="15" y="50">(</text><rect class="literal" x="100" y="5" width="24" height="24" rx="7"/><text class="text" x="110" y="21">,</text><a xlink:href="../grammar_diagrams#column-value"><rect class="rule" x="60" y="34" width="104" height="24"/><text class="text" x="70" y="50">column_value</text></a><rect class="literal" x="194" y="34" width="25" height="24" rx="7"/><text class="text" x="204" y="50">)</text></svg>

#### column_value

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="138" height="63" viewbox="0 0 138 63"><path class="connector" d="M0 21h25m88 0h20m-123 0q5 0 5 5v19q0 5 5 5h5m74 0h19q5 0 5-5v-19q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="25" y="5" width="88" height="24"/><text class="text" x="35" y="21">expression</text></a><rect class="literal" x="25" y="34" width="74" height="24" rx="7"/><text class="text" x="35" y="50">DEFAULT</text></svg>

### Grammar

```
insert = INSERT INTO qualified_name [ AS name ] '(' column_list ')' VALUES values_list [ ',' ...];
values_list = '(' expression [ ',' ...] ')';
```

Where

- `qualified_name` and `name` are identifiers.
- `column_list` is a comma-separated list of columns names (identifiers).

## Semantics
 - An error is raised if the specified table does not exist. 
 - Each of primary key columns must have a non-null value.

### `VALUES` Clause
 - Each of the values list must have the same length as the columns list.
 - Each value must be convertible to its corresponding (by position) column type.
 - Each value literal can be an expression.

## Examples

Create a sample table.

```sql
postgres=# CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```


Insert some rows.

```sql
postgres=# INSERT INTO sample VALUES (1, 2.0, 3, 'a'), (2, 3.0, 4, 'b'), (3, 4.0, 5, 'c');
```


Check the inserted rows.


```sql
postgres=# SELECT * FROM sample ORDER BY k1;
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  2 |  3 |  4 | b
  3 |  4 |  5 | c
(3 rows)
```

## See Also

[`CREATE TABLE`](../ddl_create_table)
[`SELECT`](../dml_select)
[Other PostgreSQL Statements](..)
