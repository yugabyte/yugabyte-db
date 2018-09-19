---
title: BOOLEAN
summary: Boolean values of false or true
description: BOOLEAN Type
menu:
  v1.0:
    identifier: api-postgresql-bool
    parent: api-postgresql-type
aliases:
  - api/postgresql/type/bool
  - api/pgsql/type/bool
---

## Synopsis

`BOOLEAN` datatype is used to specify values of either `true` or `false`.

## Syntax
```
type_specification ::= BOOLEAN

boolean_literal ::= TRUE | FALSE
```

## Semantics

- Columns of type `BOOLEAN` can be updated, inserted, and compared.
- In `WHERE` clause, `BOOLEAN` columns can be used as a boolean expression.

## See Also

[Data Types](../type)
