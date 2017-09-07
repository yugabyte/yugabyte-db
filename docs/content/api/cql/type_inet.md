---
title: INET
summary: IP Address String.
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

`INET` datatype is used to specify columns for data of IP addresses.

## Syntax

```
type_specification::= { INET }
```

## Semantics

<li>Implicitly, values of type `INET` datatypes are neither convertible nor comparable to other datatypes.</li>
<li>Value of text datatypes with correct format are convertible to `INET`.</li>

## Examples

``` sql
CREATE TABLE yuga_text(id INT PRIMARY KEY, address INET);
INSERT INTO yuga_text(id, address) VALUES(1, '10.10.10.10'); 
```

## See Also

[Data Types](..#datatypes)
