---
title: BLOB
summary: Binary strings of variable length.
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

`BLOB` datatype is used to represent binary strings of variable length.

## Syntax

```
type_specification ::= BLOB
```

## Semantics

<li>Columns of type `BLOB` cannot be part of `PRIMARY KEY`.</li>
<li>`BLOB` are neither convertible nor comparable with other datatypes.</li>
<li>Two series of builtin-functions BlobAsType and TypeAsBlob are provided for convertion between `BLOB` and other datatypes.</li>
<li>`BLOB` size is virtually unlimited.</li>

## Examples
``` sql
cqlsh:yugaspace> CREATE TABLE yugatab(id INT PRIMARY KEY, content BLOB);
```

## See Also

[Data Types](..#datatypes)
