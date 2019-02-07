---
title: BOOLEAN
summary: Boolean values of false or true
description: BOOLEAN Type
menu:
  v1.0:
    parent: api-cassandra
    weight: 1380
---

## Synopsis

`BOOLEAN` datatype is used to specify values of either `true` or `false`.

## Syntax
```
type_specification ::= BOOLEAN

boolean_literal ::= TRUE | FALSE
```

## Semantics

- Columns of type `BOOLEAN` cannot be part of the `PRIMARY KEY`.
- Columns of type `BOOLEAN` can be set, inserted, and compared.
- In `WHERE` and `IF` clause, `BOOLEAN` columns cannot be used as a standalone expression. They must be compared with either `true` or `false`. For example, `WHERE boolean_column = TRUE` is valid while `WHERE boolean_column` is not.
- Implicitly, `BOOLEAN` is neither comparable nor convertible to any other datatypes.

## Examples

You can do this as shown below.
<div class='copy separator-gt'>
```sql
cqlsh:example> CREATE TABLE tasks (id INT PRIMARY KEY, finished BOOLEAN);
```
</div>
<div class='copy separator-gt'>
```sql
cqlsh:example> INSERT INTO tasks (id, finished) VALUES (1, false);
```
</div>
<div class='copy separator-gt'>
```sql
cqlsh:example> INSERT INTO tasks (id, finished) VALUES (2, false);
```
</div>
<div class='copy separator-gt'>
```sql
cqlsh:example> UPDATE tasks SET finished = true WHERE id = 2;
```
</div>
<div class='copy separator-gt'>
```sql
cqlsh:example> SELECT * FROM tasks;
```
</div>
```sh
id | finished
----+----------
  2 |     True
  1 |    False
```

## See Also

[Data Types](..#datatypes)
