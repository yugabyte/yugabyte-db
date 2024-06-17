---
title: Subscripted expressions [YCQL]
headerTitle: Subscripted expressions
linkTitle: Subscripted expressions
description: Use subscripted expressions to access elements in a multi-element value, such as a map collection by using the [] operator.
menu:
  v2.20:
    parent: api-cassandra
    weight: 1340
type: docs
---

Use subscripted expressions to access elements in a multi-element value, such as a map collection by using operator `[]`. Subscripted column expressions can be used when writing the same way as a [column expression](../expr_simple##Column). For example, if `ids` refers to a column of type `LIST`, `ids[7]` refers to the third element of the list `ids`, which can be set in an [UPDATE](../dml_update/) statement.

- Subscripted expression can only be applied to columns of type `LIST`, `MAP`, or user-defined data types.
- Subscripting a `LIST` value with a non-positive index will yield NULL.
- Subscripting a `MAP` value with a non-existing key will yield NULL. Otherwise, it returns the element value that is associated with the given key.
- Apache Cassandra does not allow subscripted expression in the select list of the SELECT statement.

## Examples

```sql
ycqlsh:yugaspace> CREATE TABLE t(id INT PRIMARY KEY,yugamap MAP<TEXT, TEXT>);
```

```sql
ycqlsh:yugaspace> UPDATE yugatab SET map_value['key_value'] = 'yuga_string' WHERE id = 7;
```

## See also

- [All Expressions](..##expressions)
