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

### Diagram 

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="1047" height="64" viewbox="0 0 1047 64"><path class="connector" d="M0 21h5m67 0h10m57 0h30m30 0h10m45 0h10m61 0h20m-191 0q5 0 5 5v8q0 5 5 5h166q5 0 5-5v-8q0-5 5-5m5 0h10m93 0h30m25 0h10m113 0h10m25 0h20m-218 0q5 0 5 5v8q0 5 5 5h193q5 0 5-5v-8q0-5 5-5m5 0h10m36 0h10m55 0h30m50 0h30m38 0h20m-73 0q5 0 5 5v8q0 5 5 5h48q5 0 5-5v-8q0-5 5-5m5 0h10m52 0h20m-235 0q5 0 5 5v23q0 5 5 5h210q5 0 5-5v-23q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="67" height="24" rx="7"/><text class="text" x="15" y="21">CREATE</text><rect class="literal" x="82" y="5" width="57" height="24" rx="7"/><text class="text" x="92" y="21">TABLE</text><rect class="literal" x="169" y="5" width="30" height="24" rx="7"/><text class="text" x="179" y="21">IF</text><rect class="literal" x="209" y="5" width="45" height="24" rx="7"/><text class="text" x="219" y="21">NOT</text><rect class="literal" x="264" y="5" width="61" height="24" rx="7"/><text class="text" x="274" y="21">EXISTS</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="355" y="5" width="93" height="24"/><text class="text" x="365" y="21">table_name</text></a><rect class="literal" x="478" y="5" width="25" height="24" rx="7"/><text class="text" x="488" y="21">(</text><a xlink:href="../grammar_diagrams#column-names"><rect class="rule" x="513" y="5" width="113" height="24"/><text class="text" x="523" y="21">column_names</text></a><rect class="literal" x="636" y="5" width="25" height="24" rx="7"/><text class="text" x="646" y="21">)</text><rect class="literal" x="691" y="5" width="36" height="24" rx="7"/><text class="text" x="701" y="21">AS</text><a xlink:href="../grammar_diagrams#query"><rect class="rule" x="737" y="5" width="55" height="24"/><text class="text" x="747" y="21">query</text></a><rect class="literal" x="822" y="5" width="50" height="24" rx="7"/><text class="text" x="832" y="21">WITH</text><rect class="literal" x="902" y="5" width="38" height="24" rx="7"/><text class="text" x="912" y="21">NO</text><rect class="literal" x="970" y="5" width="52" height="24" rx="7"/><text class="text" x="980" y="21">DATA</text></svg>

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
