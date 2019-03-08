---
title: TRUNCATE
linkTitle: TRUNCATE
summary: Clear all rows in a table
description: TRUNCATE
menu:
  latest:
    identifier: api-ysql-commands-truncate
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/ddl_truncate
isTocNested: true
showAsideToc: true
---

## Synopsis

`TRUNCATE` command clears all rows in a table.

## Syntax

### Diagram 

#### Truncate

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="336" height="78" viewbox="0 0 336 78"><path class="connector" d="M0 50h5m84 0h30m57 0h20m-92 0q5 0 5 5v8q0 5 5 5h67q5 0 5-5v-8q0-5 5-5m5 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h35m24 0h36q5 0 5 5v19q0 5-5 5m-5 0h25"/><rect class="literal" x="5" y="34" width="84" height="24" rx="7"/><text class="text" x="15" y="50">TRUNCATE</text><rect class="literal" x="119" y="34" width="57" height="24" rx="7"/><text class="text" x="129" y="50">TABLE</text><rect class="literal" x="256" y="5" width="24" height="24" rx="7"/><text class="text" x="266" y="21">,</text><a xlink:href="../grammar_diagrams#table-expr"><rect class="rule" x="226" y="34" width="85" height="24"/><text class="text" x="236" y="50">table_expr</text></a></svg>

#### table_expr

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="242" height="49" viewbox="0 0 242 49"><path class="connector" d="M0 21h25m51 0h20m-86 0q5 0 5 5v8q0 5 5 5h61q5 0 5-5v-8q0-5 5-5m5 0h10m55 0h30m26 0h20m-61 0q5 0 5 5v8q0 5 5 5h36q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="51" height="24" rx="7"/><text class="text" x="35" y="21">ONLY</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="106" y="5" width="55" height="24"/><text class="text" x="116" y="21">name</text></a><rect class="literal" x="191" y="5" width="26" height="24" rx="7"/><text class="text" x="201" y="21">*</text></svg>

### Grammar
```
truncate_stmt ::= TRUNCATE [ TABLE ] [ ONLY ] name [ * ] [, ... ]
```

Where

- `name` specifies the table to be truncated.

## Semantics

- TRUNCATE acquires ACCESS EXCLUSIVE lock on the tables to be truncated. The ACCESS EXCLUSIVE locking option is not yet fully supported.
- TRUNCATE is not supported for foreign tables.

## Examples
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
postgres=# TRUNCATE sample;
```

```sql
postgres=# SELECT * FROM sample;
```

```
 k1 | k2 | v1 | v2
----+----+----+----
(0 rows)
```

## See Also
[Other PostgreSQL Statements](..)
