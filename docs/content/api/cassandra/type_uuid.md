---
title: UUID and TIMEUUID
summary: UUID types
weight: 1460
aliases:
  - api/cql/type_uuid
---

## Synopsis
`UUID` datatype is used to specify columns for data of universally unique ids. `TIMEUUID` is a uuid variant that includes time information.

## Syntax
```
type_specification ::= { UUID | TIMEUUID }

uuid_literal ::= 4hex_block 4hex_block '-' 4hex_block '-' 4hex_block '-' 4hex_block '-' 4hex_block 4hex_block 4hex_block

4hex_block ::= hex_digit hex_digit hex_digit hex_digit

```

Where 

- `hex_digit` is a hexadecimal digit (`[0-9a-fA-F]`);

## Semantics

- Columns of type `UUID` or `TIMEUUID` can be part of the `PRIMARY KEY`.
- Implicitly, values of type `UUID` and `TIMEUUID` datatypes are neither convertible nor comparable to other datatypes.
- Value of text datatypes with the correct format are convertible to UUID types.

## Examples
```{.sql .copy .separator-gt}
cqlsh:example> CREATE TABLE devices(id UUID PRIMARY KEY, ordered_id TIMEUUID);
```
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO devices (id, ordered_id) 
               VALUES (123e4567-e89b-12d3-a456-426655440000, 123e4567-e89b-12d3-a456-426655440000);
```
`TIMEUUID`s must be type 1 `UUID`s (first number in third component).
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO devices (id, ordered_id) 
               VALUES (123e4567-e89b-42d3-a456-426655440000, 123e4567-e89b-12d3-a456-426655440000);
```
```{.sql .copy .separator-gt}
cqlsh:example> UPDATE devices SET ordered_id = 00000000-0000-1000-0000-000000000000
               WHERE id = 123e4567-e89b-42d3-a456-426655440000; 
```
```{.sql .copy .separator-gt}
cqlsh:example> SELECT * FROM devices;
```
```sh
id                                   | ordered_id
--------------------------------------+--------------------------------------
 123e4567-e89b-12d3-a456-426655440000 | 123e4567-e89b-12d3-a456-426655440000
 123e4567-e89b-42d3-a456-426655440000 | 00000000-0000-1000-0000-000000000000
```

## See Also

[Data Types](..#datatypes)
