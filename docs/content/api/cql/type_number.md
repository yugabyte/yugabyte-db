---
title: Non-integer Numbers
summary: FLOAT, DOUBLE, and DECIMAL
weight: 143
---

## Synopsis
Floating-point and fixed-point numbers are used to specify non-integer numbers. Different floating point datatypes represent different precisions numbers.

DataType | Description | Decimal Precision |
---------|-----|-----|
`FLOAT` | Inexact 32-bit floating point number | 7 |
`DOUBLE` | Inexact 64-bit floating point number | 15 |
`DECIMAL` | Exact fixed-point number | 99 |

## Syntax

```
type_specification ::= { FLOAT | DOUBLE | DOUBLE PRECISION | DECIMAL }

non_integer_floating_point_literal ::= non_integer_fixed_point_literal | "NaN" | "Infinity" | "-Infinity"

non_integer_fixed_point_literal ::= [ + | - ] { digit [ digit ...] '.' [ digit ...] | '.' digit [ digit ...] }

```

Where

- Columns of type `FLOAT`, `DOUBLE`, `DOUBLE PRECISION`, or `DECIMAL` can be part of the `PRIMARY KEY`.
- `DOUBLE` and `DOUBLE PRECISION` are aliases.
- `non_integer_floating_point_literal` is used for values of `FLOAT`, `DOUBLE` and `DOUBLE PRECISION` types.
- `non_integer_fixed_point_literal` is used for values of `DECIMAL` type.

## Semantics

- Values of different floating-point and fixed-point datatypes are comparable and convertible to one another.
  - Conversion from floating-point types into `DECIMAL` will raise an error for the special values `NaN`, `Infinity`, and `-Infinity`.
- Values of non-integer numeric datatypes are neither comparable nor convertible to integer although integers are convertible to them.
- The ordering for special floating-point values is defined as (in ascending order): `-Infinity`, all negative values in order, all positive values in order, `Infinity`, and `NaN`.

## Examples

``` sql
cqlsh:example> CREATE TABLE sensor_data (sensor_id INT PRIMARY KEY, float_val FLOAT, dbl_val DOUBLE, dec_val DECIMAL);
cqlsh:example> INSERT INTO sensor_data(sensor_id, float_val, dbl_val, dec_val) 
                  VALUES (1, 321.0456789, 321.0456789, 321.0456789);
cqlsh:example> -- Integers literals can also be used (Using upsert semantics to update a non-existent row).
cqlsh:example> UPDATE sensor_data SET float_val = 1, dbl_val = 1, dec_val = 1 WHERE sensor_id = 2;
cqlsh:example> SELECT * FROM sensor_data;

 sensor_id | float_val | dbl_val   | dec_val
-----------+-----------+-----------+-------------
         2 |         1 |         1 |           1
         1 | 321.04568 | 321.04568 | 321.0456789
```

## See Also

[Data Types](..#datatypes)
