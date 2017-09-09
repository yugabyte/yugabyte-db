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
cqlsh:example> CREATE TABLE sensor_data (sensor_id INT PRIMARY KEY, float_val FLOAT, dbl_val DOUBLE, dec_val DECIMAL);
cqlsh:example> INSERT INTO sensor_data(sensor_id, float_val, dbl_val, dec_val) VALUES (1, 321.0456789, 321.0456789, 321.0456789);
cqlsh:example> -- Integers literals can also be used (Using upsert semantics to update a non-existent row).
cqlsh:example> UPDATE sensor_data SET float_val = 1, dbl_val = 1, dec_val = 1 WHERE sensor_id = 2;
ccqlsh:example> SELECT * FROM sensor_data;
```

```
 sensor_id | float_val | dbl_val   | dec_val
-----------+-----------+-----------+-------------
         2 |         1 |         1 |           1
         1 | 321.04568 | 321.04568 | 321.0456789
```

## See Also

[Data Types](..#datatypes)
