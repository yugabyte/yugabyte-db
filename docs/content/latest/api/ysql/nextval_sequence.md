---
title: nextval()
summary: Get the next value from the session's sequence cache
description: nextval()
menu:
  latest:
    identifier: api-postgresql-nextval
    parent: api-postgresql-sequences
aliases:
  - /latest/api/postgresql/nextval_sequence
  - /latest/api/ysql/nextval_sequence
isTocNested: true
showAsideToc: true
---

## Synopsis
The `nextval()` function returns the next value from the session's sequence cache. If no more values are available in the cache, the session allocates a block of numbers for the cache and returns the first one. The number of elements allocated is determined by the `cache` option specified as part of the `CREATE SEQUENCE` statement.

## Syntax

### Diagram
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="318" height="35" viewbox="0 0 318 35"><path class="connector" d="M0 22h5m66 0h10m69 0h10m118 0h10m25 0h5"/><rect class="literal" x="5" y="5" width="66" height="25" rx="7"/><text class="text" x="15" y="22">SELECT</text><rect class="literal" x="81" y="5" width="69" height="25" rx="7"/><text class="text" x="91" y="22">nextval(</text><a xlink:href="../grammar_diagrams#sequence-name"><rect class="rule" x="160" y="5" width="118" height="25"/><text class="text" x="170" y="22">sequence_name</text></a><rect class="literal" x="288" y="5" width="25" height="25" rx="7"/><text class="text" x="298" y="22">)</text></svg>

### sequence_name
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="117" height="35" viewbox="0 0 117 35"><path class="connector" d="M0 22h5m107 0h5"/><rect class="literal" x="5" y="5" width="107" height="25" rx="7"/><text class="text" x="15" y="22">&lt;Text Literal&gt;</text></svg>

### Grammar
```
nextval ::= SELECT nextval( sequence_name );
```

Where

- `sequence_name` is the name of the sequence

## Semantics
- An error is raised if a sequence reaches its minimum or maximum value.

## Examples

Create a simple sequence that increments by 1 every time nextval() is called.

```sql
postgres=# CREATE SEQUENCE s;
```
```
CREATE SEQUENCE
```

Call nextval() a couple of times.

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
postgres=# SELECT nextval('s');
```
```
 nextval
---------
       2
(1 row)
```

Create a sequence with a cache of 3 values.

```sql
postgres=# CREATE SEQUENCE s2 CACHE 3;
```
```
CREATE SEQUENCE
```

In the same session, call `nextval()`. The first time it's called, the session's cache will allocate numbers 1, 2, and 3. This means that the data for this sequence will have its `last_val` set to 3. This modification requires two RPCs.
```sql
SELECT nextval('s2');
```
```
 nextval
---------
       1
(1 row)
```

The next call of `nextval()` in the same session will not generate new numbers for the sequence, so it is much faster than the first `nextval()` call because it will just use the next value available from the cache.

```sql
SELECT nextval('s2');
```
```
nextval
---------
       2
(1 row)
```

## See Also
[`CREATE SEQUENCE`](../create_sequence)
[`DROP SEQUENCE`](../drop_sequence)
[`currval()`](../currval_sequence)
[`lastval()`](../lastval_sequence)
[Other PostgreSQL Statements](..)
