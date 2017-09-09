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
cqlsh:example> CREATE TABLE messages(id INT PRIMARY KEY, content BLOB);
cqlsh:example> INSERT INTO messages (id, content) VALUES (1, 0xab00ff);
cqlsh:example> INSERT INTO messages (id, content) VALUES (2, 0x);
cqlsh:example> UPDATE messages SET content = 0x0f0f WHERE id = 2;
cqlsh:example> SELECT * FROM messages;

 id | content
----+----------
  2 |   0x0f0f
  1 | 0xab00ff
```

## See Also

[Data Types](..#datatypes)
