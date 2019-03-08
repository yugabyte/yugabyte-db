---
title: SELECT
summary: Retrieves rows from a table
description: SELECT
menu:
  latest:
    identifier: api-ysql-commands-select
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/select
isTocNested: true
showAsideToc: true
---

## Synopsis
The `SELECT` statement retrieves (part of) rows of specified columns that meet a given condition from a table. It specifies the columns to be retrieved, the name of the table, and the condition each selected row must satisfy.

## Syntax

### Diagram

#### select
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="3286" height="141" viewbox="0 0 3286 141"><path class="connector" d="M0 50h25m50 0h30m88 0h20m-123 0q5 0 5 5v8q0 5 5 5h98q5 0 5-5v-8q0-5 5-5m5 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h36m24 0h36q5 0 5 5v19q0 5-5 5m-5 0h40m-359 0q5 0 5 5v23q0 5 5 5h334q5 0 5-5v-23q0-5 5-5m5 0h10m65 0h30m40 0h350m-400 53q0 5 5 5h5m74 0h30m38 0h10m25 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h37m24 0h37q5 0 5 5v19q0 5-5 5m-5 0h30m25 0h20m-281 0q5 0 5 5v8q0 5 5 5h256q5 0 5-5v-8q0-5 5-5m5 0h5q5 0 5-5m-395-53q5 0 5 5v76q0 5 5 5h380q5 0 5-5v-76q0-5 5-5m5 0h30m26 0h166m-202 53q0 5 5 5h25m-5 0q-5 0-5-5v-19q0-5 5-5h59m24 0h59q5 0 5 5v19q0 5-5 5m-5 0h25q5 0 5-5m-197-53q5 0 5 5v61q0 5 5 5h182q5 0 5-5v-61q0-5 5-5m5 0h30m54 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h33m24 0h34q5 0 5 5v19q0 5-5 5m-5 0h40m-220 0q5 0 5 5v8q0 5 5 5h195q5 0 5-5v-8q0-5 5-5m5 0h30m64 0h10m78 0h20m-187 0q5 0 5 5v8q0 5 5 5h162q5 0 5-5v-8q0-5 5-5m5 0h30m63 0h10m35 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h59m24 0h60q5 0 5 5v19q0 5-5 5m-5 0h40m-326 0q5 0 5 5v8q0 5 5 5h301q5 0 5-5v-8q0-5 5-5m5 0h30m66 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h32m24 0h32q5 0 5 5v19q0 5-5 5m-5 0h40m-229 0q5 0 5 5v8q0 5 5 5h204q5 0 5-5v-8q0-5 5-5m5 0h50m59 0h47m-116 24q0 5 5 5h5m86 0h5q5 0 5-5m-111-24q5 0 5 5v48q0 5 5 5h5m66 0h25q5 0 5-5v-48q0-5 5-5m5 0h30m40 0h54m-104 24q0 5 5 5h5m74 0h5q5 0 5-5m-99-24q5 0 5 5v32q0 5 5 5h84q5 0 5-5v-32q0-5 5-5m5 0h10m58 0h20m-353 0q5 0 5 5v66q0 5 5 5h328q5 0 5-5v-66q0-5 5-5m5 0h30m62 0h10m35 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h36m24 0h37q5 0 5 5v19q0 5-5 5m-5 0h40m-279 0q5 0 5 5v8q0 5 5 5h254q5 0 5-5v-8q0-5 5-5m5 0h30m49 0h30m65 0h20m-95 24q0 5 5 5h5m40 0h30q5 0 5-5m-90-24q5 0 5 5v32q0 5 5 5h75q5 0 5-5v-32q0-5 5-5m5 0h20m-199 0q5 0 5 5v42q0 5 5 5h174q5 0 5-5v-42q0-5 5-5m5 0h30m66 0h10m65 0h30m48 0h28m-86 24q0 5 5 5h5m56 0h5q5 0 5-5m-81-24q5 0 5 5v32q0 5 5 5h66q5 0 5-5v-32q0-5 5-5m5 0h20m-282 0q5 0 5 5v42q0 5 5 5h257q5 0 5-5v-42q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="34" width="50" height="24" rx="7"/><text class="text" x="35" y="50">WITH</text><rect class="literal" x="105" y="34" width="88" height="24" rx="7"/><text class="text" x="115" y="50">RECURSIVE</text><rect class="literal" x="274" y="5" width="24" height="24" rx="7"/><text class="text" x="284" y="21">,</text><a xlink:href="../grammar_diagrams#with-query"><rect class="rule" x="243" y="34" width="86" height="24"/><text class="text" x="253" y="50">with_query</text></a><rect class="literal" x="379" y="34" width="65" height="24" rx="7"/><text class="text" x="389" y="50">SELECT</text><rect class="literal" x="474" y="34" width="40" height="24" rx="7"/><text class="text" x="484" y="50">ALL</text><rect class="literal" x="474" y="92" width="74" height="24" rx="7"/><text class="text" x="484" y="108">DISTINCT</text><rect class="literal" x="578" y="92" width="38" height="24" rx="7"/><text class="text" x="588" y="108">ON</text><rect class="literal" x="626" y="92" width="25" height="24" rx="7"/><text class="text" x="636" y="108">(</text><rect class="literal" x="713" y="63" width="24" height="24" rx="7"/><text class="text" x="723" y="79">,</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="681" y="92" width="88" height="24"/><text class="text" x="691" y="108">expression</text></a><rect class="literal" x="799" y="92" width="25" height="24" rx="7"/><text class="text" x="809" y="108">)</text><rect class="literal" x="894" y="34" width="26" height="24" rx="7"/><text class="text" x="904" y="50">*</text><rect class="literal" x="968" y="63" width="24" height="24" rx="7"/><text class="text" x="978" y="79">,</text><a xlink:href="../grammar_diagrams#select-expression"><rect class="rule" x="914" y="92" width="132" height="24"/><text class="text" x="924" y="108">select_expression</text></a><rect class="literal" x="1116" y="34" width="54" height="24" rx="7"/><text class="text" x="1126" y="50">FROM</text><rect class="literal" x="1228" y="5" width="24" height="24" rx="7"/><text class="text" x="1238" y="21">,</text><a xlink:href="../grammar_diagrams#from-item"><rect class="rule" x="1200" y="34" width="81" height="24"/><text class="text" x="1210" y="50">from_item</text></a><rect class="literal" x="1351" y="34" width="64" height="24" rx="7"/><text class="text" x="1361" y="50">WHERE</text><a xlink:href="../grammar_diagrams#condition"><rect class="rule" x="1425" y="34" width="78" height="24"/><text class="text" x="1435" y="50">condition</text></a><rect class="literal" x="1553" y="34" width="63" height="24" rx="7"/><text class="text" x="1563" y="50">GROUP</text><rect class="literal" x="1626" y="34" width="35" height="24" rx="7"/><text class="text" x="1636" y="50">BY</text><rect class="literal" x="1745" y="5" width="24" height="24" rx="7"/><text class="text" x="1755" y="21">,</text><a xlink:href="../grammar_diagrams#grouping-element"><rect class="rule" x="1691" y="34" width="133" height="24"/><text class="text" x="1701" y="50">grouping_element</text></a><rect class="literal" x="1894" y="34" width="66" height="24" rx="7"/><text class="text" x="1904" y="50">HAVING</text><rect class="literal" x="2017" y="5" width="24" height="24" rx="7"/><text class="text" x="2027" y="21">,</text><a xlink:href="../grammar_diagrams#condition"><rect class="rule" x="1990" y="34" width="78" height="24"/><text class="text" x="2000" y="50">condition</text></a><rect class="literal" x="2158" y="34" width="59" height="24" rx="7"/><text class="text" x="2168" y="50">UNION</text><rect class="literal" x="2158" y="63" width="86" height="24" rx="7"/><text class="text" x="2168" y="79">INTERSECT</text><rect class="literal" x="2158" y="92" width="66" height="24" rx="7"/><text class="text" x="2168" y="108">EXCEPT</text><rect class="literal" x="2294" y="34" width="40" height="24" rx="7"/><text class="text" x="2304" y="50">ALL</text><rect class="literal" x="2294" y="63" width="74" height="24" rx="7"/><text class="text" x="2304" y="79">DISTINCT</text><a xlink:href="../grammar_diagrams#select"><rect class="rule" x="2398" y="34" width="58" height="24"/><text class="text" x="2408" y="50">select</text></a><rect class="literal" x="2506" y="34" width="62" height="24" rx="7"/><text class="text" x="2516" y="50">ORDER</text><rect class="literal" x="2578" y="34" width="35" height="24" rx="7"/><text class="text" x="2588" y="50">BY</text><rect class="literal" x="2674" y="5" width="24" height="24" rx="7"/><text class="text" x="2684" y="21">,</text><a xlink:href="../grammar_diagrams#order-expr"><rect class="rule" x="2643" y="34" width="87" height="24"/><text class="text" x="2653" y="50">order_expr</text></a><rect class="literal" x="2800" y="34" width="49" height="24" rx="7"/><text class="text" x="2810" y="50">LIMIT</text><a xlink:href="../grammar_diagrams#integer"><rect class="rule" x="2879" y="34" width="65" height="24"/><text class="text" x="2889" y="50">integer</text></a><rect class="literal" x="2879" y="63" width="40" height="24" rx="7"/><text class="text" x="2889" y="79">ALL</text><rect class="literal" x="3014" y="34" width="66" height="24" rx="7"/><text class="text" x="3024" y="50">OFFSET</text><a xlink:href="../grammar_diagrams#integer"><rect class="rule" x="3090" y="34" width="65" height="24"/><text class="text" x="3100" y="50">integer</text></a><rect class="literal" x="3185" y="34" width="48" height="24" rx="7"/><text class="text" x="3195" y="50">ROW</text><rect class="literal" x="3185" y="63" width="56" height="24" rx="7"/><text class="text" x="3195" y="79">ROWS</text></svg>

#### select_expression
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="289" height="64" viewbox="0 0 289 64"><path class="connector" d="M0 21h5m88 0h50m36 0h20m-71 0q5 0 5 5v8q0 5 5 5h46q5 0 5-5v-8q0-5 5-5m5 0h10m55 0h20m-176 0q5 0 5 5v23q0 5 5 5h151q5 0 5-5v-23q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="5" y="5" width="88" height="24"/><text class="text" x="15" y="21">expression</text></a><rect class="literal" x="143" y="5" width="36" height="24" rx="7"/><text class="text" x="153" y="21">AS</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="209" y="5" width="55" height="24"/><text class="text" x="219" y="21">name</text></a></svg>

#### order_expr
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="502" height="97" viewbox="0 0 502 97"><path class="connector" d="M0 21h5m88 0h30m44 0h119m-173 24q0 5 5 5h5m53 0h95q5 0 5-5m-163 29q0 5 5 5h5m58 0h10m75 0h5q5 0 5-5m-168-53q5 0 5 5v61q0 5 5 5h153q5 0 5-5v-61q0-5 5-5m5 0h30m58 0h30m53 0h20m-88 0q5 0 5 5v19q0 5 5 5h5m49 0h9q5 0 5-5v-19q0-5 5-5m5 0h20m-196 0q5 0 5 5v37q0 5 5 5h171q5 0 5-5v-37q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="5" y="5" width="88" height="24"/><text class="text" x="15" y="21">expression</text></a><rect class="literal" x="123" y="5" width="44" height="24" rx="7"/><text class="text" x="133" y="21">ASC</text><rect class="literal" x="123" y="34" width="53" height="24" rx="7"/><text class="text" x="133" y="50">DESC</text><rect class="literal" x="123" y="63" width="58" height="24" rx="7"/><text class="text" x="133" y="79">USING</text><a xlink:href="../grammar_diagrams#operator"><rect class="rule" x="191" y="63" width="75" height="24"/><text class="text" x="201" y="79">operator</text></a><rect class="literal" x="316" y="5" width="58" height="24" rx="7"/><text class="text" x="326" y="21">NULLS</text><rect class="literal" x="404" y="5" width="53" height="24" rx="7"/><text class="text" x="414" y="21">FIRST</text><rect class="literal" x="404" y="34" width="49" height="24" rx="7"/><text class="text" x="414" y="50">LAST</text></svg>

### Grammar

```
select ::= [ WITH [ RECURSIVE ] with_query [ ',' ... ] ]  \
               SELECT [ ALL | DISTINCT [ ON ( expression { , expression } ) ] ] \
               [ * | expression [ [ AS ] name ] [ ',' ... ] ] \
               [ FROM from_item [ ','  ... ] ] \
               [ WHERE condition ] \
               [ GROUP BY grouping_element [ , ...] ] \
               [ HAVING condition [ ',' condition ] ] \
               [ { UNION | INTERSECT | EXCEPT } [ ALL | DISTINCT ] select ] \
               [ ORDER BY order_expr [ ',' ...] ] \
               [ LIMIT [ integer | ALL ] ] \
               [ OFFSET integer [ ROW | ROWS ] ] ;

order_expr = expression [ ASC | DESC | USING operator ] [ NULLS { FIRST | LAST } ];
```

Where

- `condition` is any expression that evaluates to boolean value.
- for more details on `from_item`, `grouping_element`, and`with_query` see [this](https://www.postgresql.org/docs/10/static/sql-select.html) page.

## Semantics
 - An error is raised if the specified `qualified_name` does not exist.
 - `*` represents all columns.

While the where clause allows a wide range of operators, the exact conditions used in the where clause have significant performance considerations (especially for large datasets).

## Examples

Create two sample tables.

```sql
postgres=# CREATE TABLE sample1(k1 bigint, k2 float, v text, PRIMARY KEY (k1, k2));
```


```sql
postgres=# CREATE TABLE sample2(k1 bigint, k2 float, v text, PRIMARY KEY (k1, k2));
```

Insert some rows.

```sql
postgres=# INSERT INTO sample1(k1, k2, v) VALUES (1, 2.5, 'abc'), (1, 3.5, 'def'), (1, 4.5, 'xyz');
```


```sql
postgres=# INSERT INTO sample2(k1, k2, v) VALUES (1, 2.5, 'foo'), (1, 4.5, 'bar');
```

Select from both tables using join.

```sql
postgres=# SELECT a.k1, a.k2, a.v as av, b.v as bv FROM sample1 a LEFT JOIN sample2 b ON (a.k1 = b.k1 and a.k2 = b.k2) WHERE a.k1 = 1 AND a.k2 IN (2.5, 3.5) ORDER BY a.k2 DESC;
```

```
 k1 | k2  | av  | bv
----+-----+-----+-----
  1 | 3.5 | def |
  1 | 2.5 | abc | foo
(2 rows)
```

## See Also

[`CREATE TABLE`](../ddl_create_table)
[`INSERT`](../dml_insert)
[Other PostgreSQL Statements](..)
