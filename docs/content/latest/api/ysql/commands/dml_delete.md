---
title: DELETE
linkTitle: DELETE
summary: DELETE
description: DELETE
menu:
  latest:
    identifier: api-ysql-commands-delete
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/dml_delete
isTocNested: true
showAsideToc: true
---

## Synopsis

DELETE removes rows that meet certain conditions, and when conditions are not provided in WHERE clause, all rows are deleted. DELETE outputs the number of rows that are being deleted.

## Syntax

### Diagram 

#### delete
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="1512" height="97" viewbox="0 0 1512 97"><path class="connector" d="M0 50h25m50 0h30m88 0h20m-123 0q5 0 5 5v8q0 5 5 5h98q5 0 5-5v-8q0-5 5-5m5 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h36m24 0h36q5 0 5 5v19q0 5-5 5m-5 0h40m-359 0q5 0 5 5v23q0 5 5 5h334q5 0 5-5v-23q0-5 5-5m5 0h10m66 0h10m54 0h30m51 0h20m-86 0q5 0 5 5v8q0 5 5 5h61q5 0 5-5v-8q0-5 5-5m5 0h10m93 0h30m26 0h20m-61 0q5 0 5 5v8q0 5 5 5h36q5 0 5-5v-8q0-5 5-5m5 0h50m36 0h20m-71 0q5 0 5 5v8q0 5 5 5h46q5 0 5-5v-8q0-5 5-5m5 0h10m49 0h20m-170 0q5 0 5 5v23q0 5 5 5h145q5 0 5-5v-23q0-5 5-5m5 0h30m64 0h10m78 0h176m-338 24q0 5 5 5h5m64 0h10m77 0h10m36 0h10m101 0h5q5 0 5-5m-333-24q5 0 5 5v32q0 5 5 5h318q5 0 5-5v-32q0-5 5-5m5 0h30m125 0h20m-160 0q5 0 5 5v8q0 5 5 5h135q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="34" width="50" height="24" rx="7"/><text class="text" x="35" y="50">WITH</text><rect class="literal" x="105" y="34" width="88" height="24" rx="7"/><text class="text" x="115" y="50">RECURSIVE</text><rect class="literal" x="274" y="5" width="24" height="24" rx="7"/><text class="text" x="284" y="21">,</text><a xlink:href="../grammar_diagrams#with-query"><rect class="rule" x="243" y="34" width="86" height="24"/><text class="text" x="253" y="50">with_query</text></a><rect class="literal" x="379" y="34" width="66" height="24" rx="7"/><text class="text" x="389" y="50">DELETE</text><rect class="literal" x="455" y="34" width="54" height="24" rx="7"/><text class="text" x="465" y="50">FROM</text><rect class="literal" x="539" y="34" width="51" height="24" rx="7"/><text class="text" x="549" y="50">ONLY</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="620" y="34" width="93" height="24"/><text class="text" x="630" y="50">table_name</text></a><rect class="literal" x="743" y="34" width="26" height="24" rx="7"/><text class="text" x="753" y="50">*</text><rect class="literal" x="839" y="34" width="36" height="24" rx="7"/><text class="text" x="849" y="50">AS</text><a xlink:href="../grammar_diagrams#alias"><rect class="rule" x="905" y="34" width="49" height="24"/><text class="text" x="915" y="50">alias</text></a><rect class="literal" x="1004" y="34" width="64" height="24" rx="7"/><text class="text" x="1014" y="50">WHERE</text><a xlink:href="../grammar_diagrams#condition"><rect class="rule" x="1078" y="34" width="78" height="24"/><text class="text" x="1088" y="50">condition</text></a><rect class="literal" x="1004" y="63" width="64" height="24" rx="7"/><text class="text" x="1014" y="79">WHERE</text><rect class="literal" x="1078" y="63" width="77" height="24" rx="7"/><text class="text" x="1088" y="79">CURRENT</text><rect class="literal" x="1165" y="63" width="36" height="24" rx="7"/><text class="text" x="1175" y="79">OF</text><a xlink:href="../grammar_diagrams#cursor-name"><rect class="rule" x="1211" y="63" width="101" height="24"/><text class="text" x="1221" y="79">cursor_name</text></a><a xlink:href="../grammar_diagrams#returning-clause"><rect class="rule" x="1362" y="34" width="125" height="24"/><text class="text" x="1372" y="50">returning_clause</text></a></svg>

#### returning
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="342" height="92" viewbox="0 0 342 92"><path class="connector" d="M0 21h5m90 0h30m26 0h186m-227 0q5 0 5 5v48q0 5 5 5h25m-5 0q-5 0-5-5v-19q0-5 5-5h69m24 0h69q5 0 5 5v19q0 5-5 5m-5 0h25q5 0 5-5v-48q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="90" height="24" rx="7"/><text class="text" x="15" y="21">RETURNING</text><rect class="literal" x="125" y="5" width="26" height="24" rx="7"/><text class="text" x="135" y="21">*</text><rect class="literal" x="209" y="34" width="24" height="24" rx="7"/><text class="text" x="219" y="50">,</text><a xlink:href="../grammar_diagrams#returning-expression"><rect class="rule" x="145" y="63" width="152" height="24"/><text class="text" x="155" y="79">returning_expression</text></a></svg>

#### returning_expression
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="385" height="64" viewbox="0 0 385 64"><path class="connector" d="M0 21h5m136 0h50m36 0h20m-71 0q5 0 5 5v8q0 5 5 5h46q5 0 5-5v-8q0-5 5-5m5 0h10m103 0h20m-224 0q5 0 5 5v23q0 5 5 5h199q5 0 5-5v-23q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#output-expression"><rect class="rule" x="5" y="5" width="136" height="24"/><text class="text" x="15" y="21">output_expression</text></a><rect class="literal" x="191" y="5" width="36" height="24" rx="7"/><text class="text" x="201" y="21">AS</text><a xlink:href="../grammar_diagrams#output-name"><rect class="rule" x="257" y="5" width="103" height="24"/><text class="text" x="267" y="21">output_name</text></a></svg>

### Grammar
```
delete := [ WITH [ RECURSIVE ] with_query [, ...] ]
       DELETE FROM [ ONLY ] table_name [ * ] [ [ AS ] alias ]
       [ USING using_list ]
       [ WHERE condition | WHERE CURRENT OF cursor_name ]
       [ RETURNING * | output_expression [ [ AS ] output_name ] [, ...] ]
```

Where
- `with_query` specifies the subqueries that are referenced by name in the DELETE statement.

- `table_name` specifies a name of the table to be deleted.

- `alias` is the identifier of the target table within the DELETE statement. When an alias is specified, it must be used in place of the actual table in the statement.

- `output_expression` specifies the value to be returned. When the `output_expression` is referencing a column, the existing value of this column (deleted value) is used to evaluate.

## Semantics

- USING clause is not yet supported.

- While the where clause allows a wide range of operators, the exact conditions used in the where clause have significant performance considerations (especially for large datasets). WHERE clause that provides values for all columns in PRIMARY KEY or INDEX KEY has the best performance.

## Examples
Create a sample table, insert a few rows, then delete one of the inserted row.

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
postgres=# DELETE FROM sample WHERE k1 = 2 AND k2 = 3;
```

```sql
postgres=# SELECT * FROM sample ORDER BY k1;
```

```
DELETE 1
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  3 |  4 |  5 | c
(2 rows)
```

## See Also
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[`UPDATE`](../dml_update)
[Other PostgreSQL Statements](..)
