---
title: BLOB Type
summary: Binary strings of variable length.
---

## Synopsis

`BLOB` datatype is used to represent arbitrary binary data of variable length.

## Syntax

```
type_specification ::= BLOB

blob_literal ::= "0x" [ hex_digit hex_digit ...]
```

Where

- `hex_digit` is a hexadecimal digit (`[0-9a-fA-F]`).

## Semantics

- Columns of type `BLOB` can be part of `PRIMARY KEY`.
- Implicitly, `BLOB` datayype is neither convertible nor comparable with other datatypes.
- Two series of builtin-functions `BlobAs<Type>` and `<Type>AsBlob` are provided for conversion between `BLOB` and other datatypes.
- `BLOB` size is virtually unlimited.

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
