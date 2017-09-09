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
<li>Type parameters of `FROZEN` type must be either collection types (`LIST`, `MAP`, or `SET`) or user-defined types</li>
<li> `FROZEN` types can be parameters of collection types. </li>

## Examples

``` sql
cqlsh:example> CREATE TABLE directory(file FROZEN<LIST<TEXT>> PRIMARY KEY, value BLOB);
cqlsh:example> INSERT INTO directory(file, value) VALUES([ 'home', 'documents', 'homework.doc' ], 0x);
cqlsh:example> INSERT INTO directory(file, value) VALUES([ 'home', 'downloads', 'textbook.pdf' ], 0x12ab21ef);
cqlsh:example> UPDATE directory SET value = 0xab00ff WHERE file = [ 'home', 'documents', 'homework.doc' ];
cqlsh:example> SELECT * FROM directory;
```

```
 file                                  | value
---------------------------------------+------------
 ['home', 'downloads', 'textbook.pdf'] | 0x12ab21ef
 ['home', 'documents', 'homework.doc'] |   0xab00ff
 ```

## See Also

[Data Types](..#datatypes)
