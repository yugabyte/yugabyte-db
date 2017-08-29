---
title: Query Language DataTypes
summary: Datatypes that are used in SQL and/or CQL.
toc: true
---
<style>
table {
      float: left;
}
</style>

## Supported Types

YugaByte supports the following data types in the query language.

Type | Description |
-----|-------------|
[`TINYINT`](int.html) | 8-bit signed integer. |
[`SMALLINT`](int.html) | 16-bit signed integer. |
[<code>INT &#124; INT32</code>](int.html) | 32-bit signed integer. |
[`BIGINT`](int.html) | 64-bit signed integer. |
[`FLOAT`](float.html) | A 64-bit, inexact, floating-point number. |
[`DOUBLE`](float.html) | A 64-bit, inexact, floating-point number. |
[`DECIMAL`](decimal.html) | An exact, fixed-point number. |
[`BOOL`](bool.html) | A Boolean value. |
[`DATE`](date.html) | A date. |
[`TIMESTAMP`](timestamp.html) | A date and time pairing. |
[`INTERVAL`](interval.html) | A span of time. |
[`TEXT`](string.html) | A string of Unicode characters. |
[`BYTES`](bytes.html) | A string of binary characters. |

## Data Type Conversions & Casts
