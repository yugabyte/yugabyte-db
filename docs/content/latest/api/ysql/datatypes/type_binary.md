---
title: Binary fata types
linktitle: Binary
summary: Binary data types
description: Binary data types
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

BYTEA data type represents binary string of bytes (octets). Binary string allows 0's and non-printable bytes.

Data type | Description |
----------|-------------|
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
