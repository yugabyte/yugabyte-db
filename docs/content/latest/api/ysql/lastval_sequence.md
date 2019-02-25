---
title: lastval()
summary: Get the value returned from the last call to `nextval()`
description: lastval()
menu:
  latest:
    identifier: api-postgresql-lastval
    parent: api-postgresql-sequences
aliases:
  - /latest/api/postgresql/lastval_sequence
  - /latest/api/ysql/lastval_sequence
isTocNested: true
showAsideToc: true
---

## Synopsis
The `lastval()` function returns the value returned from the last call to `nextval()` (for any sequence) in the current session.

## Syntax

### Diagram
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="154" height="35" viewbox="0 0 154 35"><path class="connector" d="M0 22h5m66 0h10m68 0h5"/><rect class="literal" x="5" y="5" width="66" height="25" rx="7"/><text class="text" x="15" y="22">SELECT</text><rect class="literal" x="81" y="5" width="68" height="25" rx="7"/><text class="text" x="91" y="22">lastval()</text></svg>

### Grammar
```
lastval ::= SELECT lastval();
```

## Semantics
- An error is raised if `nextval()` hasn't been called in the current session.

## Examples

Create two sequences and call `nextval()` for each of them.

```sql
postgres=# CREATE SEQUENCE s1;
```
```
CREATE SEQUENCE
```
```sql
postgres=# CREATE SEQUENCE s2 START -100 MINVALUE -100;
```
```
CREATE SEQUENCE
```
```sql
postgres=# SELECT nextval('s1');
```
```
 nextval
---------
       1
(1 row)
```
```sql
postgres=# SELECT nextval('s2');
```
```
 nextval
---------
    -100
(1 row)
```

Call `lastval()`.

```sql
postgres=# SELECT lastval()
```
```
 lastval
---------
    -100
(1 row)

```

## See Also
[`CREATE SEQUENCE`](../create_sequence)
[`DROP SEQUENCE`](../drop_sequence)
[`currval()`](../currval_sequence)
[`nextval()`](../nextval_sequence)
[Other PostgreSQL Statements](..)
