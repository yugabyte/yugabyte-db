---
title: UUID Datatypes
linktitle: Uuid
summary: UUID Datatypes
description: UUID Datatypes
menu:
  latest:
    identifier: api-ysql-datatypes-uuid
    parent: api-ysql-datatypes
aliases:
  - /latest/api/ysql/datatypes/type_uuid
isTocNested: true
showAsideToc: true
---

## Synopsis
UUID datatype represents Universally Unique Identifiers. A UUID is a sequence of 32 hexadecimal digits separated by hyphens (8 digits - 4 digits - 4 digits - 4 digits - 12 digits) representing the 128 bits.

## Description

```
type_specification ::= UUID
```

## Examples:

```
ffffffff-ffff-ffff-ffff-ffffffffffff
{aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa}
12341234-1234-1234-1234-123412341234
```

## See Also

[Data Types](../datatypes)
