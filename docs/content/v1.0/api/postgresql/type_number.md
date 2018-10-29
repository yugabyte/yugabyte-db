---
title: FLOAT
summary: FLOAT, DOUBLE, and DECIMAL
description: Floating-Point Types
menu:
  v1.0:
    identifier: api-postgresql-number
    parent: api-postgresql-type
---

## Synopsis
Floating-point and fixed-point numbers are used to specify non-integer numbers. Different floating point datatypes represent different precisions numbers.

DataType | Description | Decimal Precision |
---------|-----|-----|
`REAL` | Inexact 32-bit floating point number | 6 |
`DOUBLE PRECISION` | Inexact 64-bit floating point number | 15 |
`FLOAT` | Inexact 64-bit floating point number | variable |
`DECIMAL` | Exact fixed-point number | 99 |

## Syntax

```
type_specification ::= { FLOAT | DOUBLE PRECISION | DECIMAL | DEC }

non_integer_floating_point_literal ::= non_integer_fixed_point_literal | "NaN" | "Infinity" | "-Infinity"

non_integer_fixed_point_literal ::= [ + | - ] { digit [ digit ...] '.' [ digit ...] | '.' digit [ digit ...] }

```

Where

- Columns of type `FLOAT`, `DOUBLE PRECISION`, `DEC`, or `DECIMAL` can be part of the `PRIMARY KEY`.
- `non_integer_floating_point_literal` is used for values of `FLOAT`, `DOUBLE` and `DOUBLE PRECISION` types.
- `non_integer_fixed_point_literal` is used for values of `DECIMAL` type.

## Semantics

- Values of different floating-point and fixed-point datatypes are comparable and convertible to one another.
- Conversion from floating-point types into `DECIMAL` will raise an error for the special values `NaN`, `Infinity`, and `-Infinity`.
- The ordering for special floating-point values is defined as (in ascending order): `-Infinity`, all negative values in order, all positive values in order, `Infinity`, and `NaN`.
- Values of non-integer numeric datatypes are neither comparable nor convertible to integer although integers are convertible to them. This restriction will be removed.

## See Also

[Data Types](../type)
