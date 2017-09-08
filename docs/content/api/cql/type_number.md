---
title: Non-integer Numbers
summary: FLOAT, DOUBLE, and DECIMAL
toc: false
---
<style>
table {
  float: left;
}
#psyn {
  text-indent: 50px;
}
#ptodo {
  color: red
}
</style>

## Synopsis
Floating-point and fixed-point numbers are used to specified non-integer numbers. Different floating point datatypes represent different precisions numbers.

DataType | Description | Decimal Precision |
---------|-----|-----|
`FLOAT` | Inexact 32-bit floating point number | 7 |
`DOUBLE` | Inexact 64-bit floating point number | 15 |
`DECIMAL` | Exact fixed-point number | 99 |

## Syntax
The following keywords are used to specify a column of non-integer number.
```
type_specification ::= { FLOAT | DOUBLE | DOUBLE PRECISION | DECIMAL }
```
Where
<li>`DOUBLE` and `DOUBLE PRECISION` are aliases.</li>

## Semantics

<li>Values of different floating-point and fixed-point datatypes are comparable and convertible to one another.</li>
<li>Values of these non-integer numeric datatypes are neither comparable nor convertible to integer although integers are convertible to them.</li>

## Examples
``` sql
cqlsh:yugaspace> CREATE TABLE yuga_floats (id INT PRIMARY KEY, f FLOAT, d1 DOUBLE, d2 DOUBLE PRECISION, d DECIMAL);
```

## See Also

[Data Types](..#datatypes)
