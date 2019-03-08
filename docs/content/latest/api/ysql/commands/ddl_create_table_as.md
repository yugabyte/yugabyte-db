---
title: CREATE TABLE AS
linkTitle: CREATE TABLE AS
summary: Create a new table from a query result
description: CREATE TABLE AS
menu:
  latest:
    identifier: api-ysql-commands-create-table-as
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/ddl_create_table_as
isTocNested: true
showAsideToc: true
---

## Synopsis

`CREATE TABLE AS` command create new table using the output of a subquery.

## Syntax

### Diagrams

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="369" height="191" viewbox="0 0 369 191"><path class="connector" d="M0 21h5m67 0h10m57 0h30m30 0h10m45 0h10m61 0h20m-191 0q5 0 5 5v8q0 5 5 5h166q5 0 5-5v-8q0-5 5-5m5 0h5m-350 78h5m93 0h30m25 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h46m24 0h46q5 0 5 5v19q0 5-5 5m-5 0h30m25 0h20m-251 0q5 0 5 5v8q0 5 5 5h226q5 0 5-5v-8q0-5 5-5m5 0h5m-369 49h5m36 0h10m55 0h30m50 0h30m38 0h20m-73 0q5 0 5 5v8q0 5 5 5h48q5 0 5-5v-8q0-5 5-5m5 0h10m52 0h20m-235 0q5 0 5 5v23q0 5 5 5h210q5 0 5-5v-23q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="67" height="24" rx="7"/><text class="text" x="15" y="21">CREATE</text><rect class="literal" x="82" y="5" width="57" height="24" rx="7"/><text class="text" x="92" y="21">TABLE</text><rect class="literal" x="169" y="5" width="30" height="24" rx="7"/><text class="text" x="179" y="21">IF</text><rect class="literal" x="209" y="5" width="45" height="24" rx="7"/><text class="text" x="219" y="21">NOT</text><rect class="literal" x="264" y="5" width="61" height="24" rx="7"/><text class="text" x="274" y="21">EXISTS</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="5" y="83" width="93" height="24"/><text class="text" x="15" y="99">table_name</text></a><rect class="literal" x="128" y="83" width="25" height="24" rx="7"/><text class="text" x="138" y="99">(</text><rect class="literal" x="224" y="54" width="24" height="24" rx="7"/><text class="text" x="234" y="70">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="183" y="83" width="106" height="24"/><text class="text" x="193" y="99">column_name</text></a><rect class="literal" x="319" y="83" width="25" height="24" rx="7"/><text class="text" x="329" y="99">)</text><rect class="literal" x="5" y="132" width="36" height="24" rx="7"/><text class="text" x="15" y="148">AS</text><a xlink:href="../grammar_diagrams#query"><rect class="rule" x="51" y="132" width="55" height="24"/><text class="text" x="61" y="148">query</text></a><rect class="literal" x="136" y="132" width="50" height="24" rx="7"/><text class="text" x="146" y="148">WITH</text><rect class="literal" x="216" y="132" width="38" height="24" rx="7"/><text class="text" x="226" y="148">NO</text><rect class="literal" x="284" y="132" width="52" height="24" rx="7"/><text class="text" x="294" y="148">DATA</text></svg>

### Grammar
```
create_table_as ::= CREATE TABLE [ IF NOT EXISTS ] table_name [ (column_name [, ...] ) ]
                           AS query [ WITH [ NO ] DATA ]
```

Where
- table_name specifies the name of the table to be created.
- column_name specifies the name of a column in the new table. When not specified, column names are the selected names of the query.

## Semantics

- YugaByte may extend syntax to allow specifying PRIMARY KEY for `CREATE TABLE AS` command.

## Examples
```sql
postgres=# CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

```sql
postgres=# INSERT INTO sample VALUES (1, 2.0, 3, 'a'), (2, 3.0, 4, 'b'), (3, 4.0, 5, 'c');
```

```sql
postgres=# CREATE TABLE selective_sample SELECT * FROM sample WHERE k1 > 1;
```

```sql
postgres=# SELECT * FROM selective_sample ORDER BY k1;
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  2 |  3 |  4 | b
  3 |  4 |  5 | c
(2 rows)
```

## See Also
[`CREATE TABLE`](../dml_create_table)
[Other PostgreSQL Statements](..)
