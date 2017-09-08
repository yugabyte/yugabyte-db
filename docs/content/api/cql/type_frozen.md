---
title: FROZEN Datatypes
summary: Binary format for complex datatypes.
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
`FROZEN` datatype is used to specify columns of binary strings that are resulted from serializing either collections, tuples, or user-defined types.

## Syntax
```
type_specification ::= FROZEN <{ LIST<type> | MAP<key_type:type> | SET<type> | user-defined-type }>
```
Where
  <type> must be primitive type.
  <key_type> must be primitive type that are hashable.

## Semantics
<li>Columns of type `FROZEN` can be part of `PRIMARY KEY`.</li>

## Examples
``` sql
cqlsh:yugaspace> CREATE TABLE yuga_directory(files FROZEN<LIST<TEXT>> PRIMARY KEY);
cqlsh:yugaspace> INSERT INTO yuga_directory(files) VALUES([ 'yugafile1', 'yugafile2' ]);
```

## See Also

[Data Types](..#datatypes)
