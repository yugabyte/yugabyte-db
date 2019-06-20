---
title: PREPARE
description: PREPARE
summary: PREPARE
menu:
  latest:
    identifier: api-ysql-commands-prepare
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/prepare_execute/
isTocNested: true
showAsideToc: true
---

## Synopsis

The `PREPARE` command creates a handle to a prepared statement by parsing, analyzing and rewriting (but not executing) the target statement. 

## Syntax

### Diagrams

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="534" height="78" viewbox="0 0 534 78"><path class="connector" d="M0 50h5m76 0h10m55 0h30m25 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h34m24 0h34q5 0 5 5v19q0 5-5 5m-5 0h30m25 0h20m-227 0q5 0 5 5v8q0 5 5 5h202q5 0 5-5v-8q0-5 5-5m5 0h10m36 0h10m85 0h5"/><rect class="literal" x="5" y="34" width="76" height="24" rx="7"/><text class="text" x="15" y="50">PREPARE</text><a xlink:href="../../grammar_diagrams#name"><rect class="rule" x="91" y="34" width="55" height="24"/><text class="text" x="101" y="50">name</text></a><rect class="literal" x="176" y="34" width="25" height="24" rx="7"/><text class="text" x="186" y="50">(</text><rect class="literal" x="260" y="5" width="24" height="24" rx="7"/><text class="text" x="270" y="21">,</text><a xlink:href="../../grammar_diagrams#data-type"><rect class="rule" x="231" y="34" width="82" height="24"/><text class="text" x="241" y="50">data_type</text></a><rect class="literal" x="343" y="34" width="25" height="24" rx="7"/><text class="text" x="353" y="50">)</text><rect class="literal" x="398" y="34" width="36" height="24" rx="7"/><text class="text" x="408" y="50">AS</text><a xlink:href="../../grammar_diagrams#statement"><rect class="rule" x="444" y="34" width="85" height="24"/><text class="text" x="454" y="50">statement</text></a></svg>

### Grammar

```
PREPARE name [ ( data_type [, ...] ) ] AS statement
```

## Semantics

- The statement in `PREPARE` may (should) contain parameters (e.g. `$1`) that will be provided by the expression list in `EXECUTE`.
- The data type list in `PREPARE` represent the types for the parameters used in the statement.

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
