---
title: currval()
summary: Get the last value returned by `nextval()` in the current session
description: currval()
menu:
  latest:
    identifier: api-ysql-exprs-currval
    parent: api-ysql-exprs
aliases:
  - /latest/api/ysql/exprs/func_currval
isTocNested: true
showAsideToc: true
---

## Synopsis
The `currval()` function returns the last value returned by `nextval()` for the specified sequence in the current session.

## Syntax

### Diagram
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="315" height="35" viewbox="0 0 315 35"><path class="connector" d="M0 22h5m66 0h10m66 0h10m118 0h10m25 0h5"/><rect class="literal" x="5" y="5" width="66" height="25" rx="7"/><text class="text" x="15" y="22">SELECT</text><rect class="literal" x="81" y="5" width="66" height="25" rx="7"/><text class="text" x="91" y="22">currval(</text><a xlink:href="../grammar_diagrams#sequence-name"><rect class="rule" x="157" y="5" width="118" height="25"/><text class="text" x="167" y="22">sequence_name</text></a><rect class="literal" x="285" y="5" width="25" height="25" rx="7"/><text class="text" x="295" y="22">)</text></svg>

### sequence_name
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="117" height="35" viewbox="0 0 117 35"><path class="connector" d="M0 22h5m107 0h5"/><rect class="literal" x="5" y="5" width="107" height="25" rx="7"/><text class="text" x="15" y="22">&lt;Text Literal&gt;</text></svg>

### Grammar
```
currval ::= SELECT currval( sequence_name );
```

Where

- `sequence_name` is the name of the sequence

## Semantics
- An error is raised if `nextval()` for the specified sequence hasn't been called in the current session.

## Examples

Create a sequence.

```sql
postgres=# CREATE SEQUENCE s;
```
```
CREATE SEQUENCE
```

Call `nextval()`.

```sql
postgres=# SELECT nextval('s');
```
```
 nextval
---------
       1
(1 row)
```
```sql
postgres=# SELECT currval('s');
```
```
 currval
---------
       1
(1 row)
```

Call `currval()` before `nextval()` is called.

```sql
postgres=# CREATE SEQUENCE s2;
```
```
CREATE SEQUENCE
```

```sql
SELECT currval('s2');
```
```
ERROR:  currval of sequence "s2" is not yet defined in this session
```


## See Also
[`CREATE SEQUENCE`](../create_sequence)
[`DROP SEQUENCE`](../drop_sequence)
[`lastval()`](../lastval_sequence)
[`nextval()`](../nextval_sequence)
[Other PostgreSQL Statements](..)
