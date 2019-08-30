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

The `currval( sequence_name )` function returns the last value returned by `nextval( sequence_name )` for the specified sequence in the current session.

Where

- `sequence_name` is the name of the sequence

## Semantics

- An error is raised if `nextval( sequence_name )` has not been called in the current session for the specified sequence.

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

## See also

[`CREATE SEQUENCE`](../create_sequence)
[`DROP SEQUENCE`](../drop_sequence)
[`lastval()`](../lastval_sequence)
[`nextval()`](../nextval_sequence)
[Other YSQL Statements](..)
