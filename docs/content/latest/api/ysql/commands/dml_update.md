---
title: UPDATE
linkTitle: UPDATE
summary: Update table data
description: UPDATE
menu:
  latest:
    identifier: api-ysql-commands-update
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/dml_update
isTocNested: true
showAsideToc: true
---

## Synopsis

UPDATE modifies the values of specified columns in all rows that meet certain conditions, and when conditions are not provided in WHERE clause, all rows are updated. UPDATE outputs the number of rows that are being updated.

## Syntax

### Diagrams

#### update

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="544" height="303" viewbox="0 0 544 303"><path class="connector" d="M0 50h25m50 0h30m88 0h20m-123 0q5 0 5 5v8q0 5 5 5h98q5 0 5-5v-8q0-5 5-5m5 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h36m24 0h36q5 0 5 5v19q0 5-5 5m-5 0h40m-359 0q5 0 5 5v23q0 5 5 5h334q5 0 5-5v-23q0-5 5-5m5 0h5m-374 64h5m69 0h30m51 0h20m-86 0q5 0 5 5v8q0 5 5 5h61q5 0 5-5v-8q0-5 5-5m5 0h10m93 0h30m26 0h20m-61 0q5 0 5 5v8q0 5 5 5h36q5 0 5-5v-8q0-5 5-5m5 0h50m36 0h20m-71 0q5 0 5 5v8q0 5 5 5h46q5 0 5-5v-8q0-5 5-5m5 0h10m49 0h20m-170 0q5 0 5 5v23q0 5 5 5h145q5 0 5-5v-23q0-5 5-5m5 0h5m-544 93h5m43 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h42m24 0h42q5 0 5 5v19q0 5-5 5m-5 0h50m54 0h10m72 0h20m-171 0q5 0 5 5v8q0 5 5 5h146q5 0 5-5v-8q0-5 5-5m5 0h5m-387 49h25m64 0h10m78 0h176m-338 24q0 5 5 5h5m64 0h10m77 0h10m36 0h10m101 0h5q5 0 5-5m-333-24q5 0 5 5v32q0 5 5 5h318q5 0 5-5v-32q0-5 5-5m5 0h30m125 0h20m-160 0q5 0 5 5v8q0 5 5 5h135q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="34" width="50" height="24" rx="7"/><text class="text" x="35" y="50">WITH</text><rect class="literal" x="105" y="34" width="88" height="24" rx="7"/><text class="text" x="115" y="50">RECURSIVE</text><rect class="literal" x="274" y="5" width="24" height="24" rx="7"/><text class="text" x="284" y="21">,</text><a xlink:href="../grammar_diagrams#with-query"><rect class="rule" x="243" y="34" width="86" height="24"/><text class="text" x="253" y="50">with_query</text></a><rect class="literal" x="5" y="98" width="69" height="24" rx="7"/><text class="text" x="15" y="114">UPDATE</text><rect class="literal" x="104" y="98" width="51" height="24" rx="7"/><text class="text" x="114" y="114">ONLY</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="185" y="98" width="93" height="24"/><text class="text" x="195" y="114">table_name</text></a><rect class="literal" x="308" y="98" width="26" height="24" rx="7"/><text class="text" x="318" y="114">*</text><rect class="literal" x="404" y="98" width="36" height="24" rx="7"/><text class="text" x="414" y="114">AS</text><a xlink:href="../grammar_diagrams#alias"><rect class="rule" x="470" y="98" width="49" height="24"/><text class="text" x="480" y="114">alias</text></a><rect class="literal" x="5" y="191" width="43" height="24" rx="7"/><text class="text" x="15" y="207">SET</text><rect class="literal" x="115" y="162" width="24" height="24" rx="7"/><text class="text" x="125" y="178">,</text><a xlink:href="../grammar_diagrams#update-item"><rect class="rule" x="78" y="191" width="98" height="24"/><text class="text" x="88" y="207">update_item</text></a><rect class="literal" x="226" y="191" width="54" height="24" rx="7"/><text class="text" x="236" y="207">FROM</text><a xlink:href="../grammar_diagrams#from-list"><rect class="rule" x="290" y="191" width="72" height="24"/><text class="text" x="300" y="207">from_list</text></a><rect class="literal" x="25" y="240" width="64" height="24" rx="7"/><text class="text" x="35" y="256">WHERE</text><a xlink:href="../grammar_diagrams#condition"><rect class="rule" x="99" y="240" width="78" height="24"/><text class="text" x="109" y="256">condition</text></a><rect class="literal" x="25" y="269" width="64" height="24" rx="7"/><text class="text" x="35" y="285">WHERE</text><rect class="literal" x="99" y="269" width="77" height="24" rx="7"/><text class="text" x="109" y="285">CURRENT</text><rect class="literal" x="186" y="269" width="36" height="24" rx="7"/><text class="text" x="196" y="285">OF</text><a xlink:href="../grammar_diagrams#cursor-name"><rect class="rule" x="232" y="269" width="101" height="24"/><text class="text" x="242" y="285">cursor_name</text></a><a xlink:href="../grammar_diagrams#returning-clause"><rect class="rule" x="383" y="240" width="125" height="24"/><text class="text" x="393" y="256">returning_clause</text></a></svg>

#### returning_clause
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="565" height="122" viewbox="0 0 565 122"><path class="connector" d="M0 21h5m90 0h30m26 0h409m-450 0q5 0 5 5v48q0 5 5 5h25m-5 0q-5 0-5-5v-19q0-5 5-5h180m24 0h181q5 0 5 5v19q0 5-5 5m-244 0h50m36 0h20m-71 0q5 0 5 5v8q0 5 5 5h46q5 0 5-5v-8q0-5 5-5m5 0h10m103 0h20m-224 0q5 0 5 5v23q0 5 5 5h199q5 0 5-5v-23q0-5 5-5m5 0h25q5 0 5-5v-48q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="90" height="24" rx="7"/><text class="text" x="15" y="21">RETURNING</text><rect class="literal" x="125" y="5" width="26" height="24" rx="7"/><text class="text" x="135" y="21">*</text><rect class="literal" x="320" y="34" width="24" height="24" rx="7"/><text class="text" x="330" y="50">,</text><a xlink:href="../grammar_diagrams#output-expression"><rect class="rule" x="145" y="63" width="136" height="24"/><text class="text" x="155" y="79">output_expression</text></a><rect class="literal" x="331" y="63" width="36" height="24" rx="7"/><text class="text" x="341" y="79">AS</text><a xlink:href="../grammar_diagrams#output-name"><rect class="rule" x="397" y="63" width="103" height="24"/><text class="text" x="407" y="79">output_name</text></a></svg>

#### update_item
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="562" height="107" viewbox="0 0 562 107"><path class="connector" d="M0 21h25m106 0h10m30 0h10m104 0h272m-542 24q0 5 5 5h5m25 0h10m113 0h10m25 0h10m30 0h30m48 0h20m-83 0q5 0 5 5v8q0 5 5 5h58q5 0 5-5v-8q0-5 5-5m5 0h10m25 0h10m111 0h10m25 0h5q5 0 5-5m-537-24q5 0 5 5v63q0 5 5 5h5m25 0h10m113 0h10m25 0h10m30 0h10m25 0h10m55 0h10m25 0h159q5 0 5-5v-63q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="25" y="5" width="106" height="24"/><text class="text" x="35" y="21">column_name</text></a><rect class="literal" x="141" y="5" width="30" height="24" rx="7"/><text class="text" x="151" y="21">=</text><a xlink:href="../grammar_diagrams#column-value"><rect class="rule" x="181" y="5" width="104" height="24"/><text class="text" x="191" y="21">column_value</text></a><rect class="literal" x="25" y="34" width="25" height="24" rx="7"/><text class="text" x="35" y="50">(</text><a xlink:href="../grammar_diagrams#column-names"><rect class="rule" x="60" y="34" width="113" height="24"/><text class="text" x="70" y="50">column_names</text></a><rect class="literal" x="183" y="34" width="25" height="24" rx="7"/><text class="text" x="193" y="50">)</text><rect class="literal" x="218" y="34" width="30" height="24" rx="7"/><text class="text" x="228" y="50">=</text><rect class="literal" x="278" y="34" width="48" height="24" rx="7"/><text class="text" x="288" y="50">ROW</text><rect class="literal" x="356" y="34" width="25" height="24" rx="7"/><text class="text" x="366" y="50">(</text><a xlink:href="../grammar_diagrams#column-values"><rect class="rule" x="391" y="34" width="111" height="24"/><text class="text" x="401" y="50">column_values</text></a><rect class="literal" x="512" y="34" width="25" height="24" rx="7"/><text class="text" x="522" y="50">)</text><rect class="literal" x="25" y="78" width="25" height="24" rx="7"/><text class="text" x="35" y="94">(</text><a xlink:href="../grammar_diagrams#column-names"><rect class="rule" x="60" y="78" width="113" height="24"/><text class="text" x="70" y="94">column_names</text></a><rect class="literal" x="183" y="78" width="25" height="24" rx="7"/><text class="text" x="193" y="94">)</text><rect class="literal" x="218" y="78" width="30" height="24" rx="7"/><text class="text" x="228" y="94">=</text><rect class="literal" x="258" y="78" width="25" height="24" rx="7"/><text class="text" x="268" y="94">(</text><a xlink:href="../grammar_diagrams#query"><rect class="rule" x="293" y="78" width="55" height="24"/><text class="text" x="303" y="94">query</text></a><rect class="literal" x="358" y="78" width="25" height="24" rx="7"/><text class="text" x="368" y="94">)</text></svg>

#### column_values
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="178" height="92" viewbox="0 0 178 92"><path class="connector" d="M0 50h25m-5 0q-5 0-5-5v-19q0-5 5-5h57m24 0h57q5 0 5 5v19q0 5-5 5m-133 0h20m88 0h20m-123 0q5 0 5 5v19q0 5 5 5h5m74 0h19q5 0 5-5v-19q0-5 5-5m5 0h25"/><rect class="literal" x="77" y="5" width="24" height="24" rx="7"/><text class="text" x="87" y="21">,</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="45" y="34" width="88" height="24"/><text class="text" x="55" y="50">expression</text></a><rect class="literal" x="45" y="63" width="74" height="24" rx="7"/><text class="text" x="55" y="79">DEFAULT</text></svg>

#### column_value
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="138" height="63" viewbox="0 0 138 63"><path class="connector" d="M0 21h25m88 0h20m-123 0q5 0 5 5v19q0 5 5 5h5m74 0h19q5 0 5-5v-19q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="25" y="5" width="88" height="24"/><text class="text" x="35" y="21">expression</text></a><rect class="literal" x="25" y="34" width="74" height="24" rx="7"/><text class="text" x="35" y="50">DEFAULT</text></svg>

#### column_names
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="156" height="63" viewbox="0 0 156 63"><path class="connector" d="M0 50h25m-5 0q-5 0-5-5v-19q0-5 5-5h46m24 0h46q5 0 5 5v19q0 5-5 5m-5 0h25"/><rect class="literal" x="66" y="5" width="24" height="24" rx="7"/><text class="text" x="76" y="21">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="25" y="34" width="106" height="24"/><text class="text" x="35" y="50">column_name</text></a></svg>

### Grammar
```
update ::= [ WITH [ RECURSIVE ] with_query [, ...] ]
       UPDATE [ ONLY ] table_name [ * ] [ [ AS ] alias ]
       SET { column_name = { expression | DEFAULT } |
          ( column_name [, ...] ) = [ ROW ] ( { expression | DEFAULT } [, ...] ) |
          ( column_name [, ...] ) = ( subquery )
        } [, ...]
        [ FROM from_list ]
        [ WHERE condition | WHERE CURRENT OF cursor_name ]
        [ RETURNING * | output_expression [ [ AS ] output_name ] [, ...] ]
```

Where

- `with_query` specifies the subqueries that are referenced by name in the UPDATE statement.

- `table_name` specifies a name of the table to be updated.

- `alias` is the identifier of the target table within the UPDATE statement. When an alias is specified, it must be used in place of the actual table in the statement.

- `column_name` specifies a column in the table to be updated.

- `expression` specifies the value to be assigned a column. When the expression is referencing a column, the old value of this column is used to evaluate.

- `output_expression` specifies the value to be returned. When the `output_expression` is referencing a column, the new value of this column (updated value) is used to evaluate.

- `subquery` is a SELECT statement. Its selected values will be assigned to the specified columns.

## Semantics

- Updating columns that are part of an index key including PRIMARY KEY is not yet supported.

- While the where clause allows a wide range of operators, the exact conditions used in the where clause have significant performance considerations (especially for large datasets). WHERE clause that provides values for all columns in PRIMARY KEY or INDEX KEY has the best performance.

## Examples
Create a sample table, insert a few rows, then update the inserted rows.

```sql
postgres=# CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

```sql
postgres=# INSERT INTO sample VALUES (1, 2.0, 3, 'a'), (2, 3.0, 4, 'b'), (3, 4.0, 5, 'c');
```

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

```sql
postgres=# UPDATE sample SET v1 = v1 + 3, v2 = '7' WHERE k1 = 2 AND k2 = 3;
```

```
UPDATE 1
```

```sql
postgres=# SELECT * FROM sample ORDER BY k1;
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  2 |  3 |  7 | 7
  3 |  4 |  5 | c
(2 rows)
```

## See Also
[`DELETE`](../dml_delete)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[Other PostgreSQL Statements](..)
