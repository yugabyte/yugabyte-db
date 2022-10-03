---
title: UUID data type [YSQL]
headerTitle: UUID data type
linkTitle: UUID
description: Represents Universally Unique Identifiers (UUIDs).
menu:
  preview:
    identifier: api-ysql-datatypes-uuid
    parent: api-ysql-datatypes
aliases:
  - /preview/api/ysql/datatypes/type_uuid
type: docs
---

## Synopsis

The `UUID` data type represents Universally Unique Identifiers (UUIDs). A UUID is a sequence of 32 hexadecimal digits separated by hyphens (8 digits - 4 digits - 4 digits - 4 digits - 12 digits) representing the 128 bits.

## Description

```ebnf
type_specification ::= UUID
```

## Examples

```output
ffffffff-ffff-ffff-ffff-ffffffffffff
{aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa}
12341234-1234-1234-1234-123412341234
```
