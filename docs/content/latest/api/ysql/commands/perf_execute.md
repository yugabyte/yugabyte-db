---
title: EXECUTE
description: EXECUTE
summary: EXECUTE
menu:
  latest:
    identifier: api-ysql-commands-execute
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/perf_execute
isTocNested: true
showAsideToc: true
---

## Synopsis

`EXECUTE` command executes a previously prepared statement. This separation is a performance optimization because a prepared statement would be executed many times with different values while the syntax and semantics analysis and rewriting are done only once during `PREPARE` processing.

## Syntax

### Diagrams

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="398" height="78" viewbox="0 0 398 78"><path class="connector" d="M0 50h5m75 0h10m55 0h30m25 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h37m24 0h37q5 0 5 5v19q0 5-5 5m-5 0h30m25 0h20m-233 0q5 0 5 5v8q0 5 5 5h208q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="34" width="75" height="24" rx="7"/><text class="text" x="15" y="50">EXECUTE</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="90" y="34" width="55" height="24"/><text class="text" x="100" y="50">name</text></a><rect class="literal" x="175" y="34" width="25" height="24" rx="7"/><text class="text" x="185" y="50">(</text><rect class="literal" x="262" y="5" width="24" height="24" rx="7"/><text class="text" x="272" y="21">,</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="230" y="34" width="88" height="24"/><text class="text" x="240" y="50">expression</text></a><rect class="literal" x="348" y="34" width="25" height="24" rx="7"/><text class="text" x="358" y="50">)</text></svg>

### Grammar

```
EXECUTE name [ ( expression [, ...] ) ]
```

## Semantics

- Each expression in `EXECUTE` must match with the corresponding data type from `PREPARE`.

## Examples

- Create a sample table.

```sql
postgres=# CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

- Prepare a simple insert.

```sql
postgres=# PREPARE ins (bigint, double precision, int, text) AS 
               INSERT INTO sample(k1, k2, v1, v2) VALUES ($1, $2, $3, $4);
```

- Execute the insert twice (with different parameters).

```sql
postgres=# EXECUTE ins(1, 2.0, 3, 'a');
```
```sql
postgres=# EXECUTE ins(2, 3.0, 4, 'b');
```

- Check the results.

```sql
postgres=# SELECT * FROM sample ORDER BY k1;
```
```
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  2 |  3 |  4 | b
(2 rows)
```

## See Also

[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[Other YSQL Statements](..)
