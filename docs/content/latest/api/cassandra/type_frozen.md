---
title: FROZEN
summary: Binary format for complex datatypes.
description: FROZEN Datatypes
menu:
  latest:
    parent: api-cassandra
    weight: 1400
aliases:
  - api/cassandra/type_frozen
  - api/cql/type_frozen
---

## Synopsis
`FROZEN` datatype is used to specify columns of binary strings that result from serializing either collections, tuples, or user-defined types.

## Syntax
```
type_specification ::= FROZEN<type>
```
Where

- `type` is a well-formed CQL datatype (additional restrictions for `type` are covered in the Semantics section below).

## Semantics

- Columns of type `FROZEN` can be part of the `PRIMARY KEY`.
- Type parameters of `FROZEN` type must be either [collection types](../type_collection) (`LIST`, `MAP`, or `SET`) or [user-defined types](../ddl_create_type).
- `FROZEN` types can be parameters of collection types.
- For any valid frozen type parameter `type`, values of `type` are convertible into `FROZEN<type>`.

## Examples

```{.sql .copy .separator-gt}
cqlsh:example> CREATE TABLE directory(file FROZEN<LIST<TEXT>> PRIMARY KEY, value BLOB);
```
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO directory(file, value) VALUES([ 'home', 'documents', 'homework.doc' ], 0x);
```
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO directory(file, value) VALUES([ 'home', 'downloads', 'textbook.pdf' ], 0x12ab21ef);
```
```{.sql .copy .separator-gt}
cqlsh:example> UPDATE directory SET value = 0xab00ff WHERE file = [ 'home', 'documents', 'homework.doc' ];
```
```{.sql .copy .separator-gt}
cqlsh:example> SELECT * FROM directory;
```
```sh
 file                                  | value
---------------------------------------+------------
 ['home', 'downloads', 'textbook.pdf'] | 0x12ab21ef
 ['home', 'documents', 'homework.doc'] |   0xab00ff
 ```

## See Also

[Data Types](..#datatypes)
