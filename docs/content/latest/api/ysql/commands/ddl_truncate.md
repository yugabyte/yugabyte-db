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

### Diagrams

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="483" height="78" viewbox="0 0 483 78"><path class="connector" d="M0 50h5m84 0h30m57 0h20m-92 0q5 0 5 5v8q0 5 5 5h67q5 0 5-5v-8q0-5 5-5m5 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h109m24 0h109q5 0 5 5v19q0 5-5 5m-237 0h20m51 0h20m-86 0q5 0 5 5v8q0 5 5 5h61q5 0 5-5v-8q0-5 5-5m5 0h10m55 0h30m26 0h20m-61 0q5 0 5 5v8q0 5 5 5h36q5 0 5-5v-8q0-5 5-5m5 0h25"/><rect class="literal" x="5" y="34" width="84" height="24" rx="7"/><text class="text" x="15" y="50">TRUNCATE</text><rect class="literal" x="119" y="34" width="57" height="24" rx="7"/><text class="text" x="129" y="50">TABLE</text><rect class="literal" x="330" y="5" width="24" height="24" rx="7"/><text class="text" x="340" y="21">,</text><rect class="literal" x="246" y="34" width="51" height="24" rx="7"/><text class="text" x="256" y="50">ONLY</text><a xlink:href="../../grammar_diagrams#name"><rect class="rule" x="327" y="34" width="55" height="24"/><text class="text" x="337" y="50">name</text></a><rect class="literal" x="412" y="34" width="26" height="24" rx="7"/><text class="text" x="422" y="50">*</text></svg>

### Grammar
```
truncate_stmt ::= TRUNCATE [ TABLE ] { [ ONLY ] name [ * ] } [, ... ]
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
[Other YSQL Statements](..)
