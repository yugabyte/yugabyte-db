---
title: BOOLEAN
summary: Boolean values of false or true.
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
`BOOLEAN` datatype is used to specify values of either `true` or `false`.

## Syntax

```
type_specification::= BOOLEAN

boolean_literal::= { TRUE | FALSE }
```

## Semantics

<li>Columns of type `BOOLEAN` can not be a part of `PRRIMARY KEY`.</li>
<li>Columns of type `BOOLEAN` can be set, inserted, and compared.</li>
<li>In `WHERE` and `IF` clause, `BOOLEAN` columns cannot be used as a standalone expression. They must be compared with either `true` or `false`. For example, `WHERE boolean_column = TRUE` is valid while `WHERE boolean_column` is not.
<li>Implicitly `BOOLEAN` is neither comparable nor convertible to any other datatypes.</li>

## Examples

``` sql
cqlsh:myspace> CREATE TABLE bool (a INT PRIMARY KEY, b BOOLEAN);
```
## See Also

[Data Types](data-types.html)
