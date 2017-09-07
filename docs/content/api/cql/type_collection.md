---
title: Collection
summary: Collection types.
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

Collection datatypes are used to specify columns for data objects that can contains more than one values.

### LIST

### MAP

### SET

### FROZEN

## Syntax

```
type_specification::= { LIST | MAP | SET | FROZEN }
```

## Semantics

<li>Columns of type `LIST`, `MAP`, and `SET` cannot be part of `PRIMARY KEY`.</li>
<li>Implicitly, values of collection datatypes are neither convertible nor comparable to other datatypes.</li>

## Examples

``` sql
CREATE TABLE yuga_collection(ident INT PRIMARY KEY, l LIST<INT>, m MAP<TEXT:INT>, s SET<TEXT>);
```

## See Also

[Data Types](..#datatypes)
