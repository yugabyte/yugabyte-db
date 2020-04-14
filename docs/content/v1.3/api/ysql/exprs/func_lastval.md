---
title: lastval()
summary: Get the value returned from the last call to `nextval()`
description: lastval()
block_indexing: true
menu:
  v1.3:
    identifier: api-ysql-exprs-lastval
    parent: api-ysql-exprs
isTocNested: true
showAsideToc: true
---

## Synopsis

Use the `lastval()` function to return the value returned from the last call to `nextval()`, for any sequence, in the current session.

## Semantics

- An error is raised if `nextval()` has not been called in the current session.

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

## See also

[`CREATE SEQUENCE`](../create_sequence)
[`DROP SEQUENCE`](../drop_sequence)
[`currval()`](../currval_sequence)
[`nextval()`](../nextval_sequence)
[Other YSQL Statements](..)
