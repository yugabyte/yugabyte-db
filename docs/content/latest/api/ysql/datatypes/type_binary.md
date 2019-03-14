---
title: Binary Datatypes
linktitle: Binary
summary: Binary Datatypes
description: Binary Datatypes
menu:
  latest:
    identifier: api-ysql-datatypes-binary
    parent: api-ysql-datatypes
aliases:
  - /latest/api/ysql/datatypes/type_binary
isTocNested: true
showAsideToc: true
---

## Synopsis
BYTEA datatype represents binary string of bytes (octets). Binary string allows 0's and non-printable bytes.

DataType | Description |
---------|-------------|
BYTEA | Variable length binary string |

## Description
- `BYTEA` is used to declare a binary entity.

```
type_specification ::= BYTEA
```

- Escaped input can be used for input binary data.

```
SELECT E'\\001'::bytea
```

