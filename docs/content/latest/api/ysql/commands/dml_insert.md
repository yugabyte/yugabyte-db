---
title: INSERT
summary: Add new rows to a table
description: INSERT
menu:
  latest:
    identifier: api-ysql-commands-insert
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/dml_insert/
  - /latest/api/postgresql/dml_insert/
isTocNested: true
showAsideToc: true
---

## Synopsis

The `INSERT` command adds one or more rows to the specified table.

## Syntax

### Diagrams

#### insert

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="613" height="313" viewbox="0 0 613 313"><path class="connector" d="M0 50h25m50 0h30m88 0h20m-123 0q5 0 5 5v8q0 5 5 5h98q5 0 5-5v-8q0-5 5-5m5 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h36m24 0h36q5 0 5 5v19q0 5-5 5m-5 0h40m-359 0q5 0 5 5v23q0 5 5 5h334q5 0 5-5v-23q0-5 5-5m5 0h5m-374 64h5m63 0h10m48 0h10m93 0h30m36 0h10m49 0h20m-130 0q5 0 5 5v8q0 5 5 5h105q5 0 5-5v-8q0-5 5-5m5 0h30m25 0h10m113 0h10m25 0h20m-218 0q5 0 5 5v8q0 5 5 5h193q5 0 5-5v-8q0-5 5-5m5 0h5m-612 49h25m74 0h10m67 0h432m-593 39q0 5 5 5h5m67 0h10m25 0h10m111 0h10m25 0h50m-5 0q-5 0-5-5v-16q0-5 5-5h225q5 0 5 5v16q0 5-5 5m-196 0h10m25 0h10m111 0h10m25 0h40m-290 0q5 0 5 5v8q0 5 5 5h265q5 0 5-5v-8q0-5 5-5m5 0h5q5 0 5-5m-588-39q5 0 5 5v78q0 5 5 5h5m78 0h490q5 0 5-5v-78q0-5 5-5m5 0h5m-613 122h25m125 0h20m-160 0q5 0 5 5v8q0 5 5 5h135q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="34" width="50" height="24" rx="7"/><text class="text" x="35" y="50">WITH</text><rect class="literal" x="105" y="34" width="88" height="24" rx="7"/><text class="text" x="115" y="50">RECURSIVE</text><rect class="literal" x="274" y="5" width="24" height="24" rx="7"/><text class="text" x="284" y="21">,</text><a xlink:href="../grammar_diagrams#with-query"><rect class="rule" x="243" y="34" width="86" height="24"/><text class="text" x="253" y="50">with_query</text></a><rect class="literal" x="5" y="98" width="63" height="24" rx="7"/><text class="text" x="15" y="114">INSERT</text><rect class="literal" x="78" y="98" width="48" height="24" rx="7"/><text class="text" x="88" y="114">INTO</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="136" y="98" width="93" height="24"/><text class="text" x="146" y="114">table_name</text></a><rect class="literal" x="259" y="98" width="36" height="24" rx="7"/><text class="text" x="269" y="114">AS</text><a xlink:href="../grammar_diagrams#alias"><rect class="rule" x="305" y="98" width="49" height="24"/><text class="text" x="315" y="114">alias</text></a><rect class="literal" x="404" y="98" width="25" height="24" rx="7"/><text class="text" x="414" y="114">(</text><a xlink:href="../grammar_diagrams#column-names"><rect class="rule" x="439" y="98" width="113" height="24"/><text class="text" x="449" y="114">column_names</text></a><rect class="literal" x="562" y="98" width="25" height="24" rx="7"/><text class="text" x="572" y="114">)</text><rect class="literal" x="25" y="147" width="74" height="24" rx="7"/><text class="text" x="35" y="163">DEFAULT</text><rect class="literal" x="109" y="147" width="67" height="24" rx="7"/><text class="text" x="119" y="163">VALUES</text><rect class="literal" x="25" y="191" width="67" height="24" rx="7"/><text class="text" x="35" y="207">VALUES</text><rect class="literal" x="102" y="191" width="25" height="24" rx="7"/><text class="text" x="112" y="207">(</text><a xlink:href="../grammar_diagrams#column-values"><rect class="rule" x="137" y="191" width="111" height="24"/><text class="text" x="147" y="207">column_values</text></a><rect class="literal" x="258" y="191" width="25" height="24" rx="7"/><text class="text" x="268" y="207">)</text><rect class="literal" x="333" y="191" width="24" height="24" rx="7"/><text class="text" x="343" y="207">,</text><rect class="literal" x="367" y="191" width="25" height="24" rx="7"/><text class="text" x="377" y="207">(</text><a xlink:href="../grammar_diagrams#column-values"><rect class="rule" x="402" y="191" width="111" height="24"/><text class="text" x="412" y="207">column_values</text></a><rect class="literal" x="523" y="191" width="25" height="24" rx="7"/><text class="text" x="533" y="207">)</text><a xlink:href="../grammar_diagrams#subquery"><rect class="rule" x="25" y="235" width="78" height="24"/><text class="text" x="35" y="251">subquery</text></a><a xlink:href="../grammar_diagrams#returning-clause"><rect class="rule" x="25" y="269" width="125" height="24"/><text class="text" x="35" y="285">returning_clause</text></a></svg>

#### returning_clause
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="565" height="122" viewbox="0 0 565 122"><path class="connector" d="M0 21h5m90 0h30m26 0h409m-450 0q5 0 5 5v48q0 5 5 5h25m-5 0q-5 0-5-5v-19q0-5 5-5h180m24 0h181q5 0 5 5v19q0 5-5 5m-244 0h50m36 0h20m-71 0q5 0 5 5v8q0 5 5 5h46q5 0 5-5v-8q0-5 5-5m5 0h10m103 0h20m-224 0q5 0 5 5v23q0 5 5 5h199q5 0 5-5v-23q0-5 5-5m5 0h25q5 0 5-5v-48q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="90" height="24" rx="7"/><text class="text" x="15" y="21">RETURNING</text><rect class="literal" x="125" y="5" width="26" height="24" rx="7"/><text class="text" x="135" y="21">*</text><rect class="literal" x="320" y="34" width="24" height="24" rx="7"/><text class="text" x="330" y="50">,</text><a xlink:href="../grammar_diagrams#output-expression"><rect class="rule" x="145" y="63" width="136" height="24"/><text class="text" x="155" y="79">output_expression</text></a><rect class="literal" x="331" y="63" width="36" height="24" rx="7"/><text class="text" x="341" y="79">AS</text><a xlink:href="../grammar_diagrams#output-name"><rect class="rule" x="397" y="63" width="103" height="24"/><text class="text" x="407" y="79">output_name</text></a></svg>

#### column_values

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="178" height="92" viewbox="0 0 178 92"><path class="connector" d="M0 50h25m-5 0q-5 0-5-5v-19q0-5 5-5h57m24 0h57q5 0 5 5v19q0 5-5 5m-133 0h20m88 0h20m-123 0q5 0 5 5v19q0 5 5 5h5m74 0h19q5 0 5-5v-19q0-5 5-5m5 0h25"/><rect class="literal" x="77" y="5" width="24" height="24" rx="7"/><text class="text" x="87" y="21">,</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="45" y="34" width="88" height="24"/><text class="text" x="55" y="50">expression</text></a><rect class="literal" x="45" y="63" width="74" height="24" rx="7"/><text class="text" x="55" y="79">DEFAULT</text></svg>

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
[Other YSQL Statements](..)
