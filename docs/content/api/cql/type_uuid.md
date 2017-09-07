---
title: UUID and TIMEUUID
summary: UUID types.
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

`UUID` datatype is used to specify columns for data of universally unique ids. `TIMEUUID` is a uuid that includes time.

## Syntax

```
type_specification::= { UUID | TIMEUUID }
```

## Semantics

<li>Implicitly, values of type `UUID` and `TIMEUUID` datatypes are neither convertible nor comparable to other datatypes.</li>
<li>Value of text datatypes with correct format are convertible to UUID types.</li>

## Examples

``` sql
CREATE TABLE yuga_text(id UUID PRIMARY KEY, ordered_id TIMEUUID);
```

## See Also

[Data Types](..#datatypes)
