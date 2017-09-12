---
title: BOOLEAN Type
summary: Boolean values of false or true.
toc: false
---

## Synopsis

`BOOLEAN` datatype is used to specify values of either `true` or `false`.

## Syntax
```
type_specification ::= BOOLEAN

boolean_literal ::= TRUE | FALSE
```

## Semantics

- Columns of type `BOOLEAN` can not be a part of `PRRIMARY KEY`.
- Columns of type `BOOLEAN` can be set, inserted, and compared.
- In `WHERE` and `IF` clause, `BOOLEAN` columns cannot be used as a standalone expression. They must be compared with either `true` or `false`. For example, `WHERE boolean_column = TRUE` is valid while `WHERE boolean_column` is not.
- Implicitly, `BOOLEAN` is neither comparable nor convertible to any other datatypes.

## Examples

``` sql
cqlsh:example> CREATE TABLE tasks (id INT PRIMARY KEY, finished BOOLEAN);
cqlsh:example> INSERT INTO tasks (id, finished) VALUES (1, false);
cqlsh:example> INSERT INTO tasks (id, finished) VALUES (2, false);
cqlsh:example> UPDATE tasks SET finished = true WHERE id = 2;
cqlsh:example> SELECT * FROM tasks;

id | finished
----+----------
  2 |     True
  1 |    False
```

## See Also

[Data Types](..#datatypes)
