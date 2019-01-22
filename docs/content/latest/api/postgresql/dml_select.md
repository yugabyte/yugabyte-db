---
title: SELECT
summary: Retrieves rows from a table
description: SELECT
menu:
  latest:
    identifier: api-postgresql-select
    parent: api-postgresql-dml
aliases:
  - /latest/api/postgresql/dml/select
  - /latest/api/ysql/dml/select
---

## Synopsis
The `SELECT` statement retrieves (part of) rows of specified columns that meet a given condition from a table. It specifies the columns to be retrieved, the name of the table, and the condition each selected row must satisfy.

## Syntax

### Diagram

#### select
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="501" height="975" viewbox="0 0 501 975"><path class="connector" d="M0 52h25m53 0h30m90 0h20m-125 0q5 0 5 5v8q0 5 5 5h100q5 0 5-5v-8q0-5 5-5m5 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h37m24 0h37q5 0 5 5v20q0 5-5 5m-5 0h40m-366 0q5 0 5 5v23q0 5 5 5h341q5 0 5-5v-23q0-5 5-5m5 0h5m-381 65h5m66 0h30m42 0h347m-399 55q0 5 5 5h5m78 0h30m38 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h34m24 0h35q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h20m-276 0q5 0 5 5v8q0 5 5 5h251q5 0 5-5v-8q0-5 5-5m5 0h5q5 0 5-5m-394-55q5 0 5 5v78q0 5 5 5h379q5 0 5-5v-78q0-5 5-5m5 0h5m-495 115h25m28 0h443m-481 40q0 5 5 5h5m83 0h50m36 0h20m-71 0q5 0 5 5v8q0 5 5 5h46q5 0 5-5v-8q0-5 5-5m5 0h10m54 0h20m-175 0q5 0 5 5v23q0 5 5 5h150q5 0 5-5v-23q0-5 5-5m5 0h50m-5 0q-5 0-5-5v-17q0-5 5-5h98q5 0 5 5v17q0 5-5 5m-69 0h10m54 0h40m-163 0q5 0 5 5v8q0 5 5 5h138q5 0 5-5v-8q0-5 5-5m5 0h5q5 0 5-5m-476-40q5 0 5 5v78q0 5 5 5h461q5 0 5-5v-78q0-5 5-5m5 0h5m-501 145h25m54 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h34m24 0h35q5 0 5 5v20q0 5-5 5m-5 0h40m-222 0q5 0 5 5v8q0 5 5 5h197q5 0 5-5v-8q0-5 5-5m5 0h5m-237 50h25m65 0h10m74 0h20m-184 0q5 0 5 5v8q0 5 5 5h159q5 0 5-5v-8q0-5 5-5m5 0h5m-199 80h25m62 0h10m35 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h57m24 0h58q5 0 5 5v20q0 5-5 5m-5 0h40m-321 0q5 0 5 5v8q0 5 5 5h296q5 0 5-5v-8q0-5 5-5m5 0h5m-336 80h25m68 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h30m24 0h30q5 0 5 5v20q0 5-5 5m-5 0h40m-227 0q5 0 5 5v8q0 5 5 5h202q5 0 5-5v-8q0-5 5-5m5 0h5m-242 50h45m61 0h47m-118 25q0 5 5 5h5m88 0h5q5 0 5-5m-113-25q5 0 5 5v50q0 5 5 5h5m66 0h27q5 0 5-5v-50q0-5 5-5m5 0h30m42 0h56m-108 25q0 5 5 5h5m78 0h5q5 0 5-5m-103-25q5 0 5 5v33q0 5 5 5h88q5 0 5-5v-33q0-5 5-5m5 0h10m54 0h20m-355 0q5 0 5 5v68q0 5 5 5h330q5 0 5-5v-68q0-5 5-5m5 0h5m-370 140h25m62 0h10m35 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h35m24 0h36q5 0 5 5v20q0 5-5 5m-5 0h40m-277 0q5 0 5 5v8q0 5 5 5h252q5 0 5-5v-8q0-5 5-5m5 0h5m-292 50h25m54 0h30m62 0h20m-92 25q0 5 5 5h5m42 0h25q5 0 5-5m-87-25q5 0 5 5v33q0 5 5 5h72q5 0 5-5v-33q0-5 5-5m5 0h20m-201 0q5 0 5 5v43q0 5 5 5h176q5 0 5-5v-43q0-5 5-5m5 0h5m-216 85h25m66 0h10m62 0h30m49 0h28m-87 25q0 5 5 5h5m57 0h5q5 0 5-5m-82-25q5 0 5 5v33q0 5 5 5h67q5 0 5-5v-33q0-5 5-5m5 0h20m-280 0q5 0 5 5v43q0 5 5 5h255q5 0 5-5v-43q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="35" width="53" height="25" rx="7"/><text class="text" x="35" y="52">WITH</text><rect class="literal" x="108" y="35" width="90" height="25" rx="7"/><text class="text" x="118" y="52">RECURSIVE</text><rect class="literal" x="280" y="5" width="24" height="25" rx="7"/><text class="text" x="290" y="22">,</text><a xlink:href="../grammar_diagrams#with-query"><rect class="rule" x="248" y="35" width="88" height="25"/><text class="text" x="258" y="52">with_query</text></a><rect class="literal" x="5" y="100" width="66" height="25" rx="7"/><text class="text" x="15" y="117">SELECT</text><rect class="literal" x="101" y="100" width="42" height="25" rx="7"/><text class="text" x="111" y="117">ALL</text><rect class="literal" x="101" y="160" width="78" height="25" rx="7"/><text class="text" x="111" y="177">DISTINCT</text><a xlink:href="../grammar_diagrams#ON"><rect class="rule" x="209" y="160" width="38" height="25"/><text class="text" x="219" y="177">ON</text></a><rect class="literal" x="257" y="160" width="25" height="25" rx="7"/><text class="text" x="267" y="177">(</text><rect class="literal" x="341" y="130" width="24" height="25" rx="7"/><text class="text" x="351" y="147">,</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="312" y="160" width="83" height="25"/><text class="text" x="322" y="177">expression</text></a><rect class="literal" x="425" y="160" width="25" height="25" rx="7"/><text class="text" x="435" y="177">)</text><rect class="literal" x="25" y="215" width="28" height="25" rx="7"/><text class="text" x="35" y="232">*</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="25" y="260" width="83" height="25"/><text class="text" x="35" y="277">expression</text></a><rect class="literal" x="158" y="260" width="36" height="25" rx="7"/><text class="text" x="168" y="277">AS</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="224" y="260" width="54" height="25"/><text class="text" x="234" y="277">name</text></a><rect class="literal" x="348" y="260" width="24" height="25" rx="7"/><text class="text" x="358" y="277">,</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="382" y="260" width="54" height="25"/><text class="text" x="392" y="277">name</text></a><rect class="literal" x="25" y="360" width="54" height="25" rx="7"/><text class="text" x="35" y="377">FROM</text><rect class="literal" x="138" y="330" width="24" height="25" rx="7"/><text class="text" x="148" y="347">,</text><a xlink:href="../grammar_diagrams#from-item"><rect class="rule" x="109" y="360" width="83" height="25"/><text class="text" x="119" y="377">from_item</text></a><rect class="literal" x="25" y="410" width="65" height="25" rx="7"/><text class="text" x="35" y="427">WHERE</text><a xlink:href="../grammar_diagrams#condition"><rect class="rule" x="100" y="410" width="74" height="25"/><text class="text" x="110" y="427">condition</text></a><rect class="literal" x="25" y="490" width="62" height="25" rx="7"/><text class="text" x="35" y="507">GROUP</text><rect class="literal" x="97" y="490" width="35" height="25" rx="7"/><text class="text" x="107" y="507">BY</text><rect class="literal" x="214" y="460" width="24" height="25" rx="7"/><text class="text" x="224" y="477">,</text><a xlink:href="../grammar_diagrams#grouping-element"><rect class="rule" x="162" y="490" width="129" height="25"/><text class="text" x="172" y="507">grouping_element</text></a><rect class="literal" x="25" y="570" width="68" height="25" rx="7"/><text class="text" x="35" y="587">HAVING</text><rect class="literal" x="148" y="540" width="24" height="25" rx="7"/><text class="text" x="158" y="557">,</text><a xlink:href="../grammar_diagrams#condition"><rect class="rule" x="123" y="570" width="74" height="25"/><text class="text" x="133" y="587">condition</text></a><rect class="literal" x="45" y="620" width="61" height="25" rx="7"/><text class="text" x="55" y="637">UNION</text><rect class="literal" x="45" y="650" width="88" height="25" rx="7"/><text class="text" x="55" y="667">INTERSECT</text><rect class="literal" x="45" y="680" width="66" height="25" rx="7"/><text class="text" x="55" y="697">EXCEPT</text><rect class="literal" x="183" y="620" width="42" height="25" rx="7"/><text class="text" x="193" y="637">ALL</text><rect class="literal" x="183" y="650" width="78" height="25" rx="7"/><text class="text" x="193" y="667">DISTINCT</text><a xlink:href="../grammar_diagrams#select"><rect class="rule" x="291" y="620" width="54" height="25"/><text class="text" x="301" y="637">select</text></a><rect class="literal" x="25" y="760" width="62" height="25" rx="7"/><text class="text" x="35" y="777">ORDER</text><rect class="literal" x="97" y="760" width="35" height="25" rx="7"/><text class="text" x="107" y="777">BY</text><rect class="literal" x="192" y="730" width="24" height="25" rx="7"/><text class="text" x="202" y="747">,</text><a xlink:href="../grammar_diagrams#order-expr"><rect class="rule" x="162" y="760" width="85" height="25"/><text class="text" x="172" y="777">order_expr</text></a><rect class="literal" x="25" y="810" width="54" height="25" rx="7"/><text class="text" x="35" y="827">LIMIT</text><a xlink:href="../grammar_diagrams#integer"><rect class="rule" x="109" y="810" width="62" height="25"/><text class="text" x="119" y="827">integer</text></a><rect class="literal" x="109" y="840" width="42" height="25" rx="7"/><text class="text" x="119" y="857">ALL</text><rect class="literal" x="25" y="895" width="66" height="25" rx="7"/><text class="text" x="35" y="912">OFFSET</text><a xlink:href="../grammar_diagrams#integer"><rect class="rule" x="101" y="895" width="62" height="25"/><text class="text" x="111" y="912">integer</text></a><rect class="literal" x="193" y="895" width="49" height="25" rx="7"/><text class="text" x="203" y="912">ROW</text><rect class="literal" x="193" y="925" width="57" height="25" rx="7"/><text class="text" x="203" y="942">ROWS</text></svg>

### order_expr
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="498" height="100" viewbox="0 0 498 100"><path class="connector" d="M0 22h5m83 0h30m44 0h116m-170 25q0 5 5 5h5m53 0h92q5 0 5-5m-160 30q0 5 5 5h5m60 0h10m70 0h5q5 0 5-5m-165-55q5 0 5 5v63q0 5 5 5h150q5 0 5-5v-63q0-5 5-5m5 0h30m60 0h30m55 0h20m-90 0q5 0 5 5v20q0 5 5 5h5m50 0h10q5 0 5-5v-20q0-5 5-5m5 0h20m-200 0q5 0 5 5v38q0 5 5 5h175q5 0 5-5v-38q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="5" y="5" width="83" height="25"/><text class="text" x="15" y="22">expression</text></a><rect class="literal" x="118" y="5" width="44" height="25" rx="7"/><text class="text" x="128" y="22">ASC</text><rect class="literal" x="118" y="35" width="53" height="25" rx="7"/><text class="text" x="128" y="52">DESC</text><rect class="literal" x="118" y="65" width="60" height="25" rx="7"/><text class="text" x="128" y="82">USING</text><a xlink:href="../grammar_diagrams#operator"><rect class="rule" x="188" y="65" width="70" height="25"/><text class="text" x="198" y="82">operator</text></a><rect class="literal" x="308" y="5" width="60" height="25" rx="7"/><text class="text" x="318" y="22">NULLS</text><rect class="literal" x="398" y="5" width="55" height="25" rx="7"/><text class="text" x="408" y="22">FIRST</text><rect class="literal" x="398" y="35" width="50" height="25" rx="7"/><text class="text" x="408" y="52">LAST</text></svg>

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

- Create two sample tables.

```{.sql .copy .separator-hash}
postgres=# CREATE TABLE sample1(k1 bigint, k2 float, v text, PRIMARY KEY (k1, k2));
```
```{.sql .copy .separator-hash}
postgres=# CREATE TABLE sample2(k1 bigint, k2 float, v text, PRIMARY KEY (k1, k2));
```

- Insert some rows.

```{.sql .copy .separator-hash}
postgres=# INSERT INTO sample1(k1, k2, v) VALUES (1, 2.5, 'abc'), (1, 3.5, 'def'), (1, 4.5, 'xyz');
```

```{.sql .copy .separator-hash}
postgres=# INSERT INTO sample2(k1, k2, v) VALUES (1, 2.5, 'foo'), (1, 4.5, 'bar');
```

- Select from both tables using join.

```{.sql .copy .separator-hash}
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
