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
`LIST` is an ordered collection of elements. All elements in a `LIST` must be of the same primitive types. Elements can be prepend or append by `+` operator to a list, removed by `-` operator, and referenced by their indexes of that list by `[]` operator.

### MAP
`MAP` is an unordered collection of pairs of elements, a key and a value. With their key values, elements in a `MAP` can be set by the `[]` operator, added by the `+` operator, and removed by the `-` operator.

### SET
`SET` is a sorted collection of elements. The sorting order is implementation-dependent. Elements can be added by `+` operator and removed by `-` operator. When queried, the elements of a set will be returned in sorting order.

## Syntax

```
type_specification::= { LIST<type> | MAP<key_type:type> | SET<type> }
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
