---
title: NUMERIC
linktitle: Numeric
description: Numeric Types
summary: Signed integers and Floating and Fixed point numbers
menu:
  latest:
    identifier: api-ysql-datatypes-numeric
    parent: api-ysql-datatypes
aliases:
  - /latest/api/ysql/type_number/
  - /latest/api/ysql/type_int/
isTocNested: true
showAsideToc: true
---

## Synopsis
YSQL support integers and floating-point and fixed-point numbers of different value ranges and precisions.

Datatype | Description | Min | Max |
---------|-------------|-----|-----|
`BIGINT` | 8 bytes | -9,223,372,036,854,775,808 | 9,223,372,036,854,775,807 |
`DEC` | Exact 64-bit fixed point number | variable | variable |
`DECIMAL` | Exact 64-bit fixed point number | variable | variable |
`DOUBLE PRECISION` | Inexact 64-bit floating point number | 15-digit precision | 15-digit precision|
`FLOAT` | Inexact 64-bit floating point number | variable | variable |
`INTEGER` | 4-byte integer | -2,147,483,648 | 2,147,483,647 |
`INT` | 4-byte interger | -2,147,483,648 | 2,147,483,647 |
`NUMERIC` | Exact 64-bit fixed point number | variable | variable |
`REAL` | Inexact 32-bit floating point number | 6-digit precision | 6-digit precision |
`SMALLINT` | 2-byte integer | -32,768 | 32,767 |

## Integers
- The following keywords are used to specify a column of type integer for different constraints including its value ranges.

```
type_specification ::= SMALLINT | INT | INTEGER | BIGINT
integer_literal ::= [ + | - ] digit [ { digit | , } ... ]
```

- Columns of type `SMALLINT`, `INT`, `INTEGER`, or `BIGINT` can be part of the `PRIMARY KEY`.
- Values of different integer datatypes are comparable and convertible to one another.
- Values of integer datatypes are convertible but not comparable to floating point number.
- Currently, values of floating point datatypes are not convertible to integers. This restriction
will be removed in the near future.

## Floating-Point Numbers
- The following keywords are used to specify a column of floating-point types for different constraints including its value ranges.
```
type_specification ::= { FLOAT | DOUBLE PRECISION | REAL }
floating_point_literal ::= non_integer_fixed_point_literal | "NaN" | "Infinity" | "-Infinity"
```

- Columns of type `REAL`, `DOUBLE PRECISION`, and `FLOAT` can be part of the `PRIMARY KEY`.
- Values of different floating-point and fixed-point datatypes are comparable and convertible to one another.
- Conversion from floating-point types into `DECIMAL` will raise an error for the special values `NaN`, `Infinity`, and `-Infinity`.
- The ordering for special floating-point values is defined as (in ascending order): `-Infinity`, all negative values in order, all positive values in order, `Infinity`, and `NaN`.
- Values of non-integer numeric datatypes are neither comparable nor convertible to integer although integers are convertible to them. This restriction will be removed.

## Fixed-Point Numbers
- The following keywords are used to specify a column of floating-point types for different constraints including its value ranges.

```
type_specification ::= { DEC | DECIMAL | NUMERIC }
fixed_point_literal ::= [ + | - ] { digit [ digit ...] '.' [ digit ...] | '.' digit [ digit ...] }

```

- Columns of type `DEC`, `DECIMAL`, and `NUMERIC` can be part of the `PRIMARY KEY`.
- Values of different floating-point and fixed-point datatypes are comparable and convertible to one another.
- Values of non-integer numeric datatypes are neither comparable nor convertible to integer although integers are convertible to them. This restriction will be removed.
