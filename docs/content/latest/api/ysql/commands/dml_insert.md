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

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="786" height="80" viewbox="0 0 786 80"><path class="connector" d="M0 52h5m65 0h10m50 0h10m111 0h30m36 0h10m54 0h20m-135 0q5 0 5 5v8q0 5 5 5h110q5 0 5-5v-8q0-5 5-5m5 0h10m25 0h10m89 0h10m25 0h10m68 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h34m24 0h35q5 0 5 5v20q0 5-5 5m-5 0h25"/><rect class="literal" x="5" y="35" width="65" height="25" rx="7"/><text class="text" x="15" y="52">INSERT</text><rect class="literal" x="80" y="35" width="50" height="25" rx="7"/><text class="text" x="90" y="52">INTO</text><a xlink:href="../grammar_diagrams#qualified-name"><rect class="rule" x="140" y="35" width="111" height="25"/><text class="text" x="150" y="52">qualified_name</text></a><rect class="literal" x="281" y="35" width="36" height="25" rx="7"/><text class="text" x="291" y="52">AS</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="327" y="35" width="54" height="25"/><text class="text" x="337" y="52">name</text></a><rect class="literal" x="411" y="35" width="25" height="25" rx="7"/><text class="text" x="421" y="52">(</text><a xlink:href="../grammar_diagrams#column-list"><rect class="rule" x="446" y="35" width="89" height="25"/><text class="text" x="456" y="52">column_list</text></a><rect class="literal" x="545" y="35" width="25" height="25" rx="7"/><text class="text" x="555" y="52">)</text><rect class="literal" x="580" y="35" width="68" height="25" rx="7"/><text class="text" x="590" y="52">VALUES</text><rect class="literal" x="707" y="5" width="24" height="25" rx="7"/><text class="text" x="717" y="22">,</text><a xlink:href="../grammar_diagrams#values-list"><rect class="rule" x="678" y="35" width="83" height="25"/><text class="text" x="688" y="52">values_list</text></a></svg>

#### values_list

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="203" height="65" viewbox="0 0 203 65"><path class="connector" d="M0 52h5m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h34m24 0h35q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h5"/><rect class="literal" x="5" y="35" width="25" height="25" rx="7"/><text class="text" x="15" y="52">(</text><rect class="literal" x="89" y="5" width="24" height="25" rx="7"/><text class="text" x="99" y="22">,</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="60" y="35" width="83" height="25"/><text class="text" x="70" y="52">expression</text></a><rect class="literal" x="173" y="35" width="25" height="25" rx="7"/><text class="text" x="183" y="52">)</text></svg>

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
postgres=# INSERT INTO sample(k1, k2, v1, v2) VALUES (1, 2.0, 3, 'a'), (2, 3.0, 4, 'b'), (3, 4.0, 5, 'c');
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
(2 rows)
```

## See Also

[`CREATE TABLE`](../ddl_create_table)
[`SELECT`](../dml_select)
[Other PostgreSQL Statements](..)
