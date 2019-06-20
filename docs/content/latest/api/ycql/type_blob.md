---
title: BLOB
summary: Binary strings of variable length
description: BLOB Type
menu:
  latest:
    parent: api-cassandra
    weight: 1370
aliases:
  - /latest/api/cassandra/type_blob
  - /latest/api/ycql/type_blob
isTocNested: true
showAsideToc: true
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

- Columns of type `BLOB` can be part of the `PRIMARY KEY`.
- Implicitly, `BLOB` datatype is neither convertible nor comparable with other datatypes.
- Two series of builtin-functions `BlobAs<Type>` and `<Type>AsBlob` are provided for conversion between `BLOB` and other datatypes.
- `BLOB` size is virtually unlimited.

## Examples

```sql
cqlsh:example> CREATE TABLE messages(id INT PRIMARY KEY, content BLOB);
```

```sql
cqlsh:example> INSERT INTO messages (id, content) VALUES (1, 0xab00ff);
```

```sql
cqlsh:example> INSERT INTO messages (id, content) VALUES (2, 0x);
```

```sql
cqlsh:example> UPDATE messages SET content = 0x0f0f WHERE id = 2;
```

```sql
cqlsh:example> SELECT * FROM messages;
```

```
 id | content
----+----------
  2 |   0x0f0f
  1 | 0xab00ff
```

## See Also

[Data Types](..#datatypes)
