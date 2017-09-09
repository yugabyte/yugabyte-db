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
#ptodo {
  color: red
}
</style>

## Synopsis
`UUID` datatype is used to specify columns for data of universally unique ids. `TIMEUUID` is a uuid that includes time.

## Syntax
```
type_specification ::= { UUID | TIMEUUID }
```

## Semantics
<li>Implicitly, values of type `UUID` and `TIMEUUID` datatypes are neither convertible nor comparable to other datatypes.</li>
<li>Value of text datatypes with correct format are convertible to UUID types.</li>

## Examples
``` sql
cqlsh:example> CREATE TABLE devices(id UUID PRIMARY KEY, ordered_id TIMEUUID);
cqlsh:example> INSERT INTO devices (id, ordered_id) 
               VALUES (123e4567-e89b-12d3-a456-426655440000, 123e4567-e89b-12d3-a456-426655440000);
cqlsh:example> -- `TIMEUUID`s must be type 1 `UUID`s (first number in third component).
cqlsh:example> INSERT INTO devices (id, ordered_id) 
               VALUES (123e4567-e89b-42d3-a456-426655440000, 123e4567-e89b-12d3-a456-426655440000);
cqlsh:example> UPDATE devices SET ordered_id = 00000000-0000-1000-0000-000000000000
               WHERE id = 123e4567-e89b-42d3-a456-426655440000; 

cqlsh:example> SELECT * FROM devices;

id                                   | ordered_id
--------------------------------------+--------------------------------------
 123e4567-e89b-12d3-a456-426655440000 | 123e4567-e89b-12d3-a456-426655440000
 123e4567-e89b-42d3-a456-426655440000 | 00000000-0000-1000-0000-000000000000
```

## See Also

[Data Types](..#datatypes)
