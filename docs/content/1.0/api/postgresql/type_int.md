---
title: INTEGER
summary: Signed integers of different ranges
description: Integer Types
menu:
  1.0:
    identifier: api-postgresql-int
    parent: api-postgresql-type
aliases:
  - api/postgresql/type/int
  - api/pgsql/type/int
---

## Synopsis
There are several different datatypes for integers of different value ranges. Integers can be set, inserted, incremented, and decremented.

DataType | Min | Max |
---------|-----|-----|
`SMALLINT` | -32,768 | 32,767 |
`INT` or `INTEGER` | -2,147,483,648 | 2,147,483,647 |
`BIGINT` | -9,223,372,036,854,775,808 | 9,223,372,036,854,775,807 |

## Syntax
The following keywords are used to specify a column of type integer for different constraints including its value ranges.

```
type_specification ::= SMALLINT | INT | INTEGER | BIGINT

integer_literal ::= [ + | - ] digit [ { digit | , } ... ]
```

## Semantics

- Columns of type `SMALLINT`, `INT`, `INTEGER`, or `BIGINT` can be part of the `PRIMARY KEY`.
- Values of different integer datatypes are comparable and convertible to one another.
- Values of integer datatypes are convertible but not comparable to floating point number.
- Currently, values of floating point datatypes are not convertible to integers. This restriction
will be removed in the near future.

## See Also

[Data Types](../type)
