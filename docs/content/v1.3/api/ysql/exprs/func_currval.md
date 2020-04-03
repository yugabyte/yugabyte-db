---
title: currval()
summary: Get the last value returned by `nextval()` in the current session
description: currval()
block_indexing: true
menu:
  v1.3:
    identifier: api-ysql-exprs-currval
    parent: api-ysql-exprs
isTocNested: true
showAsideToc: true
---

## Synopsis

Use the `currval( sequence_name )` function to return the last value returned by the `nextval( sequence_name )` function for the specified sequence in the current session.

## Semantics

### _sequence_name_

Specify the name of the sequence.

- An error is raised if `nextval( sequence_name )` has not been called for the specified sequence in the current session.

## Examples

### Create a sequence

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
