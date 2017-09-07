---
title: DateTime
summary: Date and time.
toc: false
---
<style>
table {
  float: left;
}
#psyn {
  text-indent: 50px;
}
#psyn2 {
  text-indent: 100px;
}
#ptodo {
  color: red
}
</style>

## Synopsis

Datetime datatypes are used to specify data of date and time at a timezone, `DATE` for a specific day, `TIME` for time of day, and `TIMESTAMP` for the combination of both date and time. If not specified, the default timezone is UTC.

## Syntax

```
type_specification::= { TIMESTAMP | DATE | TIME }
```

## Semantics

<li>Implicitly, value of type datetime type are neither convertible nor comparable to other datatypes.</li>
<li>Value of integer and text datatypes with correct format are convertible to datetime types.</li>

## Examples

``` sql
CREATE TABLE yuga_births(name TEXT PRIMARY KEY, birth_time TIMESTAMP);
```
## See Also

[Data Types](..#datatypes)
