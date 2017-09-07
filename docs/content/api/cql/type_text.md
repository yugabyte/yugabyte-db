---
title: TEXT
summary: String of Unicode characters.
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

`TEXT` datatype is used to specify data of a string of unicode characters.

## Syntax

```
type_specification::= { TEXT | VARCHAR }
```

`TEXT` and `VARCHAR` are aliases.

## Semantics

<li>Implicitly, value of type `TEXT` datatype are neither convertible nor comparable to non-text datatypes.</li>
<li>The length of `TEXT` string is virtually unlimited.</li>

## Examples

``` sql
CREATE TABLE yuga_text(name TEXT PRIMARY KEY, nick_name VARCHAR);
```
## See Also

[Data Types](..#datatypes)
